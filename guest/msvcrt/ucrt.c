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
    return index < 3 ? &MsvcrtIob[index] : NULL;
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
    int count = MsvcrtVsnwprintf(NULL, 0, format, measure);
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
    return (options & 2) || !buffer || (size_t)count < size ? count : -1;
}

DECLSPEC_EXPORT int __cdecl __stdio_common_vsscanf(ULONGLONG options, const char *buffer, size_t size,
                                                   const char *format, void *locale, va_list args)
{
    if (!buffer || !format) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    return MsvcrtScanA(NULL, buffer, size, format, args);
}
