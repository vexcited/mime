/*
 * error messages, signals, abort and assertions, random numbers,
 * sorting and searching.
 */
#include "msvcrt_private.h"

/* ------------------------------------------------------------------ error messages */

const char *_sys_errlist[44] = {
    "No error",
    "Operation not permitted",
    "No such file or directory",
    "No such process",
    "Interrupted function call",
    "Input/output error",
    "No such device or address",
    "Arg list too long",
    "Exec format error",
    "Bad file descriptor",
    "No child processes",
    "Resource temporarily unavailable",
    "Not enough space",
    "Permission denied",
    "Bad address",
    "Unknown error",
    "Resource device",
    "File exists",
    "Improper link",
    "No such device",
    "Not a directory",
    "Is a directory",
    "Invalid argument",
    "Too many open files in system",
    "Too many open files",
    "Inappropriate I/O control operation",
    "Unknown error",
    "File too large",
    "No space left on device",
    "Invalid seek",
    "Read-only file system",
    "Too many links",
    "Broken pipe",
    "Domain error",
    "Result too large",
    "Unknown error",
    "Resource deadlock avoided",
    "Unknown error",
    "Filename too long",
    "No locks available",
    "Function not implemented",
    "Directory not empty",
    "Illegal byte sequence",
    "Unknown error",
};

int _sys_nerr = 43;

int *__cdecl __sys_nerr(void)
{
    return &_sys_nerr;
}

const char **__cdecl __sys_errlist(void)
{
    return _sys_errlist;
}

static const char *errmsg(int e)
{
    return (unsigned)e < 43 ? _sys_errlist[e] : _sys_errlist[43];
}

typedef struct {
    char strerror_buf[134];
    wchar_t wcserror_buf[134];
} ERRBUF;

static DWORD g_errbuf_tls = TLS_OUT_OF_INDEXES;

static ERRBUF *errbuf(void)
{
    static ERRBUF fallback;
    if (g_errbuf_tls == TLS_OUT_OF_INDEXES) {
        DWORD i = TlsAlloc();
        if (InterlockedCompareExchange((LONG *)&g_errbuf_tls, (LONG)i, (LONG)TLS_OUT_OF_INDEXES) !=
            (LONG)TLS_OUT_OF_INDEXES)
            TlsFree(i);
    }
    if (g_errbuf_tls == TLS_OUT_OF_INDEXES)
        return &fallback;
    ERRBUF *b = TlsGetValue(g_errbuf_tls);
    if (!b) {
        b = HeapAlloc(MsvcrtHeap, HEAP_ZERO_MEMORY, sizeof(*b));
        if (!b)
            return &fallback;
        TlsSetValue(g_errbuf_tls, b);
    }
    return b;
}

char *__cdecl strerror(int e)
{
    char *buf = errbuf()->strerror_buf;
    strcpy(buf, errmsg(e));
    return buf;
}

errno_t __cdecl strerror_s(char *buf, size_t size, int e)
{
    if (!buf || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    const char *m = errmsg(e);
    size_t n = strlen(m);
    if (n >= size) {
        memcpy(buf, m, size - 1);
        buf[size - 1] = 0;
        return STRUNCATE;
    }
    memcpy(buf, m, n + 1);
    return 0;
}

wchar_t *__cdecl _wcserror(int e)
{
    wchar_t *buf = errbuf()->wcserror_buf;
    const char *m = errmsg(e);
    size_t i = 0;
    for (; m[i]; i++)
        buf[i] = (unsigned char)m[i];
    buf[i] = 0;
    return buf;
}

errno_t __cdecl _wcserror_s(wchar_t *buf, size_t size, int e)
{
    if (!buf || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    const char *m = errmsg(e);
    size_t i = 0;
    for (; m[i] && i + 1 < size; i++)
        buf[i] = (unsigned char)m[i];
    buf[i] = 0;
    return m[i] ? STRUNCATE : 0;
}

/* "message: error\n" or "error\n" like msvcrt's _strerror. */
char *__cdecl _strerror(const char *message)
{
    char *buf = errbuf()->strerror_buf;
    const char *m = errmsg(*_errno());
    buf[0] = 0;
    if (message && *message) {
        strncat(buf, message, 94);
        strcat(buf, ": ");
    }
    strcat(buf, m);
    strcat(buf, "\n");
    return buf;
}

wchar_t *__cdecl __wcserror(const wchar_t *message)
{
    wchar_t *buf = errbuf()->wcserror_buf;
    const char *m = errmsg(*_errno());
    buf[0] = 0;
    if (message && *message) {
        wcsncat(buf, message, 94);
        wcscat(buf, L": ");
    }
    size_t n = wcslen(buf);
    for (size_t i = 0; m[i]; i++)
        buf[n++] = (unsigned char)m[i];
    buf[n++] = '\n';
    buf[n] = 0;
    return buf;
}

void __cdecl perror(const char *message)
{
    int fd = msvcrt_stderr->_file;
    if (fd < 0)
        fd = 2;
    const char *m = errmsg(*_errno());
    if (message && *message) {
        _write(fd, message, (unsigned)strlen(message));
        _write(fd, ": ", 2);
    }
    _write(fd, m, (unsigned)strlen(m));
    _write(fd, "\n", 1);
}

void __cdecl _wperror(const wchar_t *message)
{
    char *a = MsvcrtWToA(message);
    perror(a);
    free(a);
}

/* ------------------------------------------------------------------ signals */

#define SIGINT 2
#define SIGILL 4
#define SIGABRT_COMPAT 6
#define SIGFPE 8
#define SIGSEGV 11
#define SIGTERM 15
#define SIGBREAK 21
#define SIGABRT 22

#define SIG_DFL ((SIGHANDLER)0)
#define SIG_IGN ((SIGHANDLER)1)
#define SIG_ERR ((SIGHANDLER)-1)
#define SIG_GET ((SIGHANDLER)2)
#define SIG_SGE ((SIGHANDLER)3)
#define SIG_ACK ((SIGHANDLER)4)

#define _FPE_EXPLICITGEN 0x8c

typedef void(__cdecl *SIGHANDLER)(int);
typedef void(__cdecl *FPEHANDLER)(int, int);

static SIGHANDLER g_handlers[23];
static int g_ctrl_installed;

static BOOL WINAPI ctrl_handler(DWORD type)
{
    int sig = type == CTRL_C_EVENT ? SIGINT : type == CTRL_BREAK_EVENT ? SIGBREAK : 0;
    if (!sig)
        return FALSE;
    _lock(_SIGNAL_LOCK);
    SIGHANDLER h = g_handlers[sig];
    if (h != SIG_DFL && h != SIG_IGN)
        g_handlers[sig] = SIG_DFL;
    _unlock(_SIGNAL_LOCK);
    if (h == SIG_DFL)
        return FALSE;
    if (h != SIG_IGN)
        h(sig);
    return TRUE;
}

static int valid_signal(int sig)
{
    return sig == SIGINT || sig == SIGILL || sig == SIGABRT_COMPAT || sig == SIGFPE || sig == SIGSEGV ||
           sig == SIGTERM || sig == SIGBREAK || sig == SIGABRT;
}

SIGHANDLER __cdecl signal(int sig, SIGHANDLER handler)
{
    if (handler == SIG_SGE || handler == SIG_ACK || !valid_signal(sig)) {
        MSVCRT_INVALID(EINVAL);
        return SIG_ERR;
    }
    if (sig == SIGABRT_COMPAT)
        sig = SIGABRT;
    _lock(_SIGNAL_LOCK);
    if ((sig == SIGINT || sig == SIGBREAK) && !g_ctrl_installed) {
        if (!SetConsoleCtrlHandler(ctrl_handler, TRUE)) {
            *__doserrno() = GetLastError();
            _unlock(_SIGNAL_LOCK);
            MsvcrtSetErrno(EINVAL);
            return SIG_ERR;
        }
        g_ctrl_installed = 1;
    }
    SIGHANDLER old = g_handlers[sig];
    if (handler != SIG_GET)
        g_handlers[sig] = handler;
    _unlock(_SIGNAL_LOCK);
    return old;
}

int __cdecl raise(int sig)
{
    if (!valid_signal(sig)) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    if (sig == SIGABRT_COMPAT)
        sig = SIGABRT;
    _lock(_SIGNAL_LOCK);
    SIGHANDLER h = g_handlers[sig];
    if (h != SIG_IGN && h != SIG_DFL)
        g_handlers[sig] = SIG_DFL;
    _unlock(_SIGNAL_LOCK);
    if (h == SIG_IGN)
        return 0;
    if (h == SIG_DFL)
        exit(3);
    if (sig == SIGFPE)
        ((FPEHANDLER)(void *)h)(sig, _FPE_EXPLICITGEN);
    else
        h(sig);
    return 0;
}

void *__cdecl __pxcptinfoptrs(void)
{
    static void *pointers;
    return &pointers;
}

int __cdecl __fpecode(void)
{
    return MsvcrtThreadData()->fpecode;
}

int *__cdecl __fpecode_ptr(void)
{
    return &MsvcrtThreadData()->fpecode;
}

/* the exception filter of the c runtime startup code, applying the signal actions. */
int __cdecl _XcptFilter(unsigned long code, PEXCEPTION_POINTERS pointers)
{
    int sig;
    switch (code) {
    case STATUS_ACCESS_VIOLATION:
        sig = SIGSEGV;
        break;
    case STATUS_ILLEGAL_INSTRUCTION:
    case STATUS_PRIVILEGED_INSTRUCTION:
        sig = SIGILL;
        break;
    case STATUS_FLOAT_DENORMAL_OPERAND:
    case STATUS_FLOAT_DIVIDE_BY_ZERO:
    case STATUS_FLOAT_INEXACT_RESULT:
    case STATUS_FLOAT_INVALID_OPERATION:
    case STATUS_FLOAT_OVERFLOW:
    case STATUS_FLOAT_STACK_CHECK:
    case STATUS_FLOAT_UNDERFLOW:
        sig = SIGFPE;
        break;
    default:
        return UnhandledExceptionFilter(pointers);
    }
    _lock(_SIGNAL_LOCK);
    SIGHANDLER h = g_handlers[sig];
    if (h != SIG_IGN && h != SIG_DFL)
        g_handlers[sig] = SIG_DFL;
    _unlock(_SIGNAL_LOCK);
    if (h == SIG_DFL)
        return UnhandledExceptionFilter(pointers);
    if (h == SIG_IGN)
        return EXCEPTION_CONTINUE_EXECUTION;
    if (sig == SIGFPE) {
        int fpe;
        switch (code) {
        case STATUS_FLOAT_DIVIDE_BY_ZERO:
            fpe = 0x83;
            break;
        case STATUS_FLOAT_INVALID_OPERATION:
            fpe = 0x81;
            break;
        case STATUS_FLOAT_OVERFLOW:
            fpe = 0x84;
            break;
        case STATUS_FLOAT_UNDERFLOW:
            fpe = 0x85;
            break;
        case STATUS_FLOAT_INEXACT_RESULT:
            fpe = 0x86;
            break;
        case STATUS_FLOAT_DENORMAL_OPERAND:
            fpe = 0x82;
            break;
        default:
            fpe = 0x8a;
            break;
        }
        MsvcrtThreadData()->fpecode = fpe;
        ((FPEHANDLER)(void *)h)(sig, fpe);
    } else {
        h(sig);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

/* ------------------------------------------------------------------ abort and assert */

#define _WRITE_ABORT_MSG 1
#define _CALL_REPORTFAULT 2

static unsigned int g_abort_behavior = _CALL_REPORTFAULT;

unsigned int __cdecl _set_abort_behavior(unsigned int flags, unsigned int mask)
{
    unsigned int old = g_abort_behavior;
    g_abort_behavior = (old & ~mask) | (flags & mask);
    return old;
}

void __cdecl abort(void)
{
    if (g_handlers[SIGABRT] != SIG_DFL)
        raise(SIGABRT);
    if (g_abort_behavior & _CALL_REPORTFAULT) {
        CONTEXT context;
        EXCEPTION_RECORD record;
        memset(&record, 0, sizeof(record));
        RtlCaptureContext(&context);
        record.ExceptionCode = STATUS_FATAL_APP_EXIT;
        record.ExceptionAddress = __builtin_return_address(0);
        EXCEPTION_POINTERS pointers = {&record, &context};
        SetUnhandledExceptionFilter(nullptr);
        UnhandledExceptionFilter(&pointers);
    }
    exit(3);
}

static int g_error_mode;

int __cdecl _set_error_mode(int mode)
{
    if (mode == 3)
        return g_error_mode;
    if (mode < 0 || mode > 2) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    int old = g_error_mode;
    g_error_mode = mode;
    return old;
}

int MsvcrtAppType(void);

typedef int(WINAPI *MESSAGEBOXW)(HWND, LPCWSTR, LPCWSTR, UINT);

static int message_box(const wchar_t *text, const wchar_t *caption, UINT type)
{
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    MESSAGEBOXW box = user32 ? (MESSAGEBOXW)(void *)GetProcAddress(user32, "MessageBoxW") : nullptr;
    if (!box)
        return 0;
    return box(nullptr, text, caption, type);
}

/* shortens s to at most max characters, keeping both ends around "...". */
static void append_short(wchar_t *dst, size_t size, const wchar_t *s, size_t max)
{
    size_t n = wcslen(s);
    if (n <= max) {
        wcscat_s(dst, size, s);
        return;
    }
    size_t head = (max - 3) / 2, tail = max - 3 - head;
    wcsncat_s(dst, size, s, head);
    wcscat_s(dst, size, L"...");
    wcscat_s(dst, size, s + n - tail);
}

void __cdecl _wassert(const wchar_t *message, const wchar_t *file, unsigned line)
{
    int mode = g_error_mode;
    if (mode == 1 || (mode == 0 && MsvcrtAppType() == 1)) {
        wchar_t text[540];
        _snwprintf(text, 540, L"Assertion failed: %s, file %s, line %d\n", message, file, line);
        text[539] = 0;
        HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
        DWORD written;
        if (h && h != INVALID_HANDLE_VALUE && GetFileType(h) == FILE_TYPE_CHAR &&
            WriteConsoleW(h, text, (DWORD)wcslen(text), &written, nullptr))
            abort();
        fwprintf(msvcrt_stderr, L"%s", text);
        fflush(msvcrt_stderr);
        abort();
    }
    wchar_t text[540] = L"Assertion failed!\n\nProgram: ";
    wchar_t program[MAX_PATH + 1];
    if (!GetModuleFileNameW(nullptr, program, MAX_PATH))
        wcscpy(program, L"<program name unknown>");
    append_short(text, 540, program, 60);
    wcscat_s(text, 540, L"\nFile: ");
    append_short(text, 540, file, 60);
    wchar_t num[16];
    _itow(line, num, 10);
    wcscat_s(text, 540, L"\nLine: ");
    wcscat_s(text, 540, num);
    wcscat_s(text, 540, L"\n\nExpression: ");
    append_short(text, 540, message, 300);
    wcscat_s(text, 540, L"\n\nFor information on how your program can cause an assertion\n"
                        L"failure, see the Visual C++ documentation on asserts\n\n"
                        L"(Press Retry to debug the application - JIT must be enabled)");
    int r = message_box(text, L"Microsoft Visual C++ Runtime Library", 0x12012 /* abort retry ignore, task modal. */);
    if (r == 3) {
        raise(SIGABRT);
        _exit(3);
    }
    if (r == 4) {
        DebugBreak();
        return;
    }
    if (r == 5)
        return;
    abort();
}

void __cdecl _assert(const char *message, const char *file, unsigned line)
{
    wchar_t *m = MsvcrtAToW(message ? message : "");
    wchar_t *f = MsvcrtAToW(file ? file : "");
    _wassert(m ? m : L"", f ? f : L"", line);
    free(m);
    free(f);
}

/* ------------------------------------------------------------------ random numbers */

/* msvcrt's linear congruential generator, per thread. */
void __cdecl srand(unsigned int seed)
{
    MsvcrtThreadData()->rand_next = seed;
}

int __cdecl rand(void)
{
    MSVCRT_THREAD_DATA *ptd = MsvcrtThreadData();
    ptd->rand_next = ptd->rand_next * 214013 + 2531011;
    return (ptd->rand_next >> 16) & 0x7fff;
}

typedef NTSTATUS(NTAPI *RTLGENRANDOM)(PVOID, ULONG);

errno_t __cdecl rand_s(unsigned int *value)
{
    if (!value) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    static RTLGENRANDOM gen;
    if (!gen) {
        HMODULE advapi = LoadLibraryW(L"advapi32.dll");
        gen = advapi ? (RTLGENRANDOM)(void *)GetProcAddress(advapi, "SystemFunction036") : nullptr;
    }
    if (gen && gen(value, sizeof(*value)))
        return 0;
    /* no random source: a mix of the clock and the generator. */
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    *value = (unsigned)(t.QuadPart ^ (t.QuadPart >> 29)) * 2654435761u ^ (unsigned)rand() << 15 ^ (unsigned)rand();
    return 0;
}

/* ------------------------------------------------------------------ sorting and searching */

typedef int(__cdecl *COMPARE)(const void *, const void *);
typedef int(__cdecl *COMPARE_S)(void *, const void *, const void *);

static void swap_bytes(char *a, char *b, size_t size)
{
    if (a == b)
        return;
    while (size >= 8) {
        uint64_t t;
        memcpy(&t, a, 8);
        memcpy(a, b, 8);
        memcpy(b, &t, 8);
        a += 8;
        b += 8;
        size -= 8;
    }
    while (size--) {
        char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

/* a comparison with or without a context, so one sort serves both forms. */
typedef struct {
    COMPARE cmp;
    COMPARE_S cmp_s;
    void *context;
} SORTCMP;

static int sort_cmp(const SORTCMP *c, const void *a, const void *b)
{
    return c->cmp ? c->cmp(a, b) : c->cmp_s(c->context, a, b);
}

static void insertion_sort(char *lo, char *hi, size_t size, const SORTCMP *c)
{
    for (char *p = lo + size; p <= hi; p += size)
        for (char *q = p; q > lo && sort_cmp(c, q - size, q) > 0; q -= size)
            swap_bytes(q - size, q, size);
}

static void sift_down(char *base, size_t start, size_t count, size_t size, const SORTCMP *c)
{
    size_t root = start;
    for (;;) {
        size_t child = 2 * root + 1;
        if (child >= count)
            break;
        if (child + 1 < count && sort_cmp(c, base + child * size, base + (child + 1) * size) < 0)
            child++;
        if (sort_cmp(c, base + root * size, base + child * size) >= 0)
            break;
        swap_bytes(base + root * size, base + child * size, size);
        root = child;
    }
}

static void heap_sort(char *base, size_t count, size_t size, const SORTCMP *c)
{
    for (size_t i = count / 2; i-- > 0;)
        sift_down(base, i, count, size, c);
    for (size_t end = count; end-- > 1;) {
        swap_bytes(base, base + end * size, size);
        sift_down(base, 0, end, size, c);
    }
}

/* quicksort with a median of three pivot, insertion sort for small parts, heap sort when too deep. */
static void quick_sort(char *lo, char *hi, size_t size, const SORTCMP *c, int depth)
{
    while (hi > lo) {
        size_t count = (size_t)(hi - lo) / size + 1;
        if (count <= 8) {
            insertion_sort(lo, hi, size, c);
            return;
        }
        if (depth-- <= 0) {
            heap_sort(lo, count, size, c);
            return;
        }
        char *mid = lo + (count / 2) * size;
        if (sort_cmp(c, lo, mid) > 0)
            swap_bytes(lo, mid, size);
        if (sort_cmp(c, lo, hi) > 0)
            swap_bytes(lo, hi, size);
        if (sort_cmp(c, mid, hi) > 0)
            swap_bytes(mid, hi, size);
        char *l = lo, *h = hi;
        for (;;) {
            do
                l += size;
            while (l < hi && sort_cmp(c, l, mid) < 0);
            do
                h -= size;
            while (h > lo && sort_cmp(c, h, mid) > 0);
            if (h < l)
                break;
            swap_bytes(l, h, size);
            if (mid == h)
                mid = l;
            else if (mid == l)
                mid = h;
        }
        /* recurses into the smaller side to bound the stack. */
        if (h - lo < hi - l) {
            quick_sort(lo, h, size, c, depth);
            lo = l;
        } else {
            quick_sort(l, hi, size, c, depth);
            hi = h;
        }
    }
}

static void sort(void *base, size_t count, size_t size, const SORTCMP *c)
{
    if (count < 2 || !size)
        return;
    int depth = 0;
    for (size_t n = count; n; n >>= 1)
        depth += 2;
    quick_sort(base, (char *)base + (count - 1) * size, size, c, depth);
}

void __cdecl qsort(void *base, size_t count, size_t size, COMPARE cmp)
{
    if ((!base && count) || !cmp) {
        MSVCRT_INVALID(EINVAL);
        return;
    }
    SORTCMP c = {cmp, nullptr, nullptr};
    sort(base, count, size, &c);
}

void __cdecl qsort_s(void *base, size_t count, size_t size, COMPARE_S cmp, void *context)
{
    if ((!base && count) || !cmp) {
        MSVCRT_INVALID(EINVAL);
        return;
    }
    SORTCMP c = {nullptr, cmp, context};
    sort(base, count, size, &c);
}

void *__cdecl bsearch(const void *key, const void *base, size_t count, size_t size, COMPARE cmp)
{
    if (!cmp || (!base && count)) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const char *p = (const char *)base + mid * size;
        int r = cmp(key, p);
        if (!r)
            return (void *)p;
        if (r < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return nullptr;
}

void *__cdecl bsearch_s(const void *key, const void *base, size_t count, size_t size, COMPARE_S cmp, void *context)
{
    if (!cmp || (!base && count)) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const char *p = (const char *)base + mid * size;
        int r = cmp(context, key, p);
        if (!r)
            return (void *)p;
        if (r < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return nullptr;
}

void *__cdecl _lfind(const void *key, const void *base, unsigned int *count, unsigned int size, COMPARE cmp)
{
    if (!count || !cmp || (!base && *count)) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    for (unsigned i = 0; i < *count; i++) {
        const char *p = (const char *)base + (size_t)i * size;
        if (!cmp(key, p))
            return (void *)p;
    }
    return nullptr;
}

void *__cdecl _lsearch(const void *key, void *base, unsigned int *count, unsigned int size, COMPARE cmp)
{
    void *p = _lfind(key, base, count, size, cmp);
    if (p || !count || !base)
        return p;
    p = (char *)base + (size_t)*count * size;
    memcpy(p, key, size);
    (*count)++;
    return p;
}

/* ------------------------------------------------------------------ leftovers */

double _HUGE = __builtin_inf();
int _fileinfo;
int __setlc_active;
int __unguarded_readlc_active;
void(__cdecl *_aexit_rtn)(int) = _exit;
unsigned char _mbctype[257];
unsigned char _mbcasemap[256];

errno_t __cdecl _get_fileinfo(int *value)
{
    if (!value) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *value = _fileinfo;
    return 0;
}

errno_t __cdecl _set_fileinfo(int value)
{
    _fileinfo = value;
    return 0;
}

int __cdecl ___setlc_active_func(void)
{
    return __setlc_active;
}

int *__cdecl ___unguarded_readlc_active_add_func(void)
{
    return &__unguarded_readlc_active;
}

int *__cdecl __daylight(void)
{
    extern int _daylight;
    return &_daylight;
}

long *__cdecl __dstbias(void)
{
    extern long _dstbias;
    return &_dstbias;
}

long *__cdecl __timezone(void)
{
    extern long _timezone;
    return &_timezone;
}

char **__cdecl __tzname(void)
{
    extern char *_tzname[2];
    return _tzname;
}

errno_t __cdecl _get_environ(char ***value)
{
    if (!value) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *value = MsvcrtEnviron;
    return 0;
}

errno_t __cdecl _get_wenviron(wchar_t ***value)
{
    if (!value) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    MsvcrtWenvInit();
    *value = MsvcrtWenviron;
    return 0;
}

errno_t __cdecl _strerror_s(char *buf, size_t size, const char *message)
{
    if (!buf || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    char *s = _strerror(message);
    size_t n = strlen(s);
    if (n >= size) {
        *buf = 0;
        MSVCRT_INVALID(ERANGE);
        return ERANGE;
    }
    memcpy(buf, s, n + 1);
    return 0;
}

errno_t __cdecl __wcserror_s(wchar_t *buf, size_t size, const wchar_t *message)
{
    if (!buf || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    wchar_t *s = __wcserror(message);
    size_t n = wcslen(s);
    if (n >= size) {
        *buf = 0;
        MSVCRT_INVALID(ERANGE);
        return ERANGE;
    }
    memcpy(buf, s, (n + 1) * sizeof(wchar_t));
    return 0;
}

void __cdecl _swab(char *src, char *dst, int n)
{
    if (!src || !dst || n < 0) {
        MSVCRT_INVALID(EINVAL);
        return;
    }
    for (int i = 0; i + 1 < n; i += 2) {
        char a = src[i], b = src[i + 1];
        dst[i] = b;
        dst[i + 1] = a;
    }
}

errno_t __cdecl _strset_s(char *s, size_t size, int c)
{
    if (!s || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t i = 0;
    for (; i < size && s[i]; i++)
        s[i] = (char)c;
    if (i == size) {
        *s = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    return 0;
}

errno_t __cdecl _strnset_s(char *s, size_t size, int c, size_t count)
{
    if (!s || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t i = 0;
    for (; i < size && i < count && s[i]; i++)
        s[i] = (char)c;
    if (i == size) {
        *s = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    return 0;
}

errno_t __cdecl _wcsset_s(wchar_t *s, size_t size, wchar_t c)
{
    if (!s || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t i = 0;
    for (; i < size && s[i]; i++)
        s[i] = c;
    if (i == size) {
        *s = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    return 0;
}

errno_t __cdecl _wcsnset_s(wchar_t *s, size_t size, wchar_t c, size_t count)
{
    if (!s || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    size_t i = 0;
    for (; i < size && i < count && s[i]; i++)
        s[i] = c;
    if (i == size) {
        *s = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    return 0;
}

size_t __cdecl __strncnt(const char *s, size_t n)
{
    return strnlen(s, n);
}

size_t __cdecl __wcsncnt(const wchar_t *s, size_t n)
{
    return wcsnlen(s, n);
}

void __cdecl _sleep(unsigned long ms)
{
    Sleep(ms);
}

int __cdecl _beep(unsigned frequency, unsigned duration)
{
    typedef BOOL(WINAPI * BEEP)(DWORD, DWORD);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    BEEP beep = k32 ? (BEEP)(void *)GetProcAddress(k32, "Beep") : nullptr;
    return beep ? beep(frequency, duration) : 0;
}

/* nothing to restore: mime's stacks have no guard page to reset. */
int __cdecl _resetstkoflw(void)
{
    return 1;
}

void __cdecl _seterrormode(int mode)
{
    SetErrorMode(mode);
}

static size_t g_sbh_threshold;

size_t __cdecl _get_sbh_threshold(void)
{
    return g_sbh_threshold;
}

int __cdecl _set_sbh_threshold(size_t size)
{
    if (size > 1016)
        return 0;
    g_sbh_threshold = size;
    return 1;
}

unsigned __cdecl _getsystime(struct tm *tm)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    if (tm) {
        tm->tm_sec = st.wSecond;
        tm->tm_min = st.wMinute;
        tm->tm_hour = st.wHour;
        tm->tm_mday = st.wDay;
        tm->tm_mon = st.wMonth - 1;
        tm->tm_year = st.wYear - 1900;
        tm->tm_wday = st.wDayOfWeek;
        tm->tm_yday = 0;
        static const short start[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
        int y = st.wYear;
        tm->tm_yday = start[st.wMonth - 1] + st.wDay - 1 +
                      (st.wMonth > 2 && ((y % 4 == 0 && y % 100) || y % 400 == 0));
        tm->tm_isdst = -1;
    }
    return st.wMilliseconds;
}

unsigned __cdecl _setsystime(struct tm *tm, unsigned ms)
{
    if (!tm)
        return ERROR_INVALID_PARAMETER;
    SYSTEMTIME st = {(WORD)(tm->tm_year + 1900), (WORD)(tm->tm_mon + 1), 0, (WORD)tm->tm_mday,
                     (WORD)tm->tm_hour, (WORD)tm->tm_min, (WORD)tm->tm_sec, (WORD)ms};
    if (!SetLocalTime(&st)) {
        DWORD err = GetLastError();
        MsvcrtDosMapError(err);
        return err;
    }
    return 0;
}
