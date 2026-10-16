/*
 * threads, child processes and the console functions of conio.h.
 */
#include "msvcrt_private.h"

/* ------------------------------------------------------------------ threads */

typedef unsigned(__stdcall *THREADEX_START)(void *);
typedef void(__cdecl *THREAD_START)(void *);

typedef struct {
    void *start;
    void *arg;
    int ex;
    HANDLE handle;
} THREAD_ARGS;

static DWORD WINAPI thread_entry(LPVOID param)
{
    THREAD_ARGS args = *(THREAD_ARGS *)param;
    free(param);
    MSVCRT_THREAD_DATA *ptd = MsvcrtThreadData();
    ptd->thread_handle = args.handle;
    ptd->beginthread = !args.ex;
    if (args.ex) {
        unsigned code = ((THREADEX_START)args.start)(args.arg);
        _endthreadex(code);
    } else {
        ((THREAD_START)args.start)(args.arg);
        _endthread();
    }
    return 0;
}

uintptr_t __cdecl _beginthreadex(void *security, unsigned stack, THREADEX_START start, void *arg, unsigned flags,
                                 unsigned *id)
{
    if (!start) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    THREAD_ARGS *args = malloc(sizeof(*args));
    if (!args)
        return 0;
    args->start = (void *)start;
    args->arg = arg;
    args->ex = 1;
    args->handle = nullptr;
    DWORD tid;
    HANDLE h = CreateThread(security, stack, thread_entry, args, flags, &tid);
    if (!h) {
        MsvcrtDosMapError(GetLastError());
        free(args);
        return 0;
    }
    if (id)
        *id = tid;
    return (uintptr_t)h;
}

void __cdecl _endthreadex(unsigned code)
{
    MsvcrtFreeThreadData();
    ExitThread(code);
}

/* _beginthread's thread owns its handle and closes it in _endthread. */
uintptr_t __cdecl _beginthread(THREAD_START start, unsigned stack, void *arg)
{
    if (!start) {
        MSVCRT_INVALID(EINVAL);
        return (uintptr_t)-1;
    }
    THREAD_ARGS *args = malloc(sizeof(*args));
    if (!args) {
        MsvcrtSetErrno(EAGAIN);
        return (uintptr_t)-1;
    }
    args->start = (void *)start;
    args->arg = arg;
    args->ex = 0;
    DWORD tid;
    HANDLE h = CreateThread(nullptr, stack, thread_entry, args, CREATE_SUSPENDED, &tid);
    if (!h) {
        DWORD err = GetLastError();
        free(args);
        MsvcrtDosMapError(err);
        if (err == ERROR_NOT_ENOUGH_MEMORY)
            MsvcrtSetErrno(EAGAIN);
        return (uintptr_t)-1;
    }
    args->handle = h;
    if (ResumeThread(h) == (DWORD)-1) {
        MsvcrtDosMapError(GetLastError());
        return (uintptr_t)-1;
    }
    return (uintptr_t)h;
}

void __cdecl _endthread(void)
{
    MSVCRT_THREAD_DATA *ptd = MsvcrtThreadData();
    HANDLE h = ptd->beginthread ? ptd->thread_handle : nullptr;
    MsvcrtFreeThreadData();
    if (h)
        CloseHandle(h);
    ExitThread(0);
}

unsigned long __cdecl __threadid(void)
{
    return GetCurrentThreadId();
}

uintptr_t __cdecl __threadhandle(void)
{
    return (uintptr_t)GetCurrentThread();
}

int __cdecl _getpid(void)
{
    return (int)GetCurrentProcessId();
}

/* ------------------------------------------------------------------ child processes */

#define _P_WAIT 0
#define _P_NOWAIT 1
#define _P_OVERLAY 2
#define _P_NOWAITO 3
#define _P_DETACH 4

#define _WAIT_CHILD 0
#define _WAIT_GRANDCHILD 1

/* the descriptor table passed to a child c runtime through lpReserved2. */
static unsigned char *inherit_block(WORD *size)
{
    int count = MsvcrtNhandle;
    while (count > 0 && !(MsvcrtFd(count - 1)->osfile & FOPEN))
        count--;
    size_t bytes = sizeof(int) + (size_t)count * (1 + sizeof(intptr_t));
    if (bytes > 0xffff) {
        count = (int)((0xffff - sizeof(int)) / (1 + sizeof(intptr_t)));
        bytes = sizeof(int) + (size_t)count * (1 + sizeof(intptr_t));
    }
    unsigned char *block = calloc(1, bytes);
    if (!block)
        return nullptr;
    memcpy(block, &count, sizeof(int));
    unsigned char *flags = block + sizeof(int);
    unsigned char *handles = flags + count;
    for (int i = 0; i < count; i++) {
        MSVCRT_IOINFO *io = MsvcrtFd(i);
        intptr_t h = -1;
        if ((io->osfile & FOPEN) && !(io->osfile & FNOINHERIT)) {
            flags[i] = io->osfile;
            h = io->osfhnd;
        }
        memcpy(handles + i * sizeof(intptr_t), &h, sizeof(h));
    }
    *size = (WORD)bytes;
    return block;
}

/* joins the arguments with spaces, as msvcrt does, without quoting. */
static wchar_t *join_args(const wchar_t *const *argv)
{
    size_t len = 1;
    for (int i = 0; argv && argv[i]; i++)
        len += wcslen(argv[i]) + 1;
    wchar_t *cmd = malloc(len * sizeof(wchar_t));
    if (!cmd) {
        MsvcrtSetErrno(ENOMEM);
        return nullptr;
    }
    cmd[0] = 0;
    for (int i = 0; argv && argv[i]; i++) {
        if (i)
            wcscat(cmd, L" ");
        wcscat(cmd, argv[i]);
    }
    return cmd;
}

/* the environment block of envp, or NULL to inherit; *ok is 0 when out of memory. */
static wchar_t *env_block(const wchar_t *const *envp, int *ok)
{
    *ok = 1;
    if (!envp)
        return nullptr;
    size_t len = 1;
    for (int i = 0; envp[i]; i++)
        len += wcslen(envp[i]) + 1;
    /* the current directory variables like "=C:=C:\dir" are kept for the child. */
    wchar_t *current = GetEnvironmentStringsW();
    size_t extra = 0;
    for (const wchar_t *p = current; p && *p; p += wcslen(p) + 1)
        if (*p == '=')
            extra += wcslen(p) + 1;
    wchar_t *block = malloc((len + extra) * sizeof(wchar_t));
    if (!block) {
        if (current)
            FreeEnvironmentStringsW(current);
        MsvcrtSetErrno(ENOMEM);
        *ok = 0;
        return nullptr;
    }
    wchar_t *q = block;
    for (const wchar_t *p = current; p && *p; p += wcslen(p) + 1)
        if (*p == '=') {
            size_t n = wcslen(p) + 1;
            memcpy(q, p, n * sizeof(wchar_t));
            q += n;
        }
    if (current)
        FreeEnvironmentStringsW(current);
    for (int i = 0; envp[i]; i++) {
        size_t n = wcslen(envp[i]) + 1;
        memcpy(q, envp[i], n * sizeof(wchar_t));
        q += n;
    }
    *q = 0;
    if (q == block)
        q[1] = 0;
    return block;
}

static intptr_t create_child(int mode, const wchar_t *path, const wchar_t *const *argv, const wchar_t *const *envp)
{
    if (mode < _P_WAIT || mode > _P_DETACH) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    wchar_t *cmd = join_args(argv);
    if (!cmd)
        return -1;
    int ok;
    wchar_t *env = env_block(envp, &ok);
    if (!ok) {
        free(cmd);
        return -1;
    }
    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    WORD reserved_size = 0;
    unsigned char *reserved = inherit_block(&reserved_size);
    si.cbReserved2 = reserved_size;
    si.lpReserved2 = reserved;
    /* the standard handles of the child are those of descriptors 0 to 2. */
    static const DWORD std_ids[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    HANDLE std_handles[3];
    for (int i = 0; i < 3; i++) {
        MSVCRT_IOINFO *io = MsvcrtFd(i);
        std_handles[i] = (io->osfile & FOPEN) && io->osfhnd != -1 && io->osfhnd != -2 ? (HANDLE)io->osfhnd
                                                                                      : GetStdHandle(std_ids[i]);
    }
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = std_handles[0];
    si.hStdOutput = std_handles[1];
    si.hStdError = std_handles[2];
    PROCESS_INFORMATION pi;
    DWORD flags = CREATE_UNICODE_ENVIRONMENT;
    if (mode == _P_DETACH)
        flags |= DETACHED_PROCESS;
    BOOL created = CreateProcessW(path, cmd, nullptr, nullptr, TRUE, flags, env, nullptr, &si, &pi);
    DWORD err = GetLastError();
    free(cmd);
    free(env);
    free(reserved);
    if (!created) {
        MsvcrtDosMapError(err);
        return -1;
    }
    CloseHandle(pi.hThread);
    switch (mode) {
    case _P_OVERLAY:
        CloseHandle(pi.hProcess);
        _exit(0);
    case _P_WAIT: {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        return (intptr_t)(int)code;
    }
    case _P_DETACH:
        CloseHandle(pi.hProcess);
        return 0;
    default:
        return (intptr_t)pi.hProcess;
    }
}

static int file_exists(const wchar_t *path)
{
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

/* tries path as given, then with .com, .exe, .bat and .cmd when it has no extension. */
static intptr_t spawn_resolved(int mode, const wchar_t *path, const wchar_t *const *argv,
                               const wchar_t *const *envp)
{
    const wchar_t *name = path;
    for (const wchar_t *p = path; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            name = p + 1;
    if (wcschr(name, '.')) {
        if (_waccess(path, 0))
            return -1;
        return create_child(mode, path, argv, envp);
    }
    static const wchar_t *const exts[] = {L".com", L".exe", L".bat", L".cmd"};
    size_t len = wcslen(path);
    wchar_t *buf = malloc((len + 5) * sizeof(wchar_t));
    if (!buf) {
        MsvcrtSetErrno(ENOMEM);
        return -1;
    }
    for (int i = 0; i < 4; i++) {
        wcscpy(buf, path);
        wcscat(buf, exts[i]);
        if (file_exists(buf)) {
            intptr_t r = create_child(mode, buf, argv, envp);
            free(buf);
            return r;
        }
    }
    free(buf);
    MsvcrtSetErrno(ENOENT);
    *__doserrno() = ERROR_FILE_NOT_FOUND;
    return -1;
}

static intptr_t spawn_search(int mode, const wchar_t *file, const wchar_t *const *argv, const wchar_t *const *envp)
{
    intptr_t r = spawn_resolved(mode, file, argv, envp);
    if (r != -1 || *_errno() != ENOENT)
        return r;
    /* only a bare name is looked up in PATH. */
    if (wcschr(file, '\\') || wcschr(file, '/') || (file[0] && file[1] == ':'))
        return -1;
    const wchar_t *path = _wgetenv(L"PATH");
    if (!path)
        return -1;
    size_t flen = wcslen(file);
    wchar_t *buf = malloc((wcslen(path) + flen + 2) * sizeof(wchar_t));
    if (!buf) {
        MsvcrtSetErrno(ENOMEM);
        return -1;
    }
    while (*path) {
        const wchar_t *end = path;
        while (*end && *end != ';')
            end++;
        size_t dlen = end - path;
        if (dlen) {
            memcpy(buf, path, dlen * sizeof(wchar_t));
            if (buf[dlen - 1] != '\\' && buf[dlen - 1] != '/')
                buf[dlen++] = '\\';
            memcpy(buf + dlen, file, (flen + 1) * sizeof(wchar_t));
            r = spawn_resolved(mode, buf, argv, envp);
            if (r != -1 || *_errno() != ENOENT) {
                free(buf);
                return r;
            }
        }
        path = *end ? end + 1 : end;
    }
    free(buf);
    return -1;
}

static int check_spawn(const void *path, const void *argv)
{
    if (!path || !argv || !*(const void *const *)argv || !**(const char *const *)argv) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    return 1;
}

intptr_t __cdecl _wspawnve(int mode, const wchar_t *path, const wchar_t *const *argv, const wchar_t *const *envp)
{
    if (!check_spawn(path, argv) || !*path)
        return -1;
    return spawn_resolved(mode, path, argv, envp);
}

intptr_t __cdecl _wspawnv(int mode, const wchar_t *path, const wchar_t *const *argv)
{
    return _wspawnve(mode, path, argv, nullptr);
}

intptr_t __cdecl _wspawnvpe(int mode, const wchar_t *file, const wchar_t *const *argv, const wchar_t *const *envp)
{
    if (!check_spawn(file, argv) || !*file)
        return -1;
    return spawn_search(mode, file, argv, envp);
}

intptr_t __cdecl _wspawnvp(int mode, const wchar_t *file, const wchar_t *const *argv)
{
    return _wspawnvpe(mode, file, argv, nullptr);
}

/* converts a narrow argument vector; NULL in, NULL out. */
static wchar_t **widen_vector(const char *const *v, int *ok)
{
    *ok = 1;
    if (!v)
        return nullptr;
    int n = 0;
    while (v[n])
        n++;
    wchar_t **w = calloc(n + 1, sizeof(wchar_t *));
    if (!w) {
        *ok = 0;
        return nullptr;
    }
    for (int i = 0; i < n; i++)
        if (!(w[i] = MsvcrtAToW(v[i])))
            *ok = 0;
    return w;
}

static void free_vector(wchar_t **v)
{
    for (int i = 0; v && v[i]; i++)
        free(v[i]);
    free(v);
}

static intptr_t spawn_a(int mode, const char *path, const char *const *argv, const char *const *envp, int search)
{
    if (!check_spawn(path, argv) || !*path)
        return -1;
    int ok1, ok2;
    wchar_t *wpath = MsvcrtAToW(path);
    wchar_t **wargv = widen_vector(argv, &ok1);
    wchar_t **wenvp = widen_vector(envp, &ok2);
    intptr_t r = -1;
    if (wpath && ok1 && ok2)
        r = search ? spawn_search(mode, wpath, (const wchar_t *const *)wargv, (const wchar_t *const *)wenvp)
                   : spawn_resolved(mode, wpath, (const wchar_t *const *)wargv, (const wchar_t *const *)wenvp);
    else
        MsvcrtSetErrno(ENOMEM);
    free(wpath);
    free_vector(wargv);
    free_vector(wenvp);
    return r;
}

intptr_t __cdecl _spawnve(int mode, const char *path, const char *const *argv, const char *const *envp)
{
    return spawn_a(mode, path, argv, envp, 0);
}

intptr_t __cdecl _spawnv(int mode, const char *path, const char *const *argv)
{
    return spawn_a(mode, path, argv, nullptr, 0);
}

intptr_t __cdecl _spawnvpe(int mode, const char *file, const char *const *argv, const char *const *envp)
{
    return spawn_a(mode, file, argv, envp, 1);
}

intptr_t __cdecl _spawnvp(int mode, const char *file, const char *const *argv)
{
    return spawn_a(mode, file, argv, nullptr, 1);
}

/* the variadic forms collect their arguments into a vector. */
#define MAX_SPAWN_ARGS 1024

#define SPAWNL(NAME, T, CALL, HAS_ENV, SEARCH)                                                                    \
    intptr_t __cdecl NAME(int mode, const T *path, const T *arg0, ...)                                            \
    {                                                                                                             \
        const T *argv[MAX_SPAWN_ARGS + 1];                                                                        \
        va_list ap;                                                                                               \
        int n = 0;                                                                                                \
        argv[n++] = arg0;                                                                                         \
        va_start(ap, arg0);                                                                                       \
        while (arg0 && n < MAX_SPAWN_ARGS && (argv[n] = va_arg(ap, const T *)))                                   \
            n++;                                                                                                  \
        argv[n] = nullptr;                                                                                           \
        const T *const *envp = HAS_ENV ? va_arg(ap, const T *const *) : nullptr;                                     \
        va_end(ap);                                                                                               \
        return CALL(mode, path, argv, envp, SEARCH);                                                              \
    }

static intptr_t spawn_w(int mode, const wchar_t *path, const wchar_t *const *argv, const wchar_t *const *envp,
                        int search)
{
    return search ? _wspawnvpe(mode, path, argv, envp) : _wspawnve(mode, path, argv, envp);
}

SPAWNL(_spawnl, char, spawn_a, 0, 0)
SPAWNL(_spawnle, char, spawn_a, 1, 0)
SPAWNL(_spawnlp, char, spawn_a, 0, 1)
SPAWNL(_spawnlpe, char, spawn_a, 1, 1)
SPAWNL(_wspawnl, wchar_t, spawn_w, 0, 0)
SPAWNL(_wspawnle, wchar_t, spawn_w, 1, 0)
SPAWNL(_wspawnlp, wchar_t, spawn_w, 0, 1)
SPAWNL(_wspawnlpe, wchar_t, spawn_w, 1, 1)

intptr_t __cdecl _cwait(int *status, intptr_t process, int action)
{
    if (WaitForSingleObject((HANDLE)process, INFINITE) == WAIT_FAILED) {
        DWORD err = GetLastError();
        MsvcrtDosMapError(err);
        if (err == ERROR_INVALID_HANDLE)
            MsvcrtSetErrno(ECHILD);
        return -1;
    }
    DWORD code = 0;
    if (!GetExitCodeProcess((HANDLE)process, &code)) {
        MsvcrtDosMapError(GetLastError());
        return -1;
    }
    if (status)
        *status = (int)code;
    CloseHandle((HANDLE)process);
    return process;
}

static const wchar_t *comspec(void)
{
    const wchar_t *c = _wgetenv(L"COMSPEC");
    return c ? c : L"cmd.exe";
}

int __cdecl _wsystem(const wchar_t *command)
{
    const wchar_t *shell = comspec();
    if (!command)
        return _waccess(shell, 0) == 0 || GetFileAttributesW(L"cmd.exe") != INVALID_FILE_ATTRIBUTES;
    const wchar_t *argv[] = {shell, L"/c", command, nullptr};
    intptr_t r = _wspawnve(_P_WAIT, shell, argv, nullptr);
    if (r == -1 && (*_errno() == ENOENT || *_errno() == EACCES))
        r = _wspawnvpe(_P_WAIT, L"cmd.exe", argv, nullptr);
    return (int)r;
}

int __cdecl system(const char *command)
{
    if (!command)
        return _wsystem(nullptr);
    wchar_t *w = MsvcrtAToW(command);
    if (!w)
        return -1;
    int r = _wsystem(w);
    free(w);
    return r;
}

/* ------------------------------------------------------------------ console */

#define KEY_EVENT_TYPE 0x0001

static HANDLE g_conin = INVALID_HANDLE_VALUE;
static HANDLE g_conout = INVALID_HANDLE_VALUE;
static int g_ungetch = EOF;
static wint_t g_ungetwch = WEOF;

static HANDLE conin(void)
{
    if (g_conin == INVALID_HANDLE_VALUE)
        g_conin = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    return g_conin;
}

static HANDLE conout(void)
{
    if (g_conout == INVALID_HANDLE_VALUE)
        g_conout = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                               nullptr);
    return g_conout;
}

void MsvcrtConsoleTerm(void)
{
    if (g_conin != INVALID_HANDLE_VALUE)
        CloseHandle(g_conin);
    if (g_conout != INVALID_HANDLE_VALUE)
        CloseHandle(g_conout);
    g_conin = g_conout = INVALID_HANDLE_VALUE;
}

int __cdecl _putch(int c)
{
    _lock(_CONIO_LOCK);
    char ch = (char)c;
    DWORD written;
    int r = WriteConsoleA(conout(), &ch, 1, &written, nullptr) ? (unsigned char)ch : EOF;
    _unlock(_CONIO_LOCK);
    return r;
}

wint_t __cdecl _putwch(wchar_t c)
{
    _lock(_CONIO_LOCK);
    DWORD written;
    wint_t r = WriteConsoleW(conout(), &c, 1, &written, nullptr) ? c : WEOF;
    _unlock(_CONIO_LOCK);
    return r;
}

int __cdecl _cputs(const char *s)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    _lock(_CONIO_LOCK);
    DWORD written;
    int r = WriteConsoleA(conout(), s, (DWORD)strlen(s), &written, nullptr) ? 0 : -1;
    _unlock(_CONIO_LOCK);
    return r;
}

int __cdecl _cputws(const wchar_t *s)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    _lock(_CONIO_LOCK);
    DWORD written;
    int r = WriteConsoleW(conout(), s, (DWORD)wcslen(s), &written, nullptr) ? 0 : -1;
    _unlock(_CONIO_LOCK);
    return r;
}

/* waits for a key press and returns its character, extended keys as 0 then the scan code. */
static int g_extended = EOF;

static int read_key(void)
{
    if (g_extended != EOF) {
        int c = g_extended;
        g_extended = EOF;
        return c;
    }
    HANDLE h = conin();
    if (h == INVALID_HANDLE_VALUE)
        return EOF;
    DWORD mode = 0;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, 0);
    int result = EOF;
    for (;;) {
        INPUT_RECORD rec;
        DWORD got = 0;
        if (!ReadConsoleInputA(h, &rec, 1, &got) || !got)
            break;
        if (rec.EventType != KEY_EVENT_TYPE || !rec.Event.KeyEvent.bKeyDown)
            continue;
        unsigned char ch = (unsigned char)rec.Event.KeyEvent.uChar.AsciiChar;
        if (ch) {
            result = ch;
            break;
        }
        WORD vk = rec.Event.KeyEvent.wVirtualKeyCode;
        if (vk == 0x10 || vk == 0x11 || vk == 0x12 || vk == 0x14)
            continue;
        g_extended = rec.Event.KeyEvent.wVirtualScanCode;
        result = 0;
        break;
    }
    SetConsoleMode(h, mode);
    return result;
}

int __cdecl _getch(void)
{
    _lock(_CONIO_LOCK);
    int c;
    if (g_ungetch != EOF) {
        c = g_ungetch;
        g_ungetch = EOF;
    } else {
        c = read_key();
    }
    _unlock(_CONIO_LOCK);
    return c;
}

int __cdecl _getche(void)
{
    int c = _getch();
    if (c != EOF && c)
        _putch(c);
    return c;
}

wint_t __cdecl _getwch(void)
{
    _lock(_CONIO_LOCK);
    wint_t c;
    if (g_ungetwch != WEOF) {
        c = g_ungetwch;
        g_ungetwch = WEOF;
    } else {
        int a = read_key();
        c = a == EOF ? WEOF : (wint_t)a;
    }
    _unlock(_CONIO_LOCK);
    return c;
}

wint_t __cdecl _getwche(void)
{
    wint_t c = _getwch();
    if (c != WEOF && c)
        _putwch(c);
    return c;
}

int __cdecl _ungetch(int c)
{
    if (c == EOF || g_ungetch != EOF)
        return EOF;
    g_ungetch = c & 0xff;
    return g_ungetch;
}

wint_t __cdecl _ungetwch(wint_t c)
{
    if (c == WEOF || g_ungetwch != WEOF)
        return WEOF;
    g_ungetwch = c;
    return c;
}

int __cdecl _kbhit(void)
{
    if (g_ungetch != EOF || g_extended != EOF)
        return 1;
    HANDLE h = conin();
    DWORD count = 0;
    if (h == INVALID_HANDLE_VALUE || !GetNumberOfConsoleInputEvents(h, &count) || !count)
        return 0;
    INPUT_RECORD *recs = malloc(count * sizeof(INPUT_RECORD));
    if (!recs)
        return 0;
    DWORD got = 0;
    int hit = 0;
    if (PeekConsoleInputA(h, recs, count, &got)) {
        for (DWORD i = 0; i < got && !hit; i++)
            if (recs[i].EventType == KEY_EVENT_TYPE && recs[i].Event.KeyEvent.bKeyDown &&
                recs[i].Event.KeyEvent.uChar.AsciiChar)
                hit = 1;
    }
    free(recs);
    return hit;
}

char *__cdecl _cgets(char *buf)
{
    if (!buf) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    unsigned max = (unsigned char)buf[0];
    char *text = buf + 2;
    unsigned n = 0;
    _lock(_CONIO_LOCK);
    for (;;) {
        int c = _getch();
        if (c == EOF || c == '\r')
            break;
        if (c == '\b') {
            if (n) {
                n--;
                _cputs("\b \b");
            }
            continue;
        }
        if (n + 1 >= max)
            continue;
        text[n++] = (char)c;
        _putch(c);
    }
    text[n] = 0;
    buf[1] = (char)n;
    _unlock(_CONIO_LOCK);
    return text;
}

int __cdecl _vcprintf(const char *fmt, va_list ap)
{
    char small[512];
    va_list copy;
    va_copy(copy, ap);
    int n = MsvcrtVsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (n < 0)
        return -1;
    char *buf = (size_t)n < sizeof(small) ? small : malloc(n + 1);
    if (!buf)
        return -1;
    MsvcrtVsnprintf(buf, n + 1, fmt, ap);
    buf[n] = 0;
    int r = _cputs(buf) ? -1 : n;
    if (buf != small)
        free(buf);
    return r;
}

int __cdecl _cprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = _vcprintf(fmt, ap);
    va_end(ap);
    return r;
}

int __cdecl _vcwprintf(const wchar_t *fmt, va_list ap)
{
    va_list copy;
    va_copy(copy, ap);
    int n = MsvcrtVsnwprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (n < 0)
        return -1;
    wchar_t *buf = malloc((n + 1) * sizeof(wchar_t));
    if (!buf)
        return -1;
    MsvcrtVsnwprintf(buf, n + 1, fmt, ap);
    buf[n] = 0;
    int r = _cputws(buf) ? -1 : n;
    free(buf);
    return r;
}

int __cdecl _cwprintf(const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = _vcwprintf(fmt, ap);
    va_end(ap);
    return r;
}

/* ------------------------------------------------------------------ exec */

/* the exec functions run the program and end this process, like _P_OVERLAY. */
intptr_t __cdecl _wexecve(const wchar_t *path, const wchar_t *const *argv, const wchar_t *const *envp)
{
    return _wspawnve(_P_OVERLAY, path, argv, envp);
}

intptr_t __cdecl _wexecv(const wchar_t *path, const wchar_t *const *argv)
{
    return _wspawnve(_P_OVERLAY, path, argv, nullptr);
}

intptr_t __cdecl _wexecvpe(const wchar_t *file, const wchar_t *const *argv, const wchar_t *const *envp)
{
    return _wspawnvpe(_P_OVERLAY, file, argv, envp);
}

intptr_t __cdecl _wexecvp(const wchar_t *file, const wchar_t *const *argv)
{
    return _wspawnvpe(_P_OVERLAY, file, argv, nullptr);
}

intptr_t __cdecl _execve(const char *path, const char *const *argv, const char *const *envp)
{
    return spawn_a(_P_OVERLAY, path, argv, envp, 0);
}

intptr_t __cdecl _execv(const char *path, const char *const *argv)
{
    return spawn_a(_P_OVERLAY, path, argv, nullptr, 0);
}

intptr_t __cdecl _execvpe(const char *file, const char *const *argv, const char *const *envp)
{
    return spawn_a(_P_OVERLAY, file, argv, envp, 1);
}

intptr_t __cdecl _execvp(const char *file, const char *const *argv)
{
    return spawn_a(_P_OVERLAY, file, argv, nullptr, 1);
}

static intptr_t exec_a(int mode, const char *path, const char *const *argv, const char *const *envp, int search)
{
    return spawn_a(_P_OVERLAY, path, argv, envp, search);
}

static intptr_t exec_w(int mode, const wchar_t *path, const wchar_t *const *argv, const wchar_t *const *envp,
                       int search)
{
    return spawn_w(_P_OVERLAY, path, argv, envp, search);
}

#define EXECL(NAME, T, CALL, HAS_ENV, SEARCH)                                                                     \
    intptr_t __cdecl NAME(const T *path, const T *arg0, ...)                                                      \
    {                                                                                                             \
        const T *argv[MAX_SPAWN_ARGS + 1];                                                                        \
        va_list ap;                                                                                               \
        int n = 0;                                                                                                \
        argv[n++] = arg0;                                                                                         \
        va_start(ap, arg0);                                                                                       \
        while (arg0 && n < MAX_SPAWN_ARGS && (argv[n] = va_arg(ap, const T *)))                                   \
            n++;                                                                                                  \
        argv[n] = nullptr;                                                                                           \
        const T *const *envp = HAS_ENV ? va_arg(ap, const T *const *) : nullptr;                                     \
        va_end(ap);                                                                                               \
        return CALL(_P_OVERLAY, path, argv, envp, SEARCH);                                                        \
    }

EXECL(_execl, char, exec_a, 0, 0)
EXECL(_execle, char, exec_a, 1, 0)
EXECL(_execlp, char, exec_a, 0, 1)
EXECL(_execlpe, char, exec_a, 1, 1)
EXECL(_wexecl, wchar_t, exec_w, 0, 0)
EXECL(_wexecle, wchar_t, exec_w, 1, 0)
EXECL(_wexeclp, wchar_t, exec_w, 0, 1)
EXECL(_wexeclpe, wchar_t, exec_w, 1, 1)

/* ------------------------------------------------------------------ pipes to commands */

typedef struct {
    FILE *stream;
    intptr_t process;
} POPEN_ENTRY;

static POPEN_ENTRY *g_popen;
static int g_popen_count;

/* runs the command interpreter with one end of a pipe as its standard input or output. */
static FILE *popen_w(const wchar_t *command, const wchar_t *mode)
{
    if (!command || !mode) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    int reading = 0, text = _O_TEXT;
    for (const wchar_t *m = mode; *m; m++) {
        if (*m == 'r')
            reading = 1;
        else if (*m == 'w')
            reading = 0;
        else if (*m == 'b')
            text = _O_BINARY;
        else if (*m == 't')
            text = _O_TEXT;
    }
    if (mode[0] != 'r' && mode[0] != 'w') {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    int fds[2];
    if (_pipe(fds, 1024, text | _O_NOINHERIT) < 0)
        return nullptr;
    int child_end = reading ? fds[1] : fds[0];
    int parent_end = reading ? fds[0] : fds[1];
    int std = reading ? 1 : 0;
    _lock(_POPEN_LOCK);
    int saved = _dup(std);
    intptr_t process = -1;
    if (saved >= 0 && _dup2(child_end, std) == 0) {
        const wchar_t *shell = comspec();
        const wchar_t *argv[] = {shell, L"/c", command, nullptr};
        process = _wspawnve(_P_NOWAIT, shell, argv, nullptr);
        if (process == -1)
            process = _wspawnvpe(_P_NOWAIT, L"cmd.exe", argv, nullptr);
    }
    if (saved >= 0) {
        _dup2(saved, std);
        _close(saved);
    }
    _close(child_end);
    FILE *f = nullptr;
    if (process != -1) {
        wchar_t fmode[3] = {reading ? 'r' : 'w', text == _O_BINARY ? 'b' : 't', 0};
        f = _wfdopen(parent_end, fmode);
    }
    if (f) {
        POPEN_ENTRY *grown = realloc(g_popen, (g_popen_count + 1) * sizeof(*grown));
        if (grown) {
            g_popen = grown;
            g_popen[g_popen_count].stream = f;
            g_popen[g_popen_count].process = process;
            g_popen_count++;
        }
    } else {
        _close(parent_end);
        if (process != -1)
            CloseHandle((HANDLE)process);
    }
    _unlock(_POPEN_LOCK);
    return f;
}

FILE *__cdecl _wpopen(const wchar_t *command, const wchar_t *mode)
{
    return popen_w(command, mode);
}

FILE *__cdecl _popen(const char *command, const char *mode)
{
    if (!command || !mode) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    wchar_t *wc = MsvcrtAToW(command), *wm = MsvcrtAToW(mode);
    FILE *f = wc && wm ? popen_w(wc, wm) : nullptr;
    free(wc);
    free(wm);
    return f;
}

int __cdecl _pclose(FILE *f)
{
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    _lock(_POPEN_LOCK);
    intptr_t process = -1;
    for (int i = 0; i < g_popen_count; i++) {
        if (g_popen[i].stream == f) {
            process = g_popen[i].process;
            g_popen[i] = g_popen[--g_popen_count];
            break;
        }
    }
    _unlock(_POPEN_LOCK);
    if (process == -1) {
        MsvcrtSetErrno(EBADF);
        return -1;
    }
    fclose(f);
    int status = -1;
    if (_cwait(&status, process, _WAIT_GRANDCHILD) == -1)
        return -1;
    return status;
}
