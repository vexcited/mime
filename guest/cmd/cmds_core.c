/* the control flow and environment commands: echo set goto call exit shift setlocal and friends. */
#include "cmd.h"

/* ------------------------------------------------------------------ prompt and ctrl+c */

static void prompt_date(STR *out)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    WCHAR day[16], date[64];
    if (!GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, L"ddd", day, NUM(day)))
        day[0] = 0;
    if (!GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, NUM(date)))
        _snwprintf(date, NUM(date), L"%02u/%02u/%04u", st.wMonth, st.wDay, st.wYear);
    str_fmt(out, L"%ls %ls", day, date);
}

static void version_string(STR *out)
{
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
    str_fmt(out, L"Microsoft Windows [Version %lu.%lu.%lu.%lu]", vi.dwMajorVersion, vi.dwMinorVersion,
            vi.dwBuildNumber, revision);
}

void print_prompt(void)
{
    if (!g.echo)
        return;
    if (!g.nul_node)
        out_s(L"\r\n");
    STR text;
    str_init(&text);
    STR template_;
    str_init(&template_);
    if (!env_get(L"PROMPT", &template_))
        str_adds(&template_, L"$P$G");
    const WCHAR *t = str_get(&template_);
    for (; *t; t++) {
        if (*t != L'$') {
            str_addc(&text, *t);
            continue;
        }
        t++;
        if (!*t)
            break;
        switch (towupper(*t)) {
        case L'A': str_addc(&text, L'&'); break;
        case L'B': str_addc(&text, L'|'); break;
        case L'C': str_addc(&text, L'('); break;
        case L'D': prompt_date(&text); break;
        case L'E': str_addc(&text, 0x1B); break;
        case L'F': str_addc(&text, L')'); break;
        case L'G': str_addc(&text, L'>'); break;
        case L'H': str_addc(&text, L'\b'); break;
        case L'L': str_addc(&text, L'<'); break;
        case L'N': {
            STR cwd;
            str_init(&cwd);
            if (get_cwd(&cwd) && cwd.len >= 2)
                str_addc(&text, cwd.p[0]);
            str_free(&cwd);
            break;
        }
        case L'P': {
            STR cwd;
            str_init(&cwd);
            if (get_cwd(&cwd))
                str_adds(&text, str_get(&cwd));
            str_free(&cwd);
            break;
        }
        case L'Q': str_addc(&text, L'='); break;
        case L'S': str_addc(&text, L' '); break;
        case L'T': {
            SYSTEMTIME st;
            GetLocalTime(&st);
            str_fmt(&text, L"%2u:%02u:%02u.%02u", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds / 10);
            break;
        }
        case L'V': version_string(&text); break;
        case L'_': str_adds(&text, L"\r\n"); break;
        case L'$': str_addc(&text, L'$'); break;
        case L'+':
            if (g.ext)
                for (int i = 0; i < g.pushd_count; i++)
                    str_addc(&text, L'+');
            break;
        default: break;
        }
    }
    out_n(text.p ? text.p : L"", text.len);
    str_free(&text);
    str_free(&template_);
}

/* asks whether to end the batch job after ctrl+c, TRUE when it ends. */
BOOL ask_terminate_batch(void)
{
    if (!g.ctrlc)
        return FALSE;
    InterlockedExchange(&g.ctrlc, 0);
    if (!g.batch)
        return FALSE;
    for (;;) {
        out_s(L"Terminate batch job (Y/N)? ");
        WCHAR c;
        if (!read_key(&c)) {
            out_s(L"\r\n");
            return FALSE;
        }
        c = towupper(c);
        if (c == L'Y' || c == L'N') {
            out_fmt(L"%lc\r\n", c);
            if (c == L'Y') {
                g.abort_all = TRUE;
                g.goto_pending = TRUE;
                return TRUE;
            }
            return FALSE;
        }
        out_s(L"\r\n");
    }
}

void ctrlc_poll(void)
{
    if (g.ctrlc)
        ask_terminate_batch();
}

/* ------------------------------------------------------------------ echo */

int internal_echo(const WCHAR *args, NODE *node)
{
    const WCHAR *p = skip_space(args);
    if (!*p && (!args[0] || is_space(args[0]))) {
        out_fmt(L"ECHO is %ls.\r\n", g.echo ? L"on" : L"off");
        return 0;
    }
    if (!_wcsnicmp(p, L"on", 2) && !*skip_space(p + 2)) {
        g.echo = TRUE;
        return 0;
    }
    if (!_wcsnicmp(p, L"off", 3) && !*skip_space(p + 3)) {
        g.echo = FALSE;
        return 0;
    }
    out_s(args[0] ? args + 1 : args);
    out_s(L"\r\n");
    return 0;
}

/* ------------------------------------------------------------------ set */

static int set_prompt_input(const WCHAR *text)
{
    STR spec;
    str_init(&spec);
    const WCHAR *p = skip_space(text);
    str_adds(&spec, p);
    if (spec.len && spec.p[0] == L'"') {
        memmove(spec.p, spec.p + 1, spec.len * sizeof(WCHAR));
        spec.len--;
        WCHAR *q = wcsrchr(spec.p, L'"');
        if (q) {
            *q = 0;
            spec.len = (SIZE_T)(q - spec.p);
        }
    }
    WCHAR *eq = wcschr(str_get(&spec), L'=');
    if (!eq || eq == spec.p) {
        str_free(&spec);
        msg_syntax();
        return 1;
    }
    *eq = 0;
    const WCHAR *name = spec.p;
    const WCHAR *prompt = eq + 1;
    out_s(prompt);
    STR line;
    str_init(&line);
    BOOL got = read_stdin_line(&line, FALSE);
    int result = 1;
    if (got && line.len) {
        env_set(name, str_get(&line));
        result = 0;
    }
    str_free(&line);
    str_free(&spec);
    if (result)
        set_errorlevel(1);
    return result;
}

int internal_set(const WCHAR *args, NODE *node)
{
    const WCHAR *p = skip_space(args);
    if (!*p) {
        env_print_all(nullptr);
        return 0;
    }
    if (g.ext && !_wcsnicmp(p, L"/a", 2))
        return set_arithmetic(p + 2, g.batch == nullptr);
    if (g.ext && !_wcsnicmp(p, L"/p", 2))
        return set_prompt_input(p + 2);
    if (*p == L'/') {
        msg_syntax();
        return 1;
    }
    STR spec;
    str_init(&spec);
    str_adds(&spec, p);
    if (spec.p[0] == L'"') {
        WCHAR *q = spec.p + 1;
        while (*q && *q <= L' ')
            q++;
        memmove(spec.p, q, (wcslen(q) + 1) * sizeof(WCHAR));
        WCHAR *last = wcsrchr(spec.p, L'"');
        if (last)
            *last = 0;
    }
    int result = 0;
    if (spec.p[0] == L'=') {
        msg_syntax();
        result = 1;
    } else {
        WCHAR *eq = wcschr(spec.p, L'=');
        if (eq) {
            *eq = 0;
            if (!env_set(spec.p, eq + 1) && GetLastError() != ERROR_ENVVAR_NOT_FOUND) {
                err_s(L"The system is out of environment space.\r\n");
                result = 1;
            }
        } else {
            /* trailing blanks of the name do not count when listing. */
            SIZE_T n = wcslen(spec.p);
            while (n && is_space(spec.p[n - 1]))
                spec.p[--n] = 0;
            result = env_print_all(spec.p) ? 0 : 1;
        }
    }
    str_free(&spec);
    return result;
}

/* ------------------------------------------------------------------ batch control */

static void terminate_current_batch(void)
{
    BATCH *b = g.batch;
    if (!b)
        return;
    LARGE_INTEGER end = {};
    HANDLE f = CreateFileW(b->path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        GetFileSizeEx(f, &end);
        CloseHandle(f);
    }
    b->offset = end.QuadPart + 1;
    g.goto_pending = TRUE;
}

int internal_goto(const WCHAR *args, NODE *node)
{
    if (!g.batch)
        return 1;
    if (!batch_goto(g.batch, args)) {
        set_errorlevel(1);
        terminate_current_batch();
        return 1;
    }
    return 0;
}

int internal_shift(const WCHAR *args, NODE *node)
{
    if (!g.batch)
        return 0;
    const WCHAR *p = skip_space(args);
    int from = 1;
    if (g.ext && p[0] == L'/') {
        if (p[1] >= L'0' && p[1] <= L'9' && !p[2])
            from = p[1] - L'0';
        else {
            err_s(L"Invalid parameter to SHIFT command\r\n");
            return 1;
        }
        if (from == 0) {
            batch_shift(g.batch, 0);
            return 0;
        }
    }
    batch_shift(g.batch, from);
    return 0;
}

int internal_setlocal(const WCHAR *args, NODE *node)
{
    BATCH *b = g.batch;
    if (!b)
        return 0;
    if (b->nlocals >= SETLOCAL_LIMIT) {
        err_s(L"Maximum setlocal recursion level reached.\r\n");
        return 1;
    }
    b->locals[b->nlocals++] = env_snapshot();
    ARGS a;
    args_split(args, &a, FALSE);
    int result = 0;
    for (int i = 0; i < a.n; i++) {
        const WCHAR *w = a.v[i];
        if (!_wcsicmp(w, L"enableextensions"))
            g.ext = TRUE;
        else if (!_wcsicmp(w, L"disableextensions"))
            g.ext = FALSE;
        else if (!_wcsicmp(w, L"enabledelayedexpansion"))
            g.delayed = TRUE;
        else if (!_wcsicmp(w, L"disabledelayedexpansion"))
            g.delayed = FALSE;
        else {
            err_s(L"Invalid parameter to SETLOCAL command\r\n");
            set_errorlevel(1);
            result = 1;
            break;
        }
    }
    args_free(&a);
    if (!result)
        set_errorlevel(0);
    return result;
}

int internal_endlocal(const WCHAR *args, NODE *node)
{
    BATCH *b = g.batch;
    if (!b || b->nlocals == 0)
        return 0;
    LOCALENV *snap = b->locals[--b->nlocals];
    b->locals[b->nlocals] = nullptr;
    env_restore(snap);
    env_free_snapshot(snap);
    return 0;
}

int internal_exit(const WCHAR *args, NODE *node)
{
    const WCHAR *p = skip_space(args);
    BOOL relative = FALSE;
    if (!_wcsnicmp(p, L"/b", 2)) {
        relative = TRUE;
        p = skip_space(p + 2);
    }
    BOOL have_code = FALSE;
    LONG code = 0;
    if (*p == L'-' || is_digit(*p)) {
        const WCHAR *q = p;
        BOOL neg = FALSE;
        if (*q == L'-') {
            neg = TRUE;
            q++;
        }
        if (is_digit(*q)) {
            LONGLONG v = 0;
            while (is_digit(*q))
                v = v * 10 + (*q++ - L'0');
            code = (LONG)(neg ? -v : v);
            have_code = TRUE;
        }
    }
    if (have_code)
        set_errorlevel((DWORD)code);
    if (relative && g.batch) {
        terminate_current_batch();
        return have_code ? (int)code : 0;
    }
    while (g.pushd_count > 0) {
        xfree(g.pushd[--g.pushd_count]);
    }
    ExitProcess(g.errorlevel);
}

/* call: another batch file or label with its own arguments, or a command. */
int internal_call(const WCHAR *args, NODE *node)
{
    const WCHAR *p = skip_space(args);
    if (!*p) {
        return 0;
    }
    /* the carets of the text are escapes again when the text is parsed a second time. */
    STR doubled;
    str_init(&doubled);
    for (const WCHAR *c = p; *c; c++) {
        str_addc(&doubled, *c);
        if (*c == L'^')
            str_addc(&doubled, L'^');
    }
    STR expanded;
    str_init(&expanded);
    BOOL ok = expand_percent(str_get(&doubled), doubled.len, &expanded);
    str_free(&doubled);
    if (!ok) {
        str_free(&expanded);
        return 1;
    }
    int error = 0;
    NODE *parsed = parse_line(str_get(&expanded), nullptr, TRUE, &error);
    str_free(&expanded);
    if (error)
        return 1;
    if (!parsed)
        return 0;
    int result;
    if (parsed->kind == N_CMD) {
        const WCHAR *t = skip_space(parsed->text ? parsed->text : L"");
        if (g.ext && *t == L':') {
            if (!g.batch) {
                err_s(L"Invalid attempt to call batch label outside of batch script.\r\n");
                node_free(parsed);
                return 1;
            }
            /* the label and the arguments after it. */
            const WCHAR *e = t;
            while (*e && !is_space(*e) && *e != L',' && *e != L';' && *e != L'=')
                e++;
            WCHAR *label = xdupn(t, (SIZE_T)(e - t));
            if (g.nest > 3000) {
                err_fmt(L"******  B A T C H   R E C U R S I O N  exceeds STACK limits ******\r\nRecursion Count=%d\r\n"
                        L"******       B A T C H   PROCESSING IS   A B O R T E D      ******\r\n",
                        g.nest);
                g.abort_all = TRUE;
                node_free(parsed);
                return 1;
            }
            BATCH *parent = g.batch;
            BATCH *b = batch_begin(parent->path, label, e);
            b->offset = parent->offset;
            xfree(label);
            g.nest++;
            if (batch_goto(b, b->name)) {
                result = run_batch_loop(b);
            } else {
                set_errorlevel(1);
                result = 1;
            }
            g.nest--;
            g.goto_pending = FALSE;
            batch_end(b);
        } else {
            g.call_flag = TRUE;
            result = run_cmd_node(parsed);
            g.call_flag = FALSE;
        }
    } else {
        result = exec_node(parsed, FALSE);
    }
    node_free(parsed);
    return result ? result : (int)g.errorlevel;
}

/* ------------------------------------------------------------------ small commands */

int internal_rem(const WCHAR *args, NODE *node)
{
    return 0;
}

int internal_break(const WCHAR *args, NODE *node)
{
    return 0;
}

int internal_pause(const WCHAR *args, NODE *node)
{
    out_s(L"Press any key to continue . . . ");
    WCHAR c;
    read_key(&c);
    out_s(L"\r\n");
    return 0;
}

int internal_title(const WCHAR *args, NODE *node)
{
    const WCHAR *p = skip_space(args);
    if (*p)
        SetConsoleTitleW(p);
    return 0;
}

int internal_ver(const WCHAR *args, NODE *node)
{
    STR v;
    str_init(&v);
    version_string(&v);
    out_fmt(L"\r\n%ls\r\n", str_get(&v));
    str_free(&v);
    set_errorlevel(0);
    return 0;
}

int internal_cls(const WCHAR *args, NODE *node)
{
    HANDLE h = g_io.h[1];
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!g_io.con[1] || !GetConsoleScreenBufferInfo(h, &info))
        return 0;
    DWORD written;
    COORD home = {0, 0};
    DWORD cells = (DWORD)info.dwSize.X * (DWORD)info.dwSize.Y;
    FillConsoleOutputCharacterW(h, L' ', cells, home, &written);
    FillConsoleOutputAttribute(h, info.wAttributes, cells, home, &written);
    SetConsoleCursorPosition(h, home);
    return 0;
}

void set_console_color(WORD attr)
{
    HANDLE h = g_io.h[1];
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!g_io.con[1] || !GetConsoleScreenBufferInfo(h, &info))
        return;
    DWORD written;
    COORD home = {0, 0};
    DWORD cells = (DWORD)info.dwSize.X * (DWORD)info.dwSize.Y;
    FillConsoleOutputAttribute(h, attr, cells, home, &written);
    SetConsoleTextAttribute(h, attr);
}

int internal_color(const WCHAR *args, NODE *node)
{
    const WCHAR *p = skip_space(args);
    WORD attr;
    if (!*p) {
        attr = g.default_color;
        if (!attr) {
            CONSOLE_SCREEN_BUFFER_INFO info;
            if (g_io.con[1] && GetConsoleScreenBufferInfo(g_io.h[1], &info))
                attr = info.wAttributes;
            else
                attr = 7;
        }
    } else {
        int digits = 0;
        int v[2] = {};
        const WCHAR *q = p;
        while (digits < 3 && *q && !is_space(*q)) {
            int d;
            if (is_digit(*q))
                d = *q - L'0';
            else if ((*q | 0x20) >= L'a' && (*q | 0x20) <= L'f')
                d = (*q | 0x20) - L'a' + 10;
            else
                break;
            if (digits < 2)
                v[digits] = d;
            digits++;
            q++;
        }
        if (digits == 0 || digits > 2 || (*q && !is_space(*q))) {
            set_errorlevel(1);
            return 1;
        }
        attr = digits == 2 ? (WORD)(v[0] << 4 | v[1]) : (WORD)v[0];
        if ((attr >> 4) == (attr & 15)) {
            set_errorlevel(1);
            return 1;
        }
    }
    set_console_color(attr);
    return 0;
}

int internal_verify(const WCHAR *args, NODE *node)
{
    const WCHAR *p = skip_space(args);
    set_errorlevel(0);
    if (!*p) {
        out_fmt(L"VERIFY is %ls.\r\n", g.verify ? L"on" : L"off");
        return 0;
    }
    if (!_wcsnicmp(p, L"on", 2) && !*skip_space(p + 2)) {
        g.verify = TRUE;
        return 0;
    }
    if (!_wcsnicmp(p, L"off", 3) && !*skip_space(p + 3)) {
        g.verify = FALSE;
        return 0;
    }
    err_s(L"Must specify ON or OFF.\r\n");
    return 1;
}

int internal_path(const WCHAR *args, NODE *node)
{
    const WCHAR *p = skip_space(args);
    if (*p == L'=')
        p = skip_space(p + 1);
    if (!*p) {
        STR v;
        str_init(&v);
        if (env_get(L"PATH", &v))
            out_fmt(L"PATH=%ls\r\n", str_get(&v));
        else
            out_s(L"No Path set.\r\n");
        str_free(&v);
        return 0;
    }
    if (!wcscmp(p, L";"))
        env_set(L"PATH", nullptr);
    else
        env_set(L"PATH", p);
    return 0;
}

int internal_prompt(const WCHAR *args, NODE *node)
{
    const WCHAR *p = skip_space(args);
    if (*p == L'=')
        p = skip_space(p + 1);
    env_set(L"PROMPT", *p ? p : nullptr);
    return 0;
}

int internal_date(const WCHAR *args, NODE *node)
{
    const WCHAR *p = skip_space(args);
    STR d;
    str_init(&d);
    prompt_date(&d);
    if (g.ext && !_wcsnicmp(p, L"/t", 2)) {
        out_fmt(L"%ls\r\n", str_get(&d));
        str_free(&d);
        return 0;
    }
    out_fmt(L"The current date is: %ls\r\n", str_get(&d));
    str_free(&d);
    if (*p) {
        err_s(L"A required privilege is not held by the client.\r\n");
        return 1;
    }
    out_s(L"Enter the new date: (mm-dd-yy) ");
    STR line;
    str_init(&line);
    read_stdin_line(&line, FALSE);
    BOOL entered = line.len != 0;
    str_free(&line);
    if (entered) {
        err_s(L"A required privilege is not held by the client.\r\n");
        return 1;
    }
    return 0;
}

int internal_time(const WCHAR *args, NODE *node)
{
    const WCHAR *p = skip_space(args);
    SYSTEMTIME st;
    GetLocalTime(&st);
    if (g.ext && !_wcsnicmp(p, L"/t", 2)) {
        WCHAR t[64];
        if (!GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, t, NUM(t)))
            _snwprintf(t, NUM(t), L"%02u:%02u", st.wHour, st.wMinute);
        out_fmt(L"%ls\r\n", t);
        return 0;
    }
    out_fmt(L"The current time is: %2u:%02u:%02u.%02u\r\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds / 10);
    if (*p) {
        err_s(L"A required privilege is not held by the client.\r\n");
        return 1;
    }
    out_s(L"Enter the new time: ");
    STR line;
    str_init(&line);
    read_stdin_line(&line, FALSE);
    BOOL entered = line.len != 0;
    str_free(&line);
    if (entered) {
        err_s(L"A required privilege is not held by the client.\r\n");
        return 1;
    }
    return 0;
}
