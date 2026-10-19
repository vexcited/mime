/*
 * msvcrt internals shared by its source files: types of the c runtime
 * abi, per thread data, locks, the low io table and helpers.
 */
#ifndef MIME_MSVCRT_PRIVATE_H
#define MIME_MSVCRT_PRIVATE_H

#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>
#include <float.h>
#include <limits.h>

/* exports are listed in msvcrt.def. */
#define CRTAPI

typedef unsigned short wint_t;
typedef unsigned short wctype_t;
typedef long long __time64_t;
typedef long __time32_t;
typedef __time64_t time_t;
typedef long clock_t;
typedef unsigned int _dev_t;
typedef unsigned short _ino_t;
typedef long _off_t;
typedef long long fpos_t;
typedef int errno_t;
typedef size_t rsize_t;

#define WEOF ((wint_t)0xFFFF)
#define EOF (-1)

/* errno values. */
#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EBUSY 16
#define EEXIST 17
#define EXDEV 18
#define ENODEV 19
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENFILE 23
#define EMFILE 24
#define ENOTTY 25
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EMLINK 31
#define EPIPE 32
#define EDOM 33
#define ERANGE 34
#define EDEADLK 36
#define ENAMETOOLONG 38
#define ENOLCK 39
#define ENOSYS 40
#define ENOTEMPTY 41
#define EILSEQ 42
#define STRUNCATE 80

/* ------------------------------------------------------------------ stdio */

typedef struct _iobuf {
    char *_ptr;
    int _cnt;
    char *_base;
    int _flag;
    int _file;
    int _charbuf;
    int _bufsiz;
    char *_tmpfname;
} FILE;

#define _IOREAD 0x0001
#define _IOWRT 0x0002
#define _IONBF 0x0004
#define _IOMYBUF 0x0008
#define _IOEOF 0x0010
#define _IOERR 0x0020
#define _IOSTRG 0x0040
#define _IORW 0x0080
#define _IOYOURBUF 0x0100
#define _IOSETVBUF 0x0400
#define _IOFEOF 0x0800
#define _IOFLRTN 0x1000
#define _IOCTRLZ 0x2000
#define _IOCOMMIT 0x4000
#define _IOFBF 0x0000
#define _IOLBF 0x0040

#define _IOB_ENTRIES 20
#define _NSTREAM_MAX 2048
#define _INTERNAL_BUFSIZ 4096
#define _SMALL_BUFSIZ 512
#define BUFSIZ 512
#define FILENAME_MAX 260
#define L_tmpnam 14
#define TMP_MAX 32767

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* a stream beyond the static _iob array carries its own lock. */
typedef struct {
    FILE f;
    CRITICAL_SECTION lock;
} FILEX;

extern FILE MsvcrtIob[_IOB_ENTRIES];
#define msvcrt_stdin (&MsvcrtIob[0])
#define msvcrt_stdout (&MsvcrtIob[1])
#define msvcrt_stderr (&MsvcrtIob[2])

extern FILE **MsvcrtStreams;
extern int MsvcrtStreamCount;

void MsvcrtLockFile(FILE *f);
void MsvcrtUnlockFile(FILE *f);
int MsvcrtFlsbuf(int ch, FILE *f);
int MsvcrtFilbuf(FILE *f);
int MsvcrtFflushNolock(FILE *f);
int MsvcrtFcloseNolock(FILE *f);
void MsvcrtGetbuf(FILE *f);
int MsvcrtStbuf(FILE *f);
void MsvcrtFtbuf(int flag, FILE *f);
FILE *MsvcrtGetStream(void);
FILE *MsvcrtOpenFileW(const wchar_t *name, const wchar_t *mode, int share, FILE *f);
int MsvcrtFlushAll(int flush_only);
int MsvcrtFputwcNolock(wchar_t c, FILE *f);
wint_t MsvcrtFgetwcNolock(FILE *f);
void MsvcrtStdioInit(void);

/* ------------------------------------------------------------------ low io */

typedef struct {
    intptr_t osfhnd;
    unsigned char osfile;
    char pipech;
    unsigned char textmode;
    char pipech2[2];
    CRITICAL_SECTION lock;
    int lockinit;
} MSVCRT_IOINFO;

#define IOINFO_L2E 5
#define IOINFO_ARRAY_ELTS (1 << IOINFO_L2E)
#define IOINFO_ARRAYS 64
#define MSVCRT_MAX_FILES (IOINFO_ARRAYS * IOINFO_ARRAY_ELTS)

#define FOPEN 0x01
#define FEOFLAG 0x02
#define FCRLF 0x04
#define FPIPE 0x08
#define FNOINHERIT 0x10
#define FAPPEND 0x20
#define FDEV 0x40
#define FTEXT 0x80

/* MSVCRT_IOINFO.textmode. */
#define TM_ANSI 0
#define TM_UTF8 1
#define TM_UTF16LE 2

#define LF 10
#define CR 13
#define CTRLZ 26

#define _O_RDONLY 0x0000
#define _O_WRONLY 0x0001
#define _O_RDWR 0x0002
#define _O_APPEND 0x0008
#define _O_RANDOM 0x0010
#define _O_SEQUENTIAL 0x0020
#define _O_TEMPORARY 0x0040
#define _O_NOINHERIT 0x0080
#define _O_CREAT 0x0100
#define _O_TRUNC 0x0200
#define _O_EXCL 0x0400
#define _O_SHORT_LIVED 0x1000
#define _O_TEXT 0x4000
#define _O_BINARY 0x8000
#define _O_WTEXT 0x10000
#define _O_U16TEXT 0x20000
#define _O_U8TEXT 0x40000
#define _O_ACCMODE (_O_RDONLY | _O_WRONLY | _O_RDWR)

#define _SH_DENYRW 0x10
#define _SH_DENYWR 0x20
#define _SH_DENYRD 0x30
#define _SH_DENYNO 0x40

#define _S_IFMT 0xF000
#define _S_IFDIR 0x4000
#define _S_IFCHR 0x2000
#define _S_IFIFO 0x1000
#define _S_IFREG 0x8000
#define _S_IREAD 0x0100
#define _S_IWRITE 0x0080
#define _S_IEXEC 0x0040

extern MSVCRT_IOINFO *MsvcrtIoinfo[IOINFO_ARRAYS];
extern int MsvcrtNhandle;
extern MSVCRT_IOINFO MsvcrtBadIoinfo;
extern int MsvcrtUmask;

static inline MSVCRT_IOINFO *MsvcrtFd(int fd)
{
    if ((unsigned)fd >= MSVCRT_MAX_FILES || !MsvcrtIoinfo[fd >> IOINFO_L2E])
        return &MsvcrtBadIoinfo;
    return &MsvcrtIoinfo[fd >> IOINFO_L2E][fd & (IOINFO_ARRAY_ELTS - 1)];
}

/* nonzero when fd is a valid, open descriptor. */
static inline int MsvcrtFdValid(int fd)
{
    return fd >= 0 && fd < MsvcrtNhandle && (MsvcrtFd(fd)->osfile & FOPEN);
}

int MsvcrtAllocFd(void);
void MsvcrtSetOsfhnd(int fd, intptr_t handle);
void MsvcrtFreeFd(int fd);
void MsvcrtLockFd(int fd);
void MsvcrtUnlockFd(int fd);
int MsvcrtWriteNolock(int fd, const void *buf, unsigned count);
int MsvcrtReadNolock(int fd, void *buf, unsigned count);
long long MsvcrtLseekNolock(int fd, long long offset, int origin);
int MsvcrtCloseNolock(int fd);
int MsvcrtOpenW(const wchar_t *name, int oflag, int shflag, int pmode, int secure);
void MsvcrtIoInit(void);
int MsvcrtCommitNolock(int fd);

/* ------------------------------------------------------------------ per thread data */

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

typedef struct {
    int err;
    unsigned long doserr;
    unsigned int rand_next;
    char *strtok_next;
    wchar_t *wcstok_next;
    unsigned char *mbstok_next;
    struct tm tm;
    char asctime_buf[26];
    wchar_t wasctime_buf[26];
    char cvt_buf[350];
    char tmpnam_buf[FILENAME_MAX];
    wchar_t wtmpnam_buf[FILENAME_MAX];
    /* _beginthread bookkeeping. */
    HANDLE thread_handle;
    void *start;
    void *arg;
    int beginthread;
    /* signal action for sigfpe and the floating point state. */
    int fpecode;
    /* c++ exception handling, MSVCRT_EH_THREAD of cxx.h. */
    void *eh[48];
    /* the encoded handler of invalid parameters of this thread, which comes before the one of the process. */
    void *invalid_handler;
} MSVCRT_THREAD_DATA;

MSVCRT_THREAD_DATA *MsvcrtThreadData(void);
void *MsvcrtEhThread(void);
int MsvcrtSetNewMode(int mode);
void MsvcrtFreeThreadData(void);

/* ------------------------------------------------------------------ locks */

#define _SIGNAL_LOCK 0
#define _IOB_SCAN_LOCK 1
#define _TMPNAM_LOCK 2
#define _CONIO_LOCK 3
#define _HEAP_LOCK 4
#define _UNDNAME_LOCK 5
#define _TIME_LOCK 6
#define _ENV_LOCK 7
#define _EXIT_LOCK1 8
#define _POPEN_LOCK 9
#define _LOCKTAB_LOCK 10
#define _OSFHND_LOCK 11
#define _SETLOCALE_LOCK 12
#define _MB_CP_LOCK 13
#define _TYPEINFO_LOCK 14
#define _DEBUG_LOCK 15
#define _STREAM_LOCKS 16
#define _LAST_STREAM_LOCK (_STREAM_LOCKS + _IOB_ENTRIES - 1)
#define _TOTAL_LOCKS (_LAST_STREAM_LOCK + 1)

CRTAPI void __cdecl _lock(int n);
CRTAPI void __cdecl _unlock(int n);

/* ------------------------------------------------------------------ helpers */

CRTAPI int *__cdecl _errno(void);
CRTAPI unsigned long *__cdecl __doserrno(void);
void MsvcrtDosMapError(unsigned long oserr);
int MsvcrtErrnoFromOsError(unsigned long oserr);
void MsvcrtInvalidParameter(void);
void MsvcrtSetErrno(int e);

/* errno set to e after an invalid parameter report. */
#define MSVCRT_INVALID(e) (MsvcrtSetErrno(e), MsvcrtInvalidParameter())

CRTAPI void *__cdecl malloc(size_t size);
CRTAPI void *__cdecl calloc(size_t count, size_t size);
CRTAPI void *__cdecl realloc(void *p, size_t size);
CRTAPI void __cdecl free(void *p);
extern HANDLE MsvcrtHeap;

/* ansi and wide conversions with the c runtime code page, allocated with malloc. */
UINT MsvcrtCodePage(void);
wchar_t *MsvcrtAToW(const char *s);
char *MsvcrtWToA(const wchar_t *s);

/* the environment. */
extern char **MsvcrtEnviron;
extern wchar_t **MsvcrtWenviron;
extern char **MsvcrtInitenv;
extern wchar_t **MsvcrtWinitenv;
void MsvcrtEnvInit(void);
int MsvcrtSetEnvA(const char *option, int primary);
int MsvcrtSetEnvW(const wchar_t *option, int primary);
void MsvcrtWenvInit(void);

/* exit and onexit. */
typedef int(__cdecl *_onexit_t)(void);
typedef void(__cdecl *_PVFV)(void);
typedef int(__cdecl *_PIFV)(void);
CRTAPI DECLSPEC_NORETURN void __cdecl exit(int code);
CRTAPI DECLSPEC_NORETURN void __cdecl _exit(int code);
CRTAPI void __cdecl abort(void);
CRTAPI int __cdecl raise(int sig);

/* text formatting shared by printf and scanf. */
typedef struct {
    /* string or stream output. */
    FILE *stream;
    char *buf;
    wchar_t *wbuf;
    size_t size;
    size_t len;
    int count;
    int error;
} MSVCRT_OUTPUT;

int MsvcrtFormatA(FILE *stream, const char *fmt, va_list ap);
int MsvcrtFormatW(FILE *stream, const wchar_t *fmt, va_list ap);
int MsvcrtVsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int MsvcrtVsnwprintf(wchar_t *buf, size_t size, const wchar_t *fmt, va_list ap);
int MsvcrtScanA(FILE *stream, const char *str, size_t len, const char *fmt, va_list ap);
int MsvcrtScanExA(FILE *stream, const char *str, size_t len, const char *fmt, va_list ap, int secure);
int MsvcrtScanExW(FILE *stream, const wchar_t *str, size_t len, const wchar_t *fmt, va_list ap, int secure);
int MsvcrtFormatPositionalA(FILE *f, const char *fmt, va_list ap);
int MsvcrtFormatPositionalW(FILE *f, const wchar_t *fmt, va_list ap);
int MsvcrtVsprintfPositionalA(char *buf, size_t size, const char *fmt, va_list ap);
int MsvcrtVsprintfPositionalW(wchar_t *buf, size_t size, const wchar_t *fmt, va_list ap);
int MsvcrtScanW(FILE *stream, const wchar_t *str, size_t len, const wchar_t *fmt, va_list ap);

extern unsigned int MsvcrtOutputFormat;

/* decimal conversion of doubles: digits of the value, rounded, without leading zeros. */
typedef struct {
    int sign;
    int decpt; /* position of the decimal point relative to the digits. */
    int ndigits;
    char digits[800];
} MSVCRT_DECIMAL;

void MsvcrtDoubleToDecimal(double value, int max_digits, MSVCRT_DECIMAL *out);
/* whether a byte starts a double byte character in the current multibyte code page. */
int MsvcrtMbLead(unsigned c);
double MsvcrtStrtod(const char *s, char **end, int *range_error);
double MsvcrtWcstod(const wchar_t *s, wchar_t **end, int *range_error);

/* math error reporting through _matherr. */
#define _DOMAIN 1
#define _SING 2
#define _OVERFLOW 3
#define _UNDERFLOW 4
#define _TLOSS 5
#define _PLOSS 6
double MsvcrtMathError(int type, const char *name, double arg1, double arg2, double retval);

/* the exception flags of fpu.c, named as _statusfp does. */
#define MSVCRT_FE_INEXACT 0x01
#define MSVCRT_FE_UNDERFLOW 0x02
#define MSVCRT_FE_OVERFLOW 0x04
#define MSVCRT_FE_DIVBYZERO 0x08
#define MSVCRT_FE_INVALID 0x10
unsigned MsvcrtFpGetStatus(void);
void MsvcrtFpSetStatus(unsigned flags);
void MsvcrtFpRaise(unsigned flags);

/* ctype. */
#define _UPPER 0x1
#define _LOWER 0x2
#define _DIGIT 0x4
#define _SPACE 0x8
#define _PUNCT 0x10
#define _CONTROL 0x20
#define _BLANK 0x40
#define _HEX 0x80
#define _LEADBYTE 0x8000
#define _ALPHA (0x0100 | _UPPER | _LOWER)

extern const unsigned short MsvcrtCtype[257];
#define MSVCRT_CTYPE(c) (MsvcrtCtype[(unsigned char)(c) + 1])
unsigned short MsvcrtWctype(wint_t c);

static inline int MsvcrtIsSpace(int c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

static inline int MsvcrtIsDigit(int c)
{
    return c >= '0' && c <= '9';
}

static inline int MsvcrtToLower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static inline int MsvcrtToUpper(int c)
{
    return c >= 'a' && c <= 'z' ? c - 32 : c;
}

/* time. */
void MsvcrtTzset(void);
int MsvcrtIsInDst(const struct tm *tm);
__time64_t MsvcrtFileTimeToTime(const FILETIME *ft);
__time64_t MsvcrtLocalToTime(int year, int month, int day, int hour, int min, int sec, int dst);
extern long MsvcrtTimezone;
extern int MsvcrtDaylight;
extern long MsvcrtDstbias;
extern char *MsvcrtTzname[2];

/* misc. */
int MsvcrtGetDrive(void);
wchar_t *MsvcrtWfullpath(wchar_t *buf, const wchar_t *path, size_t size);
void MsvcrtConsoleTerm(void);
unsigned short MsvcrtAttribToMode(DWORD attributes, const wchar_t *name);


#ifndef STATUS_FATAL_APP_EXIT
#define STATUS_FATAL_APP_EXIT ((NTSTATUS)0x40000015)
#endif
#ifndef STATUS_FLOAT_DENORMAL_OPERAND
#define STATUS_FLOAT_DENORMAL_OPERAND ((NTSTATUS)0xC000008D)
#define STATUS_FLOAT_DIVIDE_BY_ZERO ((NTSTATUS)0xC000008E)
#define STATUS_FLOAT_INEXACT_RESULT ((NTSTATUS)0xC000008F)
#define STATUS_FLOAT_INVALID_OPERATION ((NTSTATUS)0xC0000090)
#define STATUS_FLOAT_OVERFLOW ((NTSTATUS)0xC0000091)
#define STATUS_FLOAT_UNDERFLOW ((NTSTATUS)0xC0000093)
#endif
#ifndef STATUS_FLOAT_STACK_CHECK
#define STATUS_FLOAT_STACK_CHECK ((NTSTATUS)0xC0000092)
#endif

/* ------------------------------------------------------------------ functions used across files */

int __cdecl _stricmp(const char *a, const char *b);
int __cdecl _strnicmp(const char *a, const char *b, size_t n);
int __cdecl _wcsicmp(const wchar_t *a, const wchar_t *b);
int __cdecl _wcsnicmp(const wchar_t *a, const wchar_t *b, size_t n);
char *__cdecl _strdup(const char *s);
wchar_t *__cdecl _wcsdup(const wchar_t *s);
size_t __cdecl strnlen(const char *s, size_t max);
size_t __cdecl wcsnlen(const wchar_t *s, size_t max);
char *__cdecl strncat(char *dst, const char *src, size_t n);
wchar_t *__cdecl wcsncat(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t *__cdecl wcspbrk(const wchar_t *s, const wchar_t *accept);
errno_t __cdecl wcscat_s(wchar_t *dst, size_t size, const wchar_t *src);
errno_t __cdecl wcsncat_s(wchar_t *dst, size_t size, const wchar_t *src, size_t count);
errno_t __cdecl strcpy_s(char *dst, size_t size, const char *src);
char *__cdecl _itoa(int value, char *buf, int radix);
wchar_t *__cdecl _itow(int value, wchar_t *buf, int radix);
long __cdecl atol(const char *s);
char *__cdecl getenv(const char *name);
wchar_t *__cdecl _wgetenv(const wchar_t *name);

size_t __cdecl _msize(void *p);
int __cdecl _snprintf(char *buf, size_t size, const char *fmt, ...);
int __cdecl _snwprintf(wchar_t *buf, size_t size, const wchar_t *fmt, ...);
int __cdecl fwprintf(FILE *f, const wchar_t *fmt, ...);
int __cdecl fflush(FILE *f);
int __cdecl _write(int fd, const void *buf, unsigned int count);
int __cdecl _read(int fd, void *buf, unsigned int count);
int __cdecl _close(int fd);
int __cdecl _commit(int fd);
int __cdecl _isatty(int fd);
long long __cdecl _lseeki64(int fd, long long offset, int origin);
intptr_t __cdecl _get_osfhandle(int fd);
int __cdecl _filbuf(FILE *f);
int __cdecl _access(const char *path, int mode);
int __cdecl _waccess(const wchar_t *path, int mode);
void __cdecl _endthread(void);
void __cdecl _endthreadex(unsigned code);
__time64_t __cdecl _time64(__time64_t *out);
void __cdecl qsort(void *base, size_t count, size_t size, int(__cdecl *cmp)(const void *, const void *));
extern int _fmode;
int __cdecl _dup(int fd);
int __cdecl _dup2(int fd, int fd2);
int __cdecl _pipe(int *fds, unsigned int size, int textmode);
int __cdecl fclose(FILE *f);
FILE *__cdecl _wfdopen(int fd, const wchar_t *mode);
char *__cdecl tmpnam(char *buf);
char *__cdecl _strerror(const char *message);
wchar_t *__cdecl __wcserror(const wchar_t *message);
wchar_t *__cdecl _wstrdate(wchar_t *buf);
wchar_t *__cdecl _wstrtime(wchar_t *buf);

#endif
