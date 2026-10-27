/* environment variables, the dynamic ones, setlocal snapshots and the per drive current directories. */
#include "cmd.h"

BOOL get_cwd(STR *out)
{
    WCHAR stack[MAX_PATH + 2];
    DWORD n = GetCurrentDirectoryW(NUM(stack), stack);
    if (n && n < NUM(stack)) {
        str_adds(out, stack);
        return TRUE;
    }
    if (n >= NUM(stack)) {
        WCHAR *big = xalloc(((SIZE_T)n + 1) * sizeof(WCHAR));
        GetCurrentDirectoryW(n + 1, big);
        str_adds(out, big);
        xfree(big);
        return TRUE;
    }
    return FALSE;
}

static ULONG random_state;

static ULONG next_random(void)
{
    if (!random_state) {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        random_state = (ULONG)t.QuadPart ^ GetCurrentProcessId() * 2654435761u;
        if (!random_state)
            random_state = 1;
    }
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

/* the variables the interpreter makes up when a script has not set them. */
BOOL env_dynamic(const WCHAR *name, STR *value)
{
    if (!g.ext)
        return FALSE;
    if (!_wcsicmp(name, L"ERRORLEVEL")) {
        str_fmt(value, L"%lu", (unsigned long)g.errorlevel);
        return TRUE;
    }
    if (!_wcsicmp(name, L"CD")) {
        STR cwd;
        str_init(&cwd);
        if (get_cwd(&cwd))
            str_adds(value, str_get(&cwd));
        str_free(&cwd);
        return TRUE;
    }
    if (!_wcsicmp(name, L"RANDOM")) {
        str_fmt(value, L"%lu", (unsigned long)(next_random() >> 8) & 0x7FFF);
        return TRUE;
    }
    if (!_wcsicmp(name, L"CMDEXTVERSION")) {
        str_adds(value, L"2");
        return TRUE;
    }
    if (!_wcsicmp(name, L"CMDCMDLINE")) {
        str_adds(value, g.cmdline ? g.cmdline : L"");
        return TRUE;
    }
    if (!_wcsicmp(name, L"HIGHESTNUMANODENUMBER")) {
        str_adds(value, L"0");
        return TRUE;
    }
    if (!_wcsicmp(name, L"DATE")) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        format_date(&st, TRUE, value);
        return TRUE;
    }
    if (!_wcsicmp(name, L"TIME")) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        str_fmt(value, L"%2u:%02u:%02u.%02u", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds / 10);
        return TRUE;
    }
    return FALSE;
}

BOOL env_get(const WCHAR *name, STR *value)
{
    if (!name[0])
        return FALSE;
    WCHAR stack[512];
    DWORD n = GetEnvironmentVariableW(name, stack, NUM(stack));
    if (n && n < NUM(stack)) {
        str_addn(value, stack, n);
        return TRUE;
    }
    if (n >= NUM(stack)) {
        WCHAR *big = xalloc(((SIZE_T)n + 1) * sizeof(WCHAR));
        DWORD got = GetEnvironmentVariableW(name, big, n + 1);
        str_addn(value, big, got);
        xfree(big);
        return TRUE;
    }
    if (GetLastError() == ERROR_ENVVAR_NOT_FOUND || !n) {
        /* a variable that is set to nothing does not exist for this interpreter either. */
    }
    return env_dynamic(name, value);
}

WCHAR *env_dup(const WCHAR *name)
{
    STR v;
    str_init(&v);
    if (!env_get(name, &v)) {
        str_free(&v);
        return nullptr;
    }
    return str_take(&v);
}

BOOL env_defined(const WCHAR *name)
{
    STR v;
    str_init(&v);
    BOOL ok = env_get(name, &v);
    str_free(&v);
    return ok;
}

BOOL env_set(const WCHAR *name, const WCHAR *value)
{
    if (value && !*value)
        value = nullptr;
    return SetEnvironmentVariableW(name, value);
}

/* sets the variables a new interpreter keeps for the current directory of the drive it runs on. */
void env_setup(void)
{
    STR cwd;
    str_init(&cwd);
    if (get_cwd(&cwd))
        drive_dir_set(str_get(&cwd));
    str_free(&cwd);
}

void drive_dir_set(const WCHAR *dir)
{
    if (dir[0] && dir[1] == L':') {
        WCHAR name[4] = {L'=', upcase(dir[0]), L':', 0};
        env_set(name, dir);
    }
}

BOOL drive_dir_get(WCHAR drive, STR *dir)
{
    WCHAR name[4] = {L'=', upcase(drive), L':', 0};
    return env_get(name, dir);
}

/* ------------------------------------------------------------------ listing */

/* by name, upper case letters first as the system orders the block. */
static int compare_lines(const void *a, const void *b)
{
    const WCHAR *x = a, *y = b;
    for (;; x++, y++) {
        WCHAR cx = *x == L'=' ? 0 : towupper(*x), cy = *y == L'=' ? 0 : towupper(*y);
        if (cx != cy)
            return cx < cy ? -1 : 1;
        if (!cx)
            return 0;
    }
}

BOOL env_print_all(const WCHAR *prefix)
{
    WCHAR *block = GetEnvironmentStringsW();
    if (!block)
        return FALSE;
    SIZE_T count = 0;
    for (WCHAR *p = block; *p; p += wcslen(p) + 1)
        count++;
    void **lines = xalloc((count + 1) * sizeof(void *));
    SIZE_T n = 0;
    SIZE_T plen = prefix ? wcslen(prefix) : 0;
    for (WCHAR *p = block; *p; p += wcslen(p) + 1) {
        if (*p == L'=')
            continue;
        if (plen && _wcsnicmp(p, prefix, plen))
            continue;
        lines[n++] = p;
    }
    /* the block is already in the order the system keeps, a plain compare of the names keeps that. */
    sort_pointers(lines, n, compare_lines);
    if (!n && plen) {
        err_fmt(L"Environment variable %ls not defined\r\n", prefix);
        xfree(lines);
        FreeEnvironmentStringsW(block);
        set_errorlevel(1);
        return FALSE;
    }
    for (SIZE_T i = 0; i < n; i++) {
        const WCHAR *line = lines[i];
        out_s(line);
        out_n(L"\r\n", 2);
    }
    xfree(lines);
    FreeEnvironmentStringsW(block);
    return TRUE;
}

/* ------------------------------------------------------------------ setlocal */

LOCALENV *env_snapshot(void)
{
    LOCALENV *snap = xalloc(sizeof(*snap));
    WCHAR *block = GetEnvironmentStringsW();
    SIZE_T chars = 1;
    if (block)
        for (WCHAR *p = block; *p; p += wcslen(p) + 1)
            chars += wcslen(p) + 1;
    snap->block = xalloc(chars * sizeof(WCHAR));
    if (block)
        memcpy(snap->block, block, chars * sizeof(WCHAR));
    snap->chars = chars;
    if (block)
        FreeEnvironmentStringsW(block);
    STR cwd;
    str_init(&cwd);
    get_cwd(&cwd);
    snap->cwd = str_take(&cwd);
    snap->ext = g.ext;
    snap->delayed = g.delayed;
    return snap;
}

void env_free_snapshot(LOCALENV *snap)
{
    if (!snap)
        return;
    xfree(snap->block);
    xfree(snap->cwd);
    xfree(snap);
}

/* the value of an entry "name=value" of a block, the name may start with an equals sign. */
static const WCHAR *block_find(const WCHAR *block, const WCHAR *name, SIZE_T name_len)
{
    for (const WCHAR *p = block; *p; p += wcslen(p) + 1) {
        if (_wcsnicmp(p, name, name_len) == 0 && p[name_len] == L'=')
            return p + name_len + 1;
    }
    return nullptr;
}

static SIZE_T entry_name_len(const WCHAR *entry)
{
    const WCHAR *eq = wcschr(entry + 1, L'=');
    return eq ? (SIZE_T)(eq - entry) : wcslen(entry);
}

void env_restore(LOCALENV *snap)
{
    WCHAR *current = GetEnvironmentStringsW();
    if (current) {
        for (WCHAR *p = current; *p; p += wcslen(p) + 1) {
            SIZE_T n = entry_name_len(p);
            if (!block_find(snap->block, p, n)) {
                WCHAR *name = xdupn(p, n);
                SetEnvironmentVariableW(name, nullptr);
                xfree(name);
            }
        }
    }
    for (const WCHAR *p = snap->block; *p; p += wcslen(p) + 1) {
        SIZE_T n = entry_name_len(p);
        const WCHAR *value = p + n + 1;
        const WCHAR *now = current ? block_find(current, p, n) : nullptr;
        if (!now || wcscmp(now, value)) {
            WCHAR *name = xdupn(p, n);
            SetEnvironmentVariableW(name, *value ? value : nullptr);
            xfree(name);
        }
    }
    if (current)
        FreeEnvironmentStringsW(current);
    g.ext = snap->ext;
    g.delayed = snap->delayed;
    if (snap->cwd && snap->cwd[0]) {
        STR now;
        str_init(&now);
        get_cwd(&now);
        if (_wcsicmp(str_get(&now), snap->cwd) && SetCurrentDirectoryW(snap->cwd))
            drive_dir_set(snap->cwd);
        str_free(&now);
    }
}
