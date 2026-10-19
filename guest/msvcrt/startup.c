/*
 * program startup and termination: command line arguments, the
 * initializer tables of the c runtime startup code, onexit and exit.
 */
#include "msvcrt_private.h"

int __argc;
char **__argv;
wchar_t **__wargv;
char *_acmdln;
wchar_t *_wcmdln;
char *_pgmptr;
wchar_t *_wpgmptr;
int _fmode;
int _commode;
unsigned int _osver;
unsigned int _winver;
unsigned int _winmajor;
unsigned int _winminor;
unsigned int _osplatform = 2;
static int g_app_type;
static char g_pgm_name[MAX_PATH];
static wchar_t g_wpgm_name[MAX_PATH];

typedef struct {
    int newmode;
} _startupinfo;

void MsvcrtStartupInit(void)
{
    _acmdln = GetCommandLineA();
    _wcmdln = GetCommandLineW();
    GetModuleFileNameA(nullptr, g_pgm_name, MAX_PATH);
    GetModuleFileNameW(nullptr, g_wpgm_name, MAX_PATH);
    _pgmptr = g_pgm_name;
    _wpgmptr = g_wpgm_name;
    OSVERSIONINFOW info = {sizeof(info)};
    if (GetVersionExW(&info)) {
        _winmajor = info.dwMajorVersion;
        _winminor = info.dwMinorVersion;
        _winver = (_winmajor << 8) | _winminor;
        _osver = info.dwBuildNumber & 0x7fff;
        _osplatform = info.dwPlatformId;
    }
}

void __cdecl __set_app_type(int type)
{
    g_app_type = type;
}

int MsvcrtAppType(void)
{
    return g_app_type;
}

/* ------------------------------------------------------------------ argument parsing */


/* the parser of cmdline.h, built for both character types. */
static void parse_a(const char *p, char **argv, char *args, int *quoted, int *argc, size_t *chars);
static void parse_w(const wchar_t *p, wchar_t **argv, wchar_t *args, int *quoted, int *argc, size_t *chars);

#define CHAR_T char
#define PARSE_NAME parse_a
#include "cmdline.h"
#undef CHAR_T
#undef PARSE_NAME
#define CHAR_T wchar_t
#define PARSE_NAME parse_w
#include "cmdline.h"
#undef CHAR_T
#undef PARSE_NAME

/* ------------------------------------------------------------------ wildcard expansion */

static int has_wild_a(const char *s)
{
    for (; *s; s++)
        if (*s == '*' || *s == '?')
            return 1;
    return 0;
}

static int has_wild_w(const wchar_t *s)
{
    for (; *s; s++)
        if (*s == '*' || *s == '?')
            return 1;
    return 0;
}

static int cmp_a(const void *a, const void *b)
{
    return _stricmp(*(char *const *)a, *(char *const *)b);
}

static int cmp_w(const void *a, const void *b)
{
    return _wcsicmp(*(wchar_t *const *)a, *(wchar_t *const *)b);
}

void __cdecl qsort(void *base, size_t count, size_t size, int(__cdecl *cmp)(const void *, const void *));

/* appends p to a growing argument vector. */
static int push_arg(void ***vec, int *n, int *cap, void *p)
{
    if (*n + 2 > *cap) {
        int nc = *cap ? *cap * 2 : 16;
        void **nv = realloc(*vec, nc * sizeof(void *));
        if (!nv)
            return 0;
        *vec = nv;
        *cap = nc;
    }
    (*vec)[(*n)++] = p;
    return 1;
}

/* replaces each unquoted argument with wildcards by its sorted matches, like _cwild. */
static int expand_a(char ***argv, int *argc, const int *quoted)
{
    void **out = nullptr;
    int n = 0, cap = 0;
    for (int i = 0; i < *argc; i++) {
        char *arg = (*argv)[i];
        if (i == 0 || quoted[i] || !has_wild_a(arg)) {
            if (!push_arg(&out, &n, &cap, _strdup(arg)))
                return -1;
            continue;
        }
        size_t prefix = 0;
        for (size_t k = 0; arg[k]; k++)
            if (arg[k] == '\\' || arg[k] == '/' || arg[k] == ':')
                prefix = k + 1;
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(arg, &fd);
        int first = n;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
                    continue;
                size_t len = strlen(fd.cFileName);
                char *s = malloc(prefix + len + 1);
                if (!s)
                    break;
                memcpy(s, arg, prefix);
                memcpy(s + prefix, fd.cFileName, len + 1);
                if (!push_arg(&out, &n, &cap, s))
                    break;
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        if (n == first) {
            if (!push_arg(&out, &n, &cap, _strdup(arg)))
                return -1;
        } else {
            qsort(out + first, n - first, sizeof(void *), cmp_a);
        }
    }
    if (!push_arg(&out, &n, &cap, nullptr))
        return -1;
    *argv = (char **)out;
    *argc = n - 1;
    return 0;
}

static int expand_w(wchar_t ***argv, int *argc, const int *quoted)
{
    void **out = nullptr;
    int n = 0, cap = 0;
    for (int i = 0; i < *argc; i++) {
        wchar_t *arg = (*argv)[i];
        if (i == 0 || quoted[i] || !has_wild_w(arg)) {
            if (!push_arg(&out, &n, &cap, _wcsdup(arg)))
                return -1;
            continue;
        }
        size_t prefix = 0;
        for (size_t k = 0; arg[k]; k++)
            if (arg[k] == '\\' || arg[k] == '/' || arg[k] == ':')
                prefix = k + 1;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(arg, &fd);
        int first = n;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
                    continue;
                size_t len = wcslen(fd.cFileName);
                wchar_t *s = malloc((prefix + len + 1) * sizeof(wchar_t));
                if (!s)
                    break;
                memcpy(s, arg, prefix * sizeof(wchar_t));
                memcpy(s + prefix, fd.cFileName, (len + 1) * sizeof(wchar_t));
                if (!push_arg(&out, &n, &cap, s))
                    break;
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        if (n == first) {
            if (!push_arg(&out, &n, &cap, _wcsdup(arg)))
                return -1;
        } else {
            qsort(out + first, n - first, sizeof(void *), cmp_w);
        }
    }
    if (!push_arg(&out, &n, &cap, nullptr))
        return -1;
    *argv = (wchar_t **)out;
    *argc = n - 1;
    return 0;
}

static int g_args_done;
static int g_wargs_done;

static int setargv(int wildcard)
{
    if (g_args_done)
        return 0;
    const char *cmd = (_acmdln && *_acmdln) ? _acmdln : _pgmptr;
    int argc;
    size_t chars;
    parse_a(cmd, nullptr, nullptr, nullptr, &argc, &chars);
    char **argv = malloc((argc + 1) * sizeof(char *) + chars);
    int *quoted = malloc((argc + 1) * sizeof(int));
    if (!argv || !quoted) {
        free(argv);
        free(quoted);
        return -1;
    }
    parse_a(cmd, argv, (char *)(argv + argc + 1), quoted, &argc, &chars);
    if (wildcard && expand_a(&argv, &argc, quoted) < 0) {
        free(quoted);
        return -1;
    }
    free(quoted);
    __argv = argv;
    __argc = argc;
    g_args_done = 1;
    return 0;
}

static int wsetargv(int wildcard)
{
    if (g_wargs_done)
        return 0;
    const wchar_t *cmd = (_wcmdln && *_wcmdln) ? _wcmdln : _wpgmptr;
    int argc;
    size_t chars;
    parse_w(cmd, nullptr, nullptr, nullptr, &argc, &chars);
    wchar_t **argv = malloc((argc + 1) * sizeof(wchar_t *) + chars * sizeof(wchar_t));
    int *quoted = malloc((argc + 1) * sizeof(int));
    if (!argv || !quoted) {
        free(argv);
        free(quoted);
        return -1;
    }
    parse_w(cmd, argv, (wchar_t *)(argv + argc + 1), quoted, &argc, &chars);
    if (wildcard && expand_w(&argv, &argc, quoted) < 0) {
        free(quoted);
        return -1;
    }
    free(quoted);
    __wargv = argv;
    __argc = argc;
    g_wargs_done = 1;
    return 0;
}

int __cdecl __getmainargs(int *argc, char ***argv, char ***envp, int wildcard, _startupinfo *info)
{
    if (info)
        MsvcrtSetNewMode(info->newmode);
    if (setargv(wildcard) < 0)
        ExitProcess((UINT)-1);
    *argc = __argc;
    *argv = __argv;
    *envp = MsvcrtEnviron;
    return 0;
}

int __cdecl __wgetmainargs(int *argc, wchar_t ***argv, wchar_t ***envp, int wildcard, _startupinfo *info)
{
    if (info)
        MsvcrtSetNewMode(info->newmode);
    if (wsetargv(wildcard) < 0)
        ExitProcess((UINT)-1);
    MsvcrtWenvInit();
    *argc = __argc;
    *argv = __wargv;
    *envp = MsvcrtWenviron;
    return 0;
}

int *__cdecl __p___argc(void)
{
    return &__argc;
}

char ***__cdecl __p___argv(void)
{
    return &__argv;
}

wchar_t ***__cdecl __p___wargv(void)
{
    return &__wargv;
}

char **__cdecl __p__acmdln(void)
{
    return &_acmdln;
}

wchar_t **__cdecl __p__wcmdln(void)
{
    return &_wcmdln;
}

char **__cdecl __p__pgmptr(void)
{
    return &_pgmptr;
}

wchar_t **__cdecl __p__wpgmptr(void)
{
    return &_wpgmptr;
}

int *__cdecl __p__fmode(void)
{
    return &_fmode;
}

int *__cdecl __p__commode(void)
{
    return &_commode;
}

unsigned int *__cdecl __p__osver(void)
{
    return &_osver;
}

unsigned int *__cdecl __p__winver(void)
{
    return &_winver;
}

unsigned int *__cdecl __p__winmajor(void)
{
    return &_winmajor;
}

unsigned int *__cdecl __p__winminor(void)
{
    return &_winminor;
}

errno_t __cdecl _get_pgmptr(char **value)
{
    if (!value) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *value = _pgmptr;
    return 0;
}

errno_t __cdecl _get_wpgmptr(wchar_t **value)
{
    if (!value) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *value = _wpgmptr;
    return 0;
}

errno_t __cdecl _get_fmode(int *mode)
{
    if (!mode) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *mode = _fmode;
    return 0;
}

errno_t __cdecl _set_fmode(int mode)
{
    if (mode != _O_TEXT && mode != _O_BINARY && mode != _O_WTEXT) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    _fmode = mode;
    return 0;
}

errno_t __cdecl _get_osplatform(unsigned int *value)
{
    if (!value) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *value = _osplatform;
    return 0;
}

errno_t __cdecl _get_osver(unsigned int *value)
{
    if (!value) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *value = _osver;
    return 0;
}

errno_t __cdecl _get_winver(unsigned int *value)
{
    if (!value) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *value = _winver;
    return 0;
}

errno_t __cdecl _get_winmajor(unsigned int *value)
{
    if (!value) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *value = _winmajor;
    return 0;
}

errno_t __cdecl _get_winminor(unsigned int *value)
{
    if (!value) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *value = _winminor;
    return 0;
}

/* ------------------------------------------------------------------ initializers */

void __cdecl _initterm(_PVFV *first, _PVFV *last)
{
    for (; first < last; first++)
        if (*first)
            (*first)();
}

int __cdecl _initterm_e(_PIFV *first, _PIFV *last)
{
    int result = 0;
    for (; first < last && !result; first++)
        if (*first)
            result = (*first)();
    return result;
}

/* ------------------------------------------------------------------ onexit */

static PVOID g_onexit_begin;
static PVOID g_onexit_end;
static int g_exit_done;
static int g_termination_done;

static void onexit_init(void)
{
    if (g_onexit_begin)
        return;
    _PVFV *table = malloc(32 * sizeof(_PVFV));
    if (!table)
        return;
    table[0] = nullptr;
    g_onexit_begin = EncodePointer(table);
    g_onexit_end = EncodePointer(table);
}

_onexit_t __cdecl _onexit(_onexit_t func)
{
    _onexit_t result = nullptr;
    _lock(_EXIT_LOCK1);
    onexit_init();
    if (g_onexit_begin) {
        _PVFV *begin = DecodePointer(g_onexit_begin);
        _PVFV *end = DecodePointer(g_onexit_end);
        size_t used = (char *)end - (char *)begin;
        size_t size = _msize(begin);
        if (size < used + sizeof(_PVFV)) {
            size_t grow = size < 4096 ? size : 4096;
            _PVFV *nb = realloc(begin, size + grow);
            if (!nb)
                nb = realloc(begin, size + 32);
            if (nb) {
                begin = nb;
                end = (_PVFV *)((char *)nb + used);
                g_onexit_begin = EncodePointer(begin);
            } else {
                end = nullptr;
            }
        }
        if (end) {
            *end = EncodePointer((PVOID)func);
            g_onexit_end = EncodePointer(end + 1);
            result = func;
        }
    }
    _unlock(_EXIT_LOCK1);
    return result;
}

int __cdecl atexit(_PVFV func)
{
    return _onexit((_onexit_t)func) ? 0 : -1;
}

_onexit_t __cdecl __dllonexit(_onexit_t func, _PVFV **begin, _PVFV **end)
{
    _onexit_t result = nullptr;
    _lock(_EXIT_LOCK1);
    size_t used = (char *)*end - (char *)*begin;
    size_t size = _msize(*begin);
    if (size == (size_t)-1) {
        _unlock(_EXIT_LOCK1);
        return nullptr;
    }
    if (size < used + sizeof(_PVFV)) {
        size_t grow = size < 4096 ? size : 4096;
        _PVFV *nb = realloc(*begin, size + grow);
        if (!nb)
            nb = realloc(*begin, size + 32);
        if (!nb) {
            _unlock(_EXIT_LOCK1);
            return nullptr;
        }
        *begin = nb;
        *end = (_PVFV *)((char *)nb + used);
    }
    **end = (_PVFV)func;
    (*end)++;
    result = func;
    _unlock(_EXIT_LOCK1);
    return result;
}

PVOID __cdecl _encode_pointer(PVOID p)
{
    return EncodePointer(p);
}

PVOID __cdecl _decode_pointer(PVOID p)
{
    return DecodePointer(p);
}

PVOID __cdecl _encoded_null(void)
{
    return EncodePointer(nullptr);
}

/* calls the onexit functions last registered first; functions may register more. */
static void run_onexit(void)
{
    if (!g_onexit_begin)
        return;
    _PVFV *begin = DecodePointer(g_onexit_begin);
    _PVFV *end = DecodePointer(g_onexit_end);
    PVOID null = EncodePointer(nullptr);
    _PVFV *p = end;
    while (--p >= begin) {
        if (*p == (_PVFV)null || !*p)
            continue;
        _PVFV f = DecodePointer((PVOID)*p);
        *p = (_PVFV)null;
        f();
        _PVFV *nb = DecodePointer(g_onexit_begin);
        _PVFV *ne = DecodePointer(g_onexit_end);
        if (nb != begin || ne != end) {
            begin = nb;
            end = ne;
            p = ne;
        }
    }
}

static void doexit(int code, int quick, int retcaller)
{
    _lock(_EXIT_LOCK1);
    if (!g_exit_done) {
        g_termination_done = 1;
        if (!quick)
            run_onexit();
        MsvcrtFlushAll(0);
    }
    if (!retcaller) {
        g_exit_done = 1;
        _unlock(_EXIT_LOCK1);
        ExitProcess(code);
    }
    _unlock(_EXIT_LOCK1);
}

void __cdecl exit(int code)
{
    doexit(code, 0, 0);
    for (;;) {
    }
}

void __cdecl _exit(int code)
{
    doexit(code, 1, 0);
    for (;;) {
    }
}

void __cdecl _cexit(void)
{
    doexit(0, 0, 1);
}

void __cdecl _c_exit(void)
{
    doexit(0, 1, 1);
}

void MsvcrtProcessDetach(void)
{
    if (!g_termination_done)
        MsvcrtFlushAll(0);
    MsvcrtConsoleTerm();
}

/* ------------------------------------------------------------------ runtime errors */

static const struct {
    int code;
    const char *text;
} g_rterrs[] = {
    {2, "R6002\r\n- floating point support not loaded\r\n"},
    {8, "R6008\r\n- not enough space for arguments\r\n"},
    {9, "R6009\r\n- not enough space for environment\r\n"},
    {10, "\r\n"},
    {16, "R6016\r\n- not enough space for thread data\r\n"},
    {17, "R6017\r\n- unexpected multithread lock error\r\n"},
    {18, "R6018\r\n- unexpected heap error\r\n"},
    {19, "R6019\r\n- unable to open console device\r\n"},
    {24, "R6024\r\n- not enough space for _onexit/atexit table\r\n"},
    {25, "R6025\r\n- pure virtual function call\r\n"},
    {26, "R6026\r\n- not enough space for stdio initialization\r\n"},
    {27, "R6027\r\n- not enough space for lowio initialization\r\n"},
    {28, "R6028\r\n- unable to initialize heap\r\n"},
    {30, "R6030\r\n- CRT not initialized\r\n"},
    {31, "R6031\r\n- Attempt to initialize the CRT more than once.\r\n"},
    {32, "R6032\r\n- not enough space for locale information\r\n"},
    {33, "R6033\r\n- Attempt to use MSIL code from this assembly during native code initialization\r\n"},
    {120, "runtime error "},
    {252, "\r\n"},
    {255, "run-time error "},
};

static const char *rterr_text(int code)
{
    for (size_t i = 0; i < sizeof(g_rterrs) / sizeof(g_rterrs[0]); i++)
        if (g_rterrs[i].code == code)
            return g_rterrs[i].text;
    return nullptr;
}

static void write_stderr(const char *s)
{
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD written;
    if (h && h != INVALID_HANDLE_VALUE)
        WriteFile(h, s, (DWORD)strlen(s), &written, nullptr);
    OutputDebugStringA(s);
}

/* writes runtime error code to stderr, like _NMSG_WRITE. */
void MsvcrtRuntimeError(int code)
{
    const char *text = rterr_text(code);
    if (!text)
        return;
    if (code != 10 && code != 252 && code != 255 && code != 120) {
        write_stderr("\r\nRuntime Error!\r\n\r\n");
        write_stderr(_pgmptr ? _pgmptr : "<program name unknown>");
        write_stderr("\r\n\r\n");
    }
    write_stderr(text);
}

void __cdecl _amsg_exit(int code)
{
    static int reentered;
    if (!reentered) {
        reentered = 1;
        MsvcrtRuntimeError(code);
        exit(255);
    }
    ExitProcess(255);
}

void __cdecl _FF_MSGBANNER(void)
{
}

void __cdecl _NMSG_WRITE(int code)
{
    MsvcrtRuntimeError(code);
}

typedef void(__cdecl *_purecall_handler)(void);
_purecall_handler __cdecl _get_purecall_handler(void);
static PVOID g_purecall_handler;

int __cdecl _purecall(void)
{
    _purecall_handler handler = g_purecall_handler ? (_purecall_handler)DecodePointer(g_purecall_handler) : nullptr;
    if (handler) {
        handler();
        return 0;
    }
    _amsg_exit(25);
    return 0;
}

_purecall_handler __cdecl _set_purecall_handler(_purecall_handler handler)
{
    _purecall_handler old = _get_purecall_handler();
    g_purecall_handler = handler ? EncodePointer((PVOID)handler) : nullptr;
    return old;
}

_purecall_handler __cdecl _get_purecall_handler(void)
{
    return g_purecall_handler ? (_purecall_handler)DecodePointer(g_purecall_handler) : nullptr;
}
