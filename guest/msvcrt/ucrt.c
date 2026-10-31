/* ucrt startup and stream entry points over the shared c runtime. */
#include "msvcrt_private.h"

int __cdecl atexit(_PVFV function);
int __cdecl __getmainargs(int *argc, char ***argv, char ***env, int wildcard, void *info);
int __cdecl _XcptFilter(ULONG code, PEXCEPTION_POINTERS pointers);
int __cdecl vfprintf(FILE *stream, const char *format, va_list args);
int __cdecl vfwprintf(FILE *stream, const wchar_t *format, va_list args);
int __cdecl _vsnprintf_s(char *buffer, size_t size, size_t count, const char *format, va_list args);
int __cdecl _vsnwprintf_s(wchar_t *buffer, size_t size, size_t count, const wchar_t *format, va_list args);
int __cdecl vsprintf_s(char *buffer, size_t size, const char *format, va_list args);
int __cdecl vswprintf_s(wchar_t *buffer, size_t size, const wchar_t *format, va_list args);

int __cdecl _vcprintf(const char *fmt, va_list ap);
int __cdecl _vcwprintf(const wchar_t *fmt, va_list ap);
int __cdecl _fgetc_nolock(FILE *f);
int __cdecl _fputc_nolock(int c, FILE *f);
wint_t __cdecl _fgetwc_nolock(FILE *f);
wint_t __cdecl _fputwc_nolock(wchar_t c, FILE *f);
wint_t __cdecl ungetwc(wint_t c, FILE *f);
size_t __cdecl _fread_nolock(void *buf, size_t size, size_t count, FILE *f);
int __cdecl _getche_nolock(void);
wint_t __cdecl _getwche_nolock(void);
int __cdecl _cputs(const char *s);
int __cdecl _putch(int c);
int __cdecl _cputws(const wchar_t *s);
int __cdecl _getch_nolock(void);
wint_t __cdecl _getwch_nolock(void);
int __cdecl _putch_nolock(int c);
wint_t __cdecl _putwch_nolock(wchar_t c);

typedef struct { _onexit_t *first, *last, *end; } MIME_ONEXIT_TABLE;

DECLSPEC_EXPORT int __cdecl _initialize_onexit_table(MIME_ONEXIT_TABLE *table)
{
    if (!table)
        return -1;
    memset(table, 0, sizeof(*table));
    return 0;
}

DECLSPEC_EXPORT int __cdecl _register_onexit_function(MIME_ONEXIT_TABLE *table, _onexit_t function)
{
    if (!table || !function)
        return -1;
    _lock(_EXIT_LOCK1);
    if (table->last == table->end) {
        size_t count = table->first ? (size_t)(table->last - table->first) : 0;
        size_t capacity = count ? count * 2 : 16;
        _onexit_t *fresh = realloc(table->first, capacity * sizeof(*fresh));
        if (!fresh) {
            _unlock(_EXIT_LOCK1);
            return -1;
        }
        table->first = fresh;
        table->last = fresh + count;
        table->end = fresh + capacity;
    }
    *table->last++ = function;
    _unlock(_EXIT_LOCK1);
    return 0;
}

DECLSPEC_EXPORT int __cdecl _execute_onexit_table(MIME_ONEXIT_TABLE *table)
{
    if (!table)
        return -1;
    _lock(_EXIT_LOCK1);
    while (table->last != table->first) {
        _onexit_t function = *--table->last;
        _unlock(_EXIT_LOCK1);
        function();
        _lock(_EXIT_LOCK1);
    }
    free(table->first);
    memset(table, 0, sizeof(*table));
    _unlock(_EXIT_LOCK1);
    return 0;
}

DECLSPEC_EXPORT int __cdecl _crt_atexit(_PVFV function)
{
    return atexit(function);
}

DECLSPEC_EXPORT int __cdecl _initialize_narrow_environment(void)
{
    return MsvcrtEnviron ? 0 : -1;
}

DECLSPEC_EXPORT int __cdecl _configure_narrow_argv(int mode)
{
    if (mode < 0 || mode > 2)
        return EINVAL;
    if (!mode)
        return 0;
    int argc, info = 0;
    char **argv, **env;
    return __getmainargs(&argc, &argv, &env, mode == 2, &info);
}

DECLSPEC_EXPORT FILE *__cdecl __acrt_iob_func(unsigned index)
{
    return index < 3 ? &MsvcrtIob[index] : nullptr;
}

DECLSPEC_EXPORT int __cdecl _get_stream_buffer_pointers(FILE *stream, char ***base, char ***pointer, int **count)
{
    if (!stream)
        return EINVAL;
    if (base)
        *base = &stream->_base;
    if (pointer)
        *pointer = &stream->_ptr;
    if (count)
        *count = &stream->_cnt;
    return 0;
}

DECLSPEC_EXPORT wchar_t **__cdecl ___lc_locale_name_func(void)
{
    static wchar_t *names[6];
    return names;
}

DECLSPEC_EXPORT void __cdecl _lock_locales(void) { _lock(_SETLOCALE_LOCK); }
DECLSPEC_EXPORT void __cdecl _unlock_locales(void) { _unlock(_SETLOCALE_LOCK); }
DECLSPEC_EXPORT int __cdecl _seh_filter_dll(ULONG code, PEXCEPTION_POINTERS pointers)
{
    return code == 0xe06d7363u ? _XcptFilter(code, pointers) : EXCEPTION_CONTINUE_SEARCH;
}

DECLSPEC_EXPORT DECLSPEC_NORETURN void __cdecl _invalid_parameter_noinfo_noreturn(void)
{
    MsvcrtInvalidParameter();
    _exit(3);
}

DECLSPEC_EXPORT DECLSPEC_NORETURN void __cdecl terminate(void)
{
    abort();
    _exit(3);
}

DECLSPEC_EXPORT int __cdecl __stdio_common_vfprintf(ULONGLONG options, FILE *stream, const char *format,
                                                   void *locale, va_list args)
{
    return vfprintf(stream, format, args);
}

DECLSPEC_EXPORT int __cdecl __stdio_common_vfwprintf(ULONGLONG options, FILE *stream, const wchar_t *format,
                                                    void *locale, va_list args)
{
    return vfwprintf(stream, format, args);
}

DECLSPEC_EXPORT int __cdecl __stdio_common_vsnprintf_s(ULONGLONG options, char *buffer, size_t size, size_t count,
                                                      const char *format, void *locale, va_list args)
{
    return _vsnprintf_s(buffer, size, count, format, args);
}

DECLSPEC_EXPORT int __cdecl __stdio_common_vsnwprintf_s(ULONGLONG options, wchar_t *buffer, size_t size, size_t count,
                                                       const wchar_t *format, void *locale, va_list args)
{
    return _vsnwprintf_s(buffer, size, count, format, args);
}

DECLSPEC_EXPORT int __cdecl __stdio_common_vsprintf_s(ULONGLONG options, char *buffer, size_t size,
                                                     const char *format, void *locale, va_list args)
{
    return vsprintf_s(buffer, size, format, args);
}

DECLSPEC_EXPORT int __cdecl __stdio_common_vswprintf(ULONGLONG options, wchar_t *buffer, size_t size,
                                                    const wchar_t *format, void *locale, va_list args)
{
    if (!format || (!buffer && size)) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    va_list measure;
    va_copy(measure, args);
    int count = MsvcrtVsnwprintf(nullptr, 0, format, measure);
    va_end(measure);
    if (count < 0)
        return count;
    if (buffer && size) {
        size_t capacity = options & 1 ? size : size - 1;
        MsvcrtVsnwprintf(buffer, capacity, format, args);
        if ((size_t)count < size)
            buffer[count] = 0;
        else if (!(options & 1))
            buffer[size - 1] = 0;
    }
    /* the legacy form fills the buffer without a terminator when the text is as long as it, and says so. */
    return (options & 2) || !buffer || (size_t)count < size || ((options & 1) && (size_t)count == size) ? count : -1;
}

/* narrow __stdio_common_vswprintf: same options, legacy truncation and a count of what was asked. */
DECLSPEC_EXPORT int __cdecl __stdio_common_vsprintf(ULONGLONG options, char *buffer, size_t size, const char *format,
                                                    void *locale, va_list args)
{
    if (!format || (!buffer && size)) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    va_list measure;
    va_copy(measure, args);
    int count = MsvcrtVsnprintf(nullptr, 0, format, measure);
    va_end(measure);
    if (count < 0)
        return count;
    if (buffer && size) {
        size_t capacity = options & 1 ? size : size - 1;
        MsvcrtVsnprintf(buffer, capacity, format, args);
        if ((size_t)count < size)
            buffer[count] = 0;
        else if (!(options & 1))
            buffer[size - 1] = 0;
    }
    /* the legacy form fills the buffer without a terminator when the text is as long as it, and says so. */
    return (options & 2) || !buffer || (size_t)count < size || ((options & 1) && (size_t)count == size) ? count : -1;
}

DECLSPEC_EXPORT int __cdecl __stdio_common_vswprintf_s(ULONGLONG options, wchar_t *buffer, size_t size,
                                                      const wchar_t *format, void *locale, va_list args)
{
    return vswprintf_s(buffer, size, format, args);
}

DECLSPEC_EXPORT int __cdecl __stdio_common_vsscanf(ULONGLONG options, const char *buffer, size_t size,
                                                   const char *format, void *locale, va_list args)
{
    if (!buffer || !format) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    return MsvcrtScanExA(nullptr, buffer, size, format, args, (int)(options & 1));
}

DECLSPEC_EXPORT int __cdecl __stdio_common_vswscanf(ULONGLONG options, const wchar_t *buffer, size_t size,
                                                    const wchar_t *format, void *locale, va_list args)
{
    if (!buffer || !format) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    return MsvcrtScanExW(nullptr, buffer, size, format, args, (int)(options & 1));
}

/* the secure form takes the sizes of the strings it fills. */
DECLSPEC_EXPORT int __cdecl __stdio_common_vfscanf(ULONGLONG options, FILE *stream, const char *format, void *locale,
                                                   va_list args)
{
    if (!stream || !format) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    MsvcrtLockFile(stream);
    int r = MsvcrtScanExA(stream, nullptr, 0, format, args, (int)(options & 1));
    MsvcrtUnlockFile(stream);
    return r;
}

DECLSPEC_EXPORT int __cdecl __stdio_common_vfwscanf(ULONGLONG options, FILE *stream, const wchar_t *format,
                                                    void *locale, va_list args)
{
    if (!stream || !format) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    MsvcrtLockFile(stream);
    int r = MsvcrtScanExW(stream, nullptr, 0, format, args, (int)(options & 1));
    MsvcrtUnlockFile(stream);
    return r;
}

/* printf with numbered arguments, and the checked stream and string forms. */
DECLSPEC_EXPORT int __cdecl __stdio_common_vfprintf_p(ULONGLONG options, FILE *stream, const char *format, void *locale,
                                                      va_list args)
{
    return MsvcrtFormatPositionalA(stream, format, args);
}

DECLSPEC_EXPORT int __cdecl __stdio_common_vfwprintf_p(ULONGLONG options, FILE *stream, const wchar_t *format,
                                                       void *locale, va_list args)
{
    return MsvcrtFormatPositionalW(stream, format, args);
}

DECLSPEC_EXPORT int __cdecl __stdio_common_vsprintf_p(ULONGLONG options, char *buffer, size_t size, const char *format,
                                                      void *locale, va_list args)
{
    return MsvcrtVsprintfPositionalA(buffer, size, format, args);
}

DECLSPEC_EXPORT int __cdecl __stdio_common_vswprintf_p(ULONGLONG options, wchar_t *buffer, size_t size,
                                                       const wchar_t *format, void *locale, va_list args)
{
    return MsvcrtVsprintfPositionalW(buffer, size, format, args);
}

DECLSPEC_EXPORT int __cdecl __stdio_common_vfprintf_s(ULONGLONG options, FILE *stream, const char *format, void *locale,
                                                      va_list args)
{
    return vfprintf(stream, format, args);
}

DECLSPEC_EXPORT int __cdecl __stdio_common_vfwprintf_s(ULONGLONG options, FILE *stream, const wchar_t *format,
                                                       void *locale, va_list args)
{
    return vfwprintf(stream, format, args);
}

/* ------------------------------------------------------------------ unlocked streams */

DECLSPEC_EXPORT int __cdecl _fclose_nolock(FILE *f)
{
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    return MsvcrtFcloseNolock(f);
}

DECLSPEC_EXPORT int __cdecl _getc_nolock(FILE *f)
{
    return _fgetc_nolock(f);
}

DECLSPEC_EXPORT int __cdecl _putc_nolock(int c, FILE *f)
{
    return _fputc_nolock(c, f);
}

DECLSPEC_EXPORT wint_t __cdecl _getwc_nolock(FILE *f)
{
    return _fgetwc_nolock(f);
}

DECLSPEC_EXPORT wint_t __cdecl _putwc_nolock(wchar_t c, FILE *f)
{
    return _fputwc_nolock(c, f);
}

/* the file lock is recursive here, so the locked form serves. */
DECLSPEC_EXPORT wint_t __cdecl _ungetwc_nolock(wint_t c, FILE *f)
{
    return ungetwc(c, f);
}

/* fread_s without the lock: a buffer too small is a range error and nothing is read. */
DECLSPEC_EXPORT size_t __cdecl _fread_nolock_s(void *buf, size_t bufsize, size_t size, size_t count, FILE *f)
{
    if (!size || !count)
        return 0;
    if (!buf) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    if (count > bufsize / size) {
        memset(buf, 0, bufsize);
        MSVCRT_INVALID(ERANGE);
        return 0;
    }
    return _fread_nolock(buf, size, count, f);
}

/* reads at most size - 1 characters; a longer line is a range error and is dropped. */
DECLSPEC_EXPORT char *__cdecl gets_s(char *buf, size_t size)
{
    if (!buf || !size) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    FILE *f = msvcrt_stdin;
    MsvcrtLockFile(f);
    size_t n = 0;
    int c;
    BOOL overflow = FALSE;
    while ((c = _fgetc_nolock(f)) != '\n') {
        if (c == EOF) {
            if (!n && !overflow) {
                *buf = 0;
                MsvcrtUnlockFile(f);
                return nullptr;
            }
            break;
        }
        if (n + 1 >= size)
            overflow = TRUE;
        else
            buf[n++] = (char)c;
    }
    MsvcrtUnlockFile(f);
    if (overflow) {
        *buf = 0;
        MSVCRT_INVALID(ERANGE);
        return nullptr;
    }
    buf[n] = 0;
    return buf;
}

DECLSPEC_EXPORT wchar_t *__cdecl _getws_s(wchar_t *buf, size_t size)
{
    if (!buf || !size) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    FILE *f = msvcrt_stdin;
    MsvcrtLockFile(f);
    size_t n = 0;
    wint_t c;
    BOOL overflow = FALSE;
    while ((c = _fgetwc_nolock(f)) != '\n') {
        if (c == WEOF) {
            if (!n && !overflow) {
                *buf = 0;
                MsvcrtUnlockFile(f);
                return nullptr;
            }
            break;
        }
        if (n + 1 >= size)
            overflow = TRUE;
        else
            buf[n++] = c;
    }
    MsvcrtUnlockFile(f);
    if (overflow) {
        *buf = 0;
        MSVCRT_INVALID(ERANGE);
        return nullptr;
    }
    buf[n] = 0;
    return buf;
}

/* ------------------------------------------------------------------ console */

DECLSPEC_EXPORT int __cdecl __conio_common_vcprintf(ULONGLONG options, const char *format, void *locale, va_list args)
{
    return _vcprintf(format, args);
}

DECLSPEC_EXPORT int __cdecl __conio_common_vcprintf_s(ULONGLONG options, const char *format, void *locale,
                                                      va_list args)
{
    return _vcprintf(format, args);
}

DECLSPEC_EXPORT int __cdecl __conio_common_vcwprintf(ULONGLONG options, const wchar_t *format, void *locale,
                                                     va_list args)
{
    return _vcwprintf(format, args);
}

DECLSPEC_EXPORT int __cdecl __conio_common_vcwprintf_s(ULONGLONG options, const wchar_t *format, void *locale,
                                                       va_list args)
{
    return _vcwprintf(format, args);
}

/* formats the text first, then writes it. */
DECLSPEC_EXPORT int __cdecl __conio_common_vcprintf_p(ULONGLONG options, const char *format, void *locale,
                                                      va_list args)
{
    char text[1024];
    int n = MsvcrtVsprintfPositionalA(text, sizeof(text), format, args);
    if (n < 0)
        return -1;
    return _cputs(text) ? -1 : n;
}

DECLSPEC_EXPORT int __cdecl __conio_common_vcwprintf_p(ULONGLONG options, const wchar_t *format, void *locale,
                                                       va_list args)
{
    wchar_t text[1024];
    int n = MsvcrtVsprintfPositionalW(text, RTL_NUMBER_OF(text), format, args);
    if (n < 0)
        return -1;
    return _cputws(text) ? -1 : n;
}

/*
 * reads a line with echo like _cgets, then scans it. unlike Windows, what the format leaves unread is lost instead of
 * kept for the next call.
 */
DECLSPEC_EXPORT int __cdecl __conio_common_vcscanf(ULONGLONG options, const char *format, void *locale, va_list args)
{
    char line[512];
    size_t n = 0;
    if (!format) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    _lock(_CONIO_LOCK);
    for (;;) {
        int c = _getche_nolock();
        if (c == EOF || c == '\r')
            break;
        if (c == '\b') {
            if (n) {
                n--;
                _cputs(" \b");
            }
            continue;
        }
        if (n + 1 < sizeof(line))
            line[n++] = (char)c;
    }
    _unlock(_CONIO_LOCK);
    line[n] = 0;
    _putch('\n');
    if (!n)
        return EOF;
    return MsvcrtScanExA(nullptr, line, n, format, args, (int)(options & 1));
}

DECLSPEC_EXPORT int __cdecl __conio_common_vcwscanf(ULONGLONG options, const wchar_t *format, void *locale,
                                                    va_list args)
{
    wchar_t line[512];
    size_t n = 0;
    if (!format) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    _lock(_CONIO_LOCK);
    for (;;) {
        wint_t c = _getwche_nolock();
        if (c == WEOF || c == '\r')
            break;
        if (c == '\b') {
            if (n) {
                n--;
                _cputs(" \b");
            }
            continue;
        }
        if (n + 1 < RTL_NUMBER_OF(line))
            line[n++] = (wchar_t)c;
    }
    _unlock(_CONIO_LOCK);
    line[n] = 0;
    _putwch_nolock(L'\n');
    if (!n)
        return EOF;
    return MsvcrtScanExW(nullptr, line, n, format, args, (int)(options & 1));
}

/* ------------------------------------------------------------------ exit, environment and command line */

int MsvcrtAppType(void);
int __cdecl _controlfp_s(unsigned *current, unsigned new_control, unsigned mask);
unsigned int __cdecl _controlfp(unsigned int new_control, unsigned int mask);
int __cdecl _wputenv_s(const wchar_t *name, const wchar_t *value);

/* like _exit: no atexit handler runs and no stream is flushed. */
DECLSPEC_EXPORT DECLSPEC_NORETURN void __cdecl _Exit(int code)
{
    _exit(code);
}

#define QUICK_EXIT_MAX 64
static _PVFV g_quick_exit[QUICK_EXIT_MAX];
static int g_quick_exit_count;

DECLSPEC_EXPORT int __cdecl _crt_at_quick_exit(_PVFV function)
{
    int result = -1;
    _lock(_EXIT_LOCK1);
    if (function && g_quick_exit_count < QUICK_EXIT_MAX) {
        g_quick_exit[g_quick_exit_count++] = function;
        result = 0;
    }
    _unlock(_EXIT_LOCK1);
    return result;
}

/* runs the at_quick_exit handlers in reverse order, then ends without the atexit ones. */
DECLSPEC_EXPORT DECLSPEC_NORETURN void __cdecl quick_exit(int code)
{
    for (;;) {
        _PVFV function = nullptr;
        _lock(_EXIT_LOCK1);
        if (g_quick_exit_count)
            function = g_quick_exit[--g_quick_exit_count];
        _unlock(_EXIT_LOCK1);
        if (!function)
            break;
        function();
    }
    _exit(code);
}

DECLSPEC_EXPORT char **__cdecl _get_initial_narrow_environment(void)
{
    return MsvcrtEnviron;
}

DECLSPEC_EXPORT wchar_t **__cdecl _get_initial_wide_environment(void)
{
    MsvcrtWenvInit();
    return MsvcrtWenviron;
}

DECLSPEC_EXPORT int __cdecl _initialize_wide_environment(void)
{
    MsvcrtWenvInit();
    return MsvcrtWenviron ? 0 : -1;
}

DECLSPEC_EXPORT int __cdecl _configure_wide_argv(int mode)
{
    if (mode < 0 || mode > 2)
        return EINVAL;
    if (!mode)
        return 0;
    int argc, info = 0;
    wchar_t **argv, **env;
    int __cdecl __wgetmainargs(int *argc, wchar_t ***argv, wchar_t ***env, int wildcard, void *info);
    return __wgetmainargs(&argc, &argv, &env, mode == 2, &info);
}

/* the command line after the program name, as winmain gets it. */
DECLSPEC_EXPORT char *__cdecl _get_narrow_winmain_command_line(void)
{
    char *line = GetCommandLineA();
    if (*line == '"') {
        line++;
        while (*line && *line != '"')
            line++;
        if (*line)
            line++;
    } else {
        while (*line && *line > ' ')
            line++;
    }
    while (*line && *line <= ' ')
        line++;
    return line;
}

DECLSPEC_EXPORT wchar_t *__cdecl _get_wide_winmain_command_line(void)
{
    wchar_t *line = GetCommandLineW();
    if (*line == L'"') {
        line++;
        while (*line && *line != L'"')
            line++;
        if (*line)
            line++;
    } else {
        while (*line && *line > L' ')
            line++;
    }
    while (*line && *line <= L' ')
        line++;
    return line;
}

/* the caller frees the block. */
DECLSPEC_EXPORT wchar_t *__cdecl __dcrt_get_wide_environment_from_os(void)
{
    wchar_t *block = GetEnvironmentStringsW();
    if (!block)
        return nullptr;
    size_t length = 0;
    while (block[length])
        length += wcslen(block + length) + 1;
    wchar_t *copy = malloc((length + 1) * sizeof(wchar_t));
    if (copy)
        memcpy(copy, block, (length + 1) * sizeof(wchar_t));
    FreeEnvironmentStringsW(block);
    return copy;
}

DECLSPEC_EXPORT char **__cdecl __dcrt_initial_narrow_environment(void)
{
    return MsvcrtEnviron;
}

DECLSPEC_EXPORT int __cdecl _query_app_type(void)
{
    return MsvcrtAppType();
}

DECLSPEC_EXPORT void __cdecl _crt_debugger_hook(int reserved)
{
}

/* ------------------------------------------------------------------ floating point and libraries */

/* the value of FLT_ROUNDS: 0 toward zero, 1 to nearest, 2 up and 3 down. */
DECLSPEC_EXPORT int __cdecl __fpe_flt_rounds(void)
{
    switch (_controlfp(0, 0) & 0x300) {
    case 0x300:
        return 0;
    case 0x200:
        return 2;
    case 0x100:
        return 3;
    default:
        return 1;
    }
}

/* nothing here uses the fused instructions, so the setting is only kept. */
static int g_fma3 = -1;

static int fma3_supported(void)
{
    return IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) != 0;
}

DECLSPEC_EXPORT int __cdecl _set_FMA3_enable(int enable)
{
    g_fma3 = enable && fma3_supported();
    return g_fma3;
}

DECLSPEC_EXPORT int __cdecl _get_FMA3_enable(void)
{
    return g_fma3 < 0 ? fma3_supported() : g_fma3;
}

DECLSPEC_EXPORT intptr_t __cdecl _loaddll(char *name)
{
    return (intptr_t)LoadLibraryA(name);
}

DECLSPEC_EXPORT int __cdecl _unloaddll(intptr_t module)
{
    return FreeLibrary((HMODULE)module) ? 0 : (int)GetLastError();
}

DECLSPEC_EXPORT int(__cdecl *__cdecl _getdllprocaddr(intptr_t module, char *name, intptr_t ordinal))(void)
{
    if (name)
        return (int(__cdecl *)(void))GetProcAddress((HMODULE)module, name);
    return (int(__cdecl *)(void))GetProcAddress((HMODULE)module, (LPCSTR)(ordinal & 0xffff));
}
