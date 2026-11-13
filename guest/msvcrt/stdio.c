/* buffered streams: msvcrt's FILE layer over the low io descriptors. */
#include "msvcrt_private.h"

FILE MsvcrtIob[_IOB_ENTRIES];
FILE **MsvcrtStreams;
int MsvcrtStreamCount;
extern int _commode;

static char g_stbuf[2][_INTERNAL_BUFSIZ];

#define inuse(f) ((f)->_flag & (_IOREAD | _IOWRT | _IORW))
#define anybuf(f) ((f)->_flag & (_IOMYBUF | _IONBF | _IOYOURBUF))
#define bigbuf(f) ((f)->_flag & (_IOMYBUF | _IOYOURBUF))

void MsvcrtStdioInit(void)
{
    MsvcrtStreamCount = 512;
    MsvcrtStreams = calloc(MsvcrtStreamCount, sizeof(FILE *));
    for (int i = 0; i < 3; i++) {
        FILE *f = &MsvcrtIob[i];
        f->_file = i;
        f->_flag = i == 0 ? _IOREAD : _IOWRT;
        if (MsvcrtStreams)
            MsvcrtStreams[i] = f;
        /* a missing standard handle gets descriptor -2, like msvcrt. */
        intptr_t h = MsvcrtFd(i)->osfhnd;
        if (h == -1 || h == -2 || !h)
            f->_file = -2;
    }
    for (int i = 3; i < _IOB_ENTRIES; i++) {
        MsvcrtIob[i]._file = -1;
        if (MsvcrtStreams)
            MsvcrtStreams[i] = &MsvcrtIob[i];
    }
}

FILE *__cdecl __iob_func(void)
{
    return MsvcrtIob;
}

FILE *__cdecl __p__iob(void)
{
    return MsvcrtIob;
}

/* ------------------------------------------------------------------ locks */

static int is_static(FILE *f)
{
    return f >= MsvcrtIob && f < MsvcrtIob + _IOB_ENTRIES;
}

void MsvcrtLockFile(FILE *f)
{
    if (is_static(f))
        _lock(_STREAM_LOCKS + (int)(f - MsvcrtIob));
    else
        EnterCriticalSection(&((FILEX *)f)->lock);
}

void MsvcrtUnlockFile(FILE *f)
{
    if (is_static(f))
        _unlock(_STREAM_LOCKS + (int)(f - MsvcrtIob));
    else
        LeaveCriticalSection(&((FILEX *)f)->lock);
}

void __cdecl _lock_file(FILE *f)
{
    MsvcrtLockFile(f);
}

void __cdecl _unlock_file(FILE *f)
{
    MsvcrtUnlockFile(f);
}

/* returns a free stream, locked, or NULL. */
FILE *MsvcrtGetStream(void)
{
    FILE *result = nullptr;
    _lock(_IOB_SCAN_LOCK);
    for (int i = 0; i < MsvcrtStreamCount && MsvcrtStreams; i++) {
        FILE *f = MsvcrtStreams[i];
        if (!f) {
            FILEX *x = calloc(1, sizeof(FILEX));
            if (!x)
                break;
            InitializeCriticalSection(&x->lock);
            MsvcrtStreams[i] = &x->f;
            f = &x->f;
            f->_file = -1;
        }
        if (!inuse(f)) {
            MsvcrtLockFile(f);
            if (inuse(f)) {
                MsvcrtUnlockFile(f);
                continue;
            }
            f->_ptr = f->_base = nullptr;
            f->_cnt = 0;
            f->_flag = 0;
            f->_file = -1;
            f->_tmpfname = nullptr;
            f->_bufsiz = 0;
            f->_charbuf = 0;
            result = f;
            break;
        }
    }
    _unlock(_IOB_SCAN_LOCK);
    if (!result)
        MsvcrtSetErrno(EMFILE);
    return result;
}

/* ------------------------------------------------------------------ buffers */

void MsvcrtGetbuf(FILE *f)
{
    f->_base = malloc(_INTERNAL_BUFSIZ);
    if (f->_base) {
        f->_flag |= _IOMYBUF;
        f->_bufsiz = _INTERNAL_BUFSIZ;
    } else {
        f->_flag |= _IONBF;
        f->_base = (char *)&f->_charbuf;
        f->_bufsiz = 2;
    }
    f->_ptr = f->_base;
    f->_cnt = 0;
}

/* gives unbuffered stdout or stderr on a terminal a temporary buffer for one printf. */
int MsvcrtStbuf(FILE *f)
{
    if (!_isatty(f->_file))
        return 0;
    int i;
    if (f == msvcrt_stdout)
        i = 0;
    else if (f == msvcrt_stderr)
        i = 1;
    else
        return 0;
    if (anybuf(f))
        return 0;
    f->_ptr = f->_base = g_stbuf[i];
    f->_cnt = f->_bufsiz = _INTERNAL_BUFSIZ;
    f->_flag |= _IOWRT | _IOYOURBUF | _IOFLRTN;
    return 1;
}

void MsvcrtFtbuf(int flag, FILE *f)
{
    if (flag && (f->_flag & _IOFLRTN)) {
        MsvcrtFflushNolock(f);
        f->_flag &= ~(_IOYOURBUF | _IOFLRTN);
        f->_bufsiz = 0;
        f->_ptr = f->_base = nullptr;
    }
}

int __cdecl _filbuf(FILE *f)
{
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    if (!inuse(f) || (f->_flag & _IOSTRG))
        return EOF;
    if (f->_flag & _IOWRT) {
        f->_flag |= _IOERR;
        return EOF;
    }
    f->_flag |= _IOREAD;
    if (!anybuf(f))
        MsvcrtGetbuf(f);
    else
        f->_ptr = f->_base;
    f->_cnt = _read(f->_file, f->_base, f->_bufsiz);
    if (f->_cnt <= 0) {
        f->_flag |= f->_cnt ? _IOERR : _IOEOF;
        f->_cnt = 0;
        return EOF;
    }
    if (!(f->_flag & (_IOWRT | _IORW)) && (MsvcrtFd(f->_file)->osfile & (FTEXT | FEOFLAG)) == (FTEXT | FEOFLAG))
        f->_flag |= _IOCTRLZ;
    if (f->_bufsiz == _SMALL_BUFSIZ && (f->_flag & _IOMYBUF) && !(f->_flag & _IOSETVBUF))
        f->_bufsiz = _INTERNAL_BUFSIZ;
    f->_cnt--;
    return (unsigned char)*f->_ptr++;
}

int MsvcrtFilbuf(FILE *f)
{
    return _filbuf(f);
}

int __cdecl _flsbuf(int ch, FILE *f)
{
    int fd = f->_file;
    if (!(f->_flag & (_IOWRT | _IORW))) {
        MsvcrtSetErrno(EBADF);
        f->_flag |= _IOERR;
        return EOF;
    }
    if (f->_flag & _IOSTRG) {
        MsvcrtSetErrno(ERANGE);
        f->_flag |= _IOERR;
        return EOF;
    }
    if (f->_flag & _IOREAD) {
        f->_cnt = 0;
        if (!(f->_flag & _IOEOF)) {
            f->_flag |= _IOERR;
            return EOF;
        }
        f->_ptr = f->_base;
        f->_flag &= ~_IOREAD;
    }
    f->_flag |= _IOWRT;
    f->_flag &= ~_IOEOF;
    f->_cnt = 0;
    if (!anybuf(f) && !((f == msvcrt_stdout || f == msvcrt_stderr) && _isatty(fd)))
        MsvcrtGetbuf(f);
    int n, written;
    if (bigbuf(f)) {
        n = (int)(f->_ptr - f->_base);
        f->_ptr = f->_base + 1;
        f->_cnt = f->_bufsiz - 1;
        written = 0;
        if (n > 0)
            written = _write(fd, f->_base, n);
        else if (MsvcrtFdValid(fd) && (MsvcrtFd(fd)->osfile & FAPPEND))
            _lseeki64(fd, 0, SEEK_END);
        *f->_base = (char)ch;
    } else {
        char c = (char)ch;
        n = 1;
        written = _write(fd, &c, 1);
    }
    if (written != n) {
        f->_flag |= _IOERR;
        return EOF;
    }
    return ch & 0xff;
}

int MsvcrtFlsbuf(int ch, FILE *f)
{
    return _flsbuf(ch, f);
}

/* ------------------------------------------------------------------ flushing */

static int flush_buffer(FILE *f)
{
    int r = 0;
    if ((f->_flag & (_IOREAD | _IOWRT)) == _IOWRT && bigbuf(f)) {
        int n = (int)(f->_ptr - f->_base);
        if (n > 0) {
            if (_write(f->_file, f->_base, n) == n) {
                if (f->_flag & _IORW)
                    f->_flag &= ~_IOWRT;
            } else {
                f->_flag |= _IOERR;
                r = EOF;
            }
        }
    }
    f->_ptr = f->_base;
    f->_cnt = 0;
    return r;
}

int MsvcrtFflushNolock(FILE *f)
{
    if (flush_buffer(f))
        return EOF;
    if (f->_flag & _IOCOMMIT)
        return _commit(f->_file) ? EOF : 0;
    return 0;
}

/* with flush_only, counts the streams flushed, else the open ones. */
int MsvcrtFlushAll(int flush_only)
{
    int count = 0, err = 0;
    _lock(_IOB_SCAN_LOCK);
    for (int i = 0; i < MsvcrtStreamCount && MsvcrtStreams; i++) {
        FILE *f = MsvcrtStreams[i];
        if (!f || !inuse(f))
            continue;
        MsvcrtLockFile(f);
        if (inuse(f)) {
            if (flush_only) {
                if (f->_flag & _IOWRT) {
                    if (MsvcrtFflushNolock(f) == EOF)
                        err = EOF;
                }
            } else if (MsvcrtFflushNolock(f) != EOF) {
                count++;
            }
        }
        MsvcrtUnlockFile(f);
    }
    _unlock(_IOB_SCAN_LOCK);
    return flush_only ? err : count;
}

int __cdecl fflush(FILE *f)
{
    if (!f)
        return MsvcrtFlushAll(1);
    MsvcrtLockFile(f);
    int r = MsvcrtFflushNolock(f);
    MsvcrtUnlockFile(f);
    return r;
}

int __cdecl _fflush_nolock(FILE *f)
{
    if (!f)
        return MsvcrtFlushAll(1);
    return MsvcrtFflushNolock(f);
}

int __cdecl _flushall(void)
{
    return MsvcrtFlushAll(0);
}

/* ------------------------------------------------------------------ open and close */

/* returns 0 for an invalid mode. */
static int parse_mode(const wchar_t *mode, int *oflag, int *sflag, int *share)
{
    int commit = _commode;
    while (*mode == ' ')
        mode++;
    switch (*mode) {
    case 'r':
        *oflag = _O_RDONLY;
        *sflag = _IOREAD;
        break;
    case 'w':
        *oflag = _O_WRONLY | _O_CREAT | _O_TRUNC;
        *sflag = _IOWRT;
        break;
    case 'a':
        *oflag = _O_WRONLY | _O_CREAT | _O_APPEND;
        *sflag = _IOWRT;
        break;
    default:
        return 0;
    }
    int text_set = 0, seq = 0, temp = 0, plus = 0, commit_set = 0;
    for (mode++; *mode; mode++) {
        switch (*mode) {
        case ' ':
            break;
        case '+':
            if (plus)
                return 0;
            plus = 1;
            *oflag = (*oflag & ~(_O_RDONLY | _O_WRONLY)) | _O_RDWR;
            *sflag = _IORW;
            break;
        case 'b':
            if (text_set)
                return 0;
            text_set = 1;
            *oflag |= _O_BINARY;
            break;
        case 't':
            if (text_set)
                return 0;
            text_set = 1;
            *oflag |= _O_TEXT;
            break;
        case 'c':
            if (commit_set)
                return 0;
            commit_set = 1;
            commit = _IOCOMMIT;
            break;
        case 'n':
            if (commit_set)
                return 0;
            commit_set = 1;
            commit = 0;
            break;
        case 'S':
        case 'R':
            if (seq)
                return 0;
            seq = 1;
            *oflag |= *mode == 'S' ? _O_SEQUENTIAL : _O_RANDOM;
            break;
        case 'T':
            if (*oflag & _O_SHORT_LIVED)
                return 0;
            *oflag |= _O_SHORT_LIVED;
            break;
        case 'D':
            if (temp)
                return 0;
            temp = 1;
            *oflag |= _O_TEMPORARY;
            break;
        case 'N':
            *oflag |= _O_NOINHERIT;
            break;
        case ',': {
            /* ccs=UTF-8, UTF-16LE or UNICODE. */
            mode++;
            while (*mode == ' ')
                mode++;
            if (wcsncmp(mode, L"ccs", 3))
                return 0;
            mode += 3;
            while (*mode == ' ')
                mode++;
            if (*mode++ != '=')
                return 0;
            while (*mode == ' ')
                mode++;
            if (!_wcsnicmp(mode, L"UTF-8", 5)) {
                *oflag |= _O_U8TEXT;
                mode += 5;
            } else if (!_wcsnicmp(mode, L"UTF-16LE", 8)) {
                *oflag |= _O_U16TEXT;
                mode += 8;
            } else if (!_wcsnicmp(mode, L"UNICODE", 7)) {
                *oflag |= _O_WTEXT;
                mode += 7;
            } else {
                return 0;
            }
            while (*mode == ' ')
                mode++;
            if (*mode)
                return 0;
            mode--;
            break;
        }
        default:
            return 0;
        }
    }
    *sflag |= commit;
    return 1;
}

static wchar_t *mode_to_w(const char *mode)
{
    size_t n = strlen(mode);
    wchar_t *w = malloc((n + 1) * sizeof(wchar_t));
    if (w)
        for (size_t i = 0; i <= n; i++)
            w[i] = (unsigned char)mode[i];
    return w;
}

FILE *MsvcrtOpenFileW(const wchar_t *name, const wchar_t *mode, int share, FILE *f)
{
    int oflag, sflag;
    if (!parse_mode(mode, &oflag, &sflag, &share)) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    int fd = MsvcrtOpenW(name, oflag, share, _S_IREAD | _S_IWRITE, 0);
    if (fd < 0)
        return nullptr;
    f->_flag = sflag;
    f->_cnt = 0;
    f->_ptr = f->_base = nullptr;
    f->_tmpfname = nullptr;
    f->_file = fd;
    return f;
}

static FILE *open_stream(const wchar_t *name, const wchar_t *mode, int share)
{
    if (!name || !mode || !*mode) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    FILE *f = MsvcrtGetStream();
    if (!f)
        return nullptr;
    FILE *r = nullptr;
    /* an empty name fails as a file that is not there does, with no call of the invalid parameter handler. */
    if (*name)
        r = MsvcrtOpenFileW(name, mode, share, f);
    else
        MsvcrtSetErrno(EINVAL);
    MsvcrtUnlockFile(f);
    return r;
}

wchar_t *MsvcrtPathToW(const char *path);

FILE *__cdecl _wfsopen(const wchar_t *name, const wchar_t *mode, int share)
{
    return open_stream(name, mode, share);
}

FILE *__cdecl _fsopen(const char *name, const char *mode, int share)
{
    if (!name || !mode) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    wchar_t *wn = MsvcrtPathToW(name);
    wchar_t *wm = mode_to_w(mode);
    FILE *f = wn && wm ? open_stream(wn, wm, share) : nullptr;
    free(wn);
    free(wm);
    return f;
}

FILE *__cdecl fopen(const char *name, const char *mode)
{
    return _fsopen(name, mode, _SH_DENYNO);
}

FILE *__cdecl _wfopen(const wchar_t *name, const wchar_t *mode)
{
    return open_stream(name, mode, _SH_DENYNO);
}

errno_t __cdecl fopen_s(FILE **pf, const char *name, const char *mode)
{
    if (!pf) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *pf = _fsopen(name, mode, 0x50);
    return *pf ? 0 : *_errno();
}

errno_t __cdecl _wfopen_s(FILE **pf, const wchar_t *name, const wchar_t *mode)
{
    if (!pf) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *pf = _wfsopen(name, mode, 0x50);
    return *pf ? 0 : *_errno();
}

int MsvcrtFcloseNolock(FILE *f)
{
    int r = EOF;
    if (inuse(f)) {
        r = MsvcrtFflushNolock(f);
        if (f->_flag & _IOMYBUF) {
            free(f->_base);
            f->_flag &= ~_IOMYBUF;
            f->_base = f->_ptr = nullptr;
            f->_bufsiz = 0;
        }
        if (_close(f->_file) < 0)
            r = EOF;
        if (f->_tmpfname) {
            free(f->_tmpfname);
            f->_tmpfname = nullptr;
        }
    }
    f->_flag = 0;
    return r;
}

int __cdecl fclose(FILE *f)
{
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    if (f->_flag & _IOSTRG) {
        f->_flag = 0;
        return EOF;
    }
    MsvcrtLockFile(f);
    int r = MsvcrtFcloseNolock(f);
    MsvcrtUnlockFile(f);
    return r;
}

int __cdecl _fcloseall(void)
{
    int count = 0;
    _lock(_IOB_SCAN_LOCK);
    for (int i = 3; i < MsvcrtStreamCount && MsvcrtStreams; i++) {
        FILE *f = MsvcrtStreams[i];
        if (f && inuse(f) && fclose(f) != EOF)
            count++;
    }
    _unlock(_IOB_SCAN_LOCK);
    return count;
}

static FILE *reopen(const wchar_t *name, const wchar_t *mode, FILE *f, int share)
{
    if (!name || !mode || !f) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    MsvcrtLockFile(f);
    if (inuse(f))
        MsvcrtFcloseNolock(f);
    f->_ptr = f->_base = nullptr;
    f->_cnt = f->_flag = 0;
    FILE *r = MsvcrtOpenFileW(name, mode, share, f);
    MsvcrtUnlockFile(f);
    return r;
}

FILE *__cdecl _wfreopen(const wchar_t *name, const wchar_t *mode, FILE *f)
{
    return reopen(name, mode, f, _SH_DENYNO);
}

FILE *__cdecl freopen(const char *name, const char *mode, FILE *f)
{
    if (!name || !mode || !f) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    wchar_t *wn = MsvcrtPathToW(name);
    wchar_t *wm = mode_to_w(mode);
    FILE *r = wn && wm ? reopen(wn, wm, f, _SH_DENYNO) : nullptr;
    free(wn);
    free(wm);
    return r;
}

errno_t __cdecl freopen_s(FILE **pf, const char *name, const char *mode, FILE *f)
{
    if (!pf) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *pf = freopen(name, mode, f);
    return *pf ? 0 : *_errno();
}

errno_t __cdecl _wfreopen_s(FILE **pf, const wchar_t *name, const wchar_t *mode, FILE *f)
{
    if (!pf) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *pf = _wfreopen(name, mode, f);
    return *pf ? 0 : *_errno();
}

static FILE *fdopen_w(int fd, const wchar_t *mode)
{
    if (!mode) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    if (!MsvcrtFdValid(fd)) {
        MSVCRT_INVALID(EBADF);
        return nullptr;
    }
    int oflag, sflag, share = 0;
    if (!parse_mode(mode, &oflag, &sflag, &share)) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    FILE *f = MsvcrtGetStream();
    if (!f)
        return nullptr;
    f->_flag = sflag;
    f->_file = fd;
    MsvcrtUnlockFile(f);
    return f;
}

FILE *__cdecl _fdopen(int fd, const char *mode)
{
    if (!mode) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    wchar_t *wm = mode_to_w(mode);
    FILE *f = wm ? fdopen_w(fd, wm) : nullptr;
    free(wm);
    return f;
}

FILE *__cdecl _wfdopen(int fd, const wchar_t *mode)
{
    return fdopen_w(fd, mode);
}

int __cdecl _fileno(FILE *f)
{
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    return f->_file;
}

/* ------------------------------------------------------------------ characters */

int __cdecl _fgetc_nolock(FILE *f)
{
    return --f->_cnt >= 0 ? (unsigned char)*f->_ptr++ : _filbuf(f);
}

int __cdecl _fputc_nolock(int c, FILE *f)
{
    return --f->_cnt >= 0 ? (unsigned char)(*f->_ptr++ = (char)c) : _flsbuf(c, f);
}

int __cdecl fgetc(FILE *f)
{
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    MsvcrtLockFile(f);
    int r = _fgetc_nolock(f);
    MsvcrtUnlockFile(f);
    return r;
}

int __cdecl getc(FILE *f)
{
    return fgetc(f);
}

int __cdecl _fgetchar(void)
{
    return fgetc(msvcrt_stdin);
}

int __cdecl getchar(void)
{
    return fgetc(msvcrt_stdin);
}

int __cdecl fputc(int c, FILE *f)
{
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    MsvcrtLockFile(f);
    int r = _fputc_nolock(c, f);
    MsvcrtUnlockFile(f);
    return r;
}

int __cdecl putc(int c, FILE *f)
{
    return fputc(c, f);
}

int __cdecl _fputchar(int c)
{
    return fputc(c, msvcrt_stdout);
}

int __cdecl putchar(int c)
{
    return fputc(c, msvcrt_stdout);
}

int __cdecl _ungetc_nolock(int c, FILE *f)
{
    if (c == EOF)
        return EOF;
    if (!(f->_flag & _IOREAD) && !((f->_flag & _IORW) && !(f->_flag & _IOWRT)))
        return EOF;
    if (!f->_base)
        MsvcrtGetbuf(f);
    if (f->_ptr == f->_base) {
        if (f->_cnt)
            return EOF;
        f->_ptr++;
    }
    if (f->_flag & _IOSTRG) {
        if (*--f->_ptr != (char)c) {
            f->_ptr++;
            return EOF;
        }
    } else {
        *--f->_ptr = (char)c;
    }
    f->_cnt++;
    f->_flag &= ~_IOEOF;
    f->_flag |= _IOREAD;
    return c & 0xff;
}

int __cdecl ungetc(int c, FILE *f)
{
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    MsvcrtLockFile(f);
    int r = _ungetc_nolock(c, f);
    MsvcrtUnlockFile(f);
    return r;
}

int __cdecl _getw(FILE *f)
{
    int v;
    char *p = (char *)&v;
    MsvcrtLockFile(f);
    for (size_t i = 0; i < sizeof(v); i++) {
        int c = _fgetc_nolock(f);
        if (c == EOF) {
            MsvcrtUnlockFile(f);
            return EOF;
        }
        p[i] = (char)c;
    }
    MsvcrtUnlockFile(f);
    return v;
}

int __cdecl _putw(int v, FILE *f)
{
    const char *p = (const char *)&v;
    MsvcrtLockFile(f);
    for (size_t i = 0; i < sizeof(v); i++) {
        if (_fputc_nolock((unsigned char)p[i], f) == EOF) {
            MsvcrtUnlockFile(f);
            return EOF;
        }
    }
    MsvcrtUnlockFile(f);
    return v;
}

/* ------------------------------------------------------------------ wide characters */

static int stream_wide_raw(FILE *f)
{
    if (f->_flag & _IOSTRG)
        return 1;
    MSVCRT_IOINFO *io = MsvcrtFd(f->_file);
    return !(io->osfile & FTEXT) || io->textmode != TM_ANSI;
}

wint_t MsvcrtFgetwcNolock(FILE *f)
{
    if (stream_wide_raw(f)) {
        int lo = _fgetc_nolock(f);
        if (lo == EOF)
            return WEOF;
        int hi = _fgetc_nolock(f);
        if (hi == EOF)
            return WEOF;
        return (wint_t)(lo | (hi << 8));
    }
    /* the c locale maps bytes to the same character values. */
    int c = _fgetc_nolock(f);
    return c == EOF ? WEOF : (wint_t)c;
}

int MsvcrtFputwcNolock(wchar_t c, FILE *f)
{
    if (stream_wide_raw(f)) {
        if (_fputc_nolock(c & 0xff, f) == EOF || _fputc_nolock(c >> 8, f) == EOF)
            return WEOF;
        return c;
    }
    if (c > 0xff) {
        MsvcrtSetErrno(EILSEQ);
        return WEOF;
    }
    return _fputc_nolock(c, f) == EOF ? WEOF : c;
}

wint_t __cdecl _fgetwc_nolock(FILE *f)
{
    return MsvcrtFgetwcNolock(f);
}

wint_t __cdecl _fputwc_nolock(wchar_t c, FILE *f)
{
    return (wint_t)MsvcrtFputwcNolock(c, f);
}

wint_t __cdecl fgetwc(FILE *f)
{
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return WEOF;
    }
    MsvcrtLockFile(f);
    wint_t r = MsvcrtFgetwcNolock(f);
    MsvcrtUnlockFile(f);
    return r;
}

wint_t __cdecl getwc(FILE *f)
{
    return fgetwc(f);
}

wint_t __cdecl _fgetwchar(void)
{
    return fgetwc(msvcrt_stdin);
}

wint_t __cdecl getwchar(void)
{
    return fgetwc(msvcrt_stdin);
}

wint_t __cdecl fputwc(wchar_t c, FILE *f)
{
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return WEOF;
    }
    MsvcrtLockFile(f);
    wint_t r = (wint_t)MsvcrtFputwcNolock(c, f);
    MsvcrtUnlockFile(f);
    return r;
}

wint_t __cdecl putwc(wchar_t c, FILE *f)
{
    return fputwc(c, f);
}

wint_t __cdecl _fputwchar(wchar_t c)
{
    return fputwc(c, msvcrt_stdout);
}

wint_t __cdecl putwchar(wchar_t c)
{
    return fputwc(c, msvcrt_stdout);
}

wint_t __cdecl ungetwc(wint_t c, FILE *f)
{
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return WEOF;
    }
    if (c == WEOF)
        return WEOF;
    MsvcrtLockFile(f);
    wint_t r = c;
    if (stream_wide_raw(f)) {
        if (_ungetc_nolock(c >> 8, f) == EOF)
            r = WEOF;
        else if (_ungetc_nolock(c & 0xff, f) == EOF)
            r = WEOF;
    } else if (c > 0xff || _ungetc_nolock(c, f) == EOF) {
        r = WEOF;
    }
    MsvcrtUnlockFile(f);
    return r;
}

/* ------------------------------------------------------------------ strings */

char *__cdecl fgets(char *s, int n, FILE *f)
{
    if (!s || n <= 0 || !f) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    MsvcrtLockFile(f);
    char *p = s;
    char *r = s;
    while (--n > 0) {
        int c = _fgetc_nolock(f);
        if (c == EOF) {
            if (p == s)
                r = nullptr;
            break;
        }
        *p++ = (char)c;
        if (c == '\n')
            break;
    }
    *p = 0;
    MsvcrtUnlockFile(f);
    return r;
}

wchar_t *__cdecl fgetws(wchar_t *s, int n, FILE *f)
{
    if (!s || n <= 0 || !f) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    MsvcrtLockFile(f);
    wchar_t *p = s;
    wchar_t *r = s;
    while (--n > 0) {
        wint_t c = MsvcrtFgetwcNolock(f);
        if (c == WEOF) {
            if (p == s)
                r = nullptr;
            break;
        }
        *p++ = c;
        if (c == '\n')
            break;
    }
    *p = 0;
    MsvcrtUnlockFile(f);
    return r;
}

char *__cdecl gets(char *s)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    FILE *f = msvcrt_stdin;
    MsvcrtLockFile(f);
    char *p = s;
    int c;
    while ((c = _fgetc_nolock(f)) != '\n') {
        if (c == EOF) {
            if (p == s) {
                MsvcrtUnlockFile(f);
                return nullptr;
            }
            break;
        }
        *p++ = (char)c;
    }
    *p = 0;
    MsvcrtUnlockFile(f);
    return s;
}

wchar_t *__cdecl _getws(wchar_t *s)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return nullptr;
    }
    FILE *f = msvcrt_stdin;
    MsvcrtLockFile(f);
    wchar_t *p = s;
    wint_t c;
    while ((c = MsvcrtFgetwcNolock(f)) != '\n') {
        if (c == WEOF) {
            if (p == s) {
                MsvcrtUnlockFile(f);
                return nullptr;
            }
            break;
        }
        *p++ = c;
    }
    *p = 0;
    MsvcrtUnlockFile(f);
    return s;
}

size_t __cdecl fwrite(const void *buf, size_t size, size_t count, FILE *f);

int __cdecl fputs(const char *s, FILE *f)
{
    if (!s || !f) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    size_t n = strlen(s);
    MsvcrtLockFile(f);
    int buffing = MsvcrtStbuf(f);
    size_t written = fwrite(s, 1, n, f);
    MsvcrtFtbuf(buffing, f);
    MsvcrtUnlockFile(f);
    return written == n ? 0 : EOF;
}

int __cdecl fputws(const wchar_t *s, FILE *f)
{
    if (!s || !f) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    MsvcrtLockFile(f);
    int buffing = MsvcrtStbuf(f);
    int r = 0;
    for (; *s; s++)
        if (MsvcrtFputwcNolock(*s, f) == WEOF) {
            r = EOF;
            break;
        }
    MsvcrtFtbuf(buffing, f);
    MsvcrtUnlockFile(f);
    return r;
}

int __cdecl puts(const char *s)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    FILE *f = msvcrt_stdout;
    size_t n = strlen(s);
    MsvcrtLockFile(f);
    int buffing = MsvcrtStbuf(f);
    int r = 0;
    if (fwrite(s, 1, n, f) != n || _fputc_nolock('\n', f) == EOF)
        r = EOF;
    MsvcrtFtbuf(buffing, f);
    MsvcrtUnlockFile(f);
    return r;
}

int __cdecl _putws(const wchar_t *s)
{
    if (!s) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    FILE *f = msvcrt_stdout;
    MsvcrtLockFile(f);
    int buffing = MsvcrtStbuf(f);
    int r = 0;
    for (; *s && !r; s++)
        if (MsvcrtFputwcNolock(*s, f) == WEOF)
            r = EOF;
    if (!r && MsvcrtFputwcNolock('\n', f) == WEOF)
        r = EOF;
    MsvcrtFtbuf(buffing, f);
    MsvcrtUnlockFile(f);
    return r;
}

/* ------------------------------------------------------------------ blocks */

size_t __cdecl _fread_nolock(void *buf, size_t size, size_t count, FILE *f)
{
    if (!size || !count)
        return 0;
    if (!buf || !f || count > (size_t)-1 / size) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    size_t total = size * count, left = total;
    char *p = buf;
    unsigned bufsize = bigbuf(f) ? (unsigned)f->_bufsiz : _INTERNAL_BUFSIZ;
    while (left) {
        if (bigbuf(f) && f->_cnt) {
            size_t n = (size_t)f->_cnt < left ? (size_t)f->_cnt : left;
            memcpy(p, f->_ptr, n);
            left -= n;
            f->_cnt -= (int)n;
            f->_ptr += n;
            p += n;
        } else if (left >= bufsize) {
            /* whole buffers go straight into the caller's memory. */
            unsigned n = bufsize ? (unsigned)(left - left % bufsize) : (unsigned)left;
            if (n > INT_MAX)
                n = INT_MAX - INT_MAX % (bufsize ? bufsize : 1);
            int got = _read(f->_file, p, n);
            if (got == 0) {
                f->_flag |= _IOEOF;
                return (total - left) / size;
            }
            if (got < 0) {
                f->_flag |= _IOERR;
                return (total - left) / size;
            }
            left -= got;
            p += got;
        } else {
            int c = _filbuf(f);
            if (c == EOF)
                return (total - left) / size;
            *p++ = (char)c;
            left--;
            bufsize = (unsigned)f->_bufsiz;
        }
    }
    return count;
}

size_t __cdecl fread(void *buf, size_t size, size_t count, FILE *f)
{
    if (!size || !count)
        return 0;
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    MsvcrtLockFile(f);
    size_t r = _fread_nolock(buf, size, count, f);
    MsvcrtUnlockFile(f);
    return r;
}

errno_t __cdecl fread_s(void *buf, size_t bufsize, size_t size, size_t count, FILE *f)
{
    if (size && count > bufsize / size) {
        MSVCRT_INVALID(ERANGE);
        return 0;
    }
    return (errno_t)fread(buf, size, count, f);
}

size_t __cdecl _fwrite_nolock(const void *buf, size_t size, size_t count, FILE *f)
{
    if (!size || !count)
        return 0;
    if (!buf || !f || count > (size_t)-1 / size) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    size_t total = size * count, left = total;
    const char *p = buf;
    unsigned bufsize = bigbuf(f) ? (unsigned)f->_bufsiz : _INTERNAL_BUFSIZ;
    while (left) {
        if (bigbuf(f) && f->_cnt) {
            size_t n = (size_t)f->_cnt < left ? (size_t)f->_cnt : left;
            memcpy(f->_ptr, p, n);
            left -= n;
            f->_cnt -= (int)n;
            f->_ptr += n;
            p += n;
        } else if (left >= bufsize) {
            if (bigbuf(f) && MsvcrtFflushNolock(f))
                return (total - left) / size;
            unsigned n = bufsize ? (unsigned)(left - left % bufsize) : (unsigned)left;
            if (n > INT_MAX)
                n = INT_MAX - INT_MAX % (bufsize ? bufsize : 1);
            int written = _write(f->_file, p, n);
            if (written < 0) {
                f->_flag |= _IOERR;
                return (total - left) / size;
            }
            left -= written;
            p += written;
            if ((unsigned)written < n) {
                f->_flag |= _IOERR;
                return (total - left) / size;
            }
        } else {
            if (_flsbuf((unsigned char)*p, f) == EOF)
                return (total - left) / size;
            p++;
            left--;
            bufsize = f->_bufsiz > 0 ? (unsigned)f->_bufsiz : 1;
        }
    }
    return count;
}

size_t __cdecl fwrite(const void *buf, size_t size, size_t count, FILE *f)
{
    if (!size || !count)
        return 0;
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    MsvcrtLockFile(f);
    size_t r = _fwrite_nolock(buf, size, count, f);
    MsvcrtUnlockFile(f);
    return r;
}

/* ------------------------------------------------------------------ positioning */

long long __cdecl _ftelli64_nolock(FILE *f)
{
    int fd = f->_file;
    if (f->_cnt < 0)
        f->_cnt = 0;
    long long filepos = _lseeki64(fd, 0, SEEK_CUR);
    if (filepos < 0)
        return -1;
    if (!bigbuf(f))
        return filepos - f->_cnt;
    MSVCRT_IOINFO *io = MsvcrtFd(fd);
    long long offset = f->_ptr - f->_base;
    if (f->_flag & (_IOWRT | _IOREAD)) {
        if (io->osfile & FTEXT) {
            if (io->textmode == TM_ANSI) {
                for (char *p = f->_base; p < f->_ptr; p++)
                    if (*p == '\n')
                        offset++;
            }
        }
    } else if (!(f->_flag & _IORW)) {
        MsvcrtSetErrno(EINVAL);
        return -1;
    }
    if (!filepos)
        return offset;
    if (f->_flag & _IOREAD) {
        if (!f->_cnt) {
            offset = 0;
        } else {
            long long rdcnt = f->_cnt + (f->_ptr - f->_base);
            if ((io->osfile & FTEXT) && io->textmode == TM_ANSI) {
                if (_lseeki64(fd, 0, SEEK_END) == filepos) {
                    char *end = f->_base + rdcnt;
                    for (char *p = f->_base; p < end; p++)
                        if (*p == '\n')
                            rdcnt++;
                    if (f->_flag & _IOCTRLZ)
                        rdcnt++;
                } else {
                    if (_lseeki64(fd, filepos, SEEK_SET) < 0)
                        return -1;
                    if (rdcnt <= _SMALL_BUFSIZ && (f->_flag & _IOMYBUF) && !(f->_flag & _IOSETVBUF))
                        rdcnt = _SMALL_BUFSIZ;
                    else
                        rdcnt = f->_bufsiz;
                    if (io->osfile & FCRLF)
                        rdcnt++;
                }
            }
            filepos -= rdcnt;
        }
    }
    return filepos + offset;
}

long long __cdecl _ftelli64(FILE *f)
{
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    MsvcrtLockFile(f);
    long long r = _ftelli64_nolock(f);
    MsvcrtUnlockFile(f);
    return r;
}

long __cdecl ftell(FILE *f)
{
    long long r = _ftelli64(f);
    if (r > LONG_MAX) {
        MsvcrtSetErrno(EINVAL);
        return -1;
    }
    return (long)r;
}

long __cdecl _ftell_nolock(FILE *f)
{
    return (long)_ftelli64_nolock(f);
}

int __cdecl _fseeki64_nolock(FILE *f, long long offset, int origin)
{
    if (!inuse(f) || (origin != SEEK_SET && origin != SEEK_CUR && origin != SEEK_END)) {
        MsvcrtSetErrno(EINVAL);
        return -1;
    }
    f->_flag &= ~_IOEOF;
    if (origin == SEEK_CUR) {
        long long here = _ftelli64_nolock(f);
        if (here < 0)
            return -1;
        offset += here;
        origin = SEEK_SET;
    }
    MsvcrtFflushNolock(f);
    if (f->_flag & _IORW)
        f->_flag &= ~(_IOWRT | _IOREAD);
    else if ((f->_flag & _IOREAD) && (f->_flag & _IOMYBUF) && !(f->_flag & _IOSETVBUF))
        f->_bufsiz = _SMALL_BUFSIZ;
    return _lseeki64(f->_file, offset, origin) < 0 ? -1 : 0;
}

int __cdecl _fseeki64(FILE *f, long long offset, int origin)
{
    if (!f || (origin != SEEK_SET && origin != SEEK_CUR && origin != SEEK_END)) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    MsvcrtLockFile(f);
    int r = _fseeki64_nolock(f, offset, origin);
    MsvcrtUnlockFile(f);
    return r;
}

int __cdecl fseek(FILE *f, long offset, int origin)
{
    return _fseeki64(f, offset, origin);
}

int __cdecl _fseek_nolock(FILE *f, long offset, int origin)
{
    return _fseeki64_nolock(f, offset, origin);
}

void __cdecl rewind(FILE *f)
{
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return;
    }
    MsvcrtLockFile(f);
    MsvcrtFflushNolock(f);
    f->_flag &= ~(_IOERR | _IOEOF);
    if (f->_flag & _IORW)
        f->_flag &= ~(_IOREAD | _IOWRT);
    if (MsvcrtFdValid(f->_file)) {
        MsvcrtFd(f->_file)->osfile &= ~FEOFLAG;
        _lseeki64(f->_file, 0, SEEK_SET);
    }
    MsvcrtUnlockFile(f);
}

int __cdecl fgetpos(FILE *f, fpos_t *pos)
{
    if (!f || !pos) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    *pos = _ftelli64(f);
    return *pos == -1 ? -1 : 0;
}

int __cdecl fsetpos(FILE *f, const fpos_t *pos)
{
    if (!f || !pos) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    return _fseeki64(f, *pos, SEEK_SET);
}

int __cdecl feof(FILE *f)
{
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    return f->_flag & _IOEOF;
}

int __cdecl ferror(FILE *f)
{
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    return f->_flag & _IOERR;
}

void __cdecl clearerr(FILE *f)
{
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return;
    }
    MsvcrtLockFile(f);
    f->_flag &= ~(_IOERR | _IOEOF);
    if (MsvcrtFdValid(f->_file))
        MsvcrtFd(f->_file)->osfile &= ~FEOFLAG;
    MsvcrtUnlockFile(f);
}

errno_t __cdecl clearerr_s(FILE *f)
{
    if (!f) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    clearerr(f);
    return 0;
}

int __cdecl setvbuf(FILE *f, char *buf, int mode, size_t size)
{
    if (!f || (mode != _IONBF && (size < 2 || size > INT_MAX)) ||
        (mode != _IOFBF && mode != _IOLBF && mode != _IONBF)) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    size &= ~(size_t)1;
    MsvcrtLockFile(f);
    MsvcrtFflushNolock(f);
    if (f->_flag & _IOMYBUF)
        free(f->_base);
    f->_flag &= ~(_IOMYBUF | _IOYOURBUF | _IONBF | _IOSETVBUF | _IOFEOF | _IOFLRTN | _IOCTRLZ);
    if (mode == _IONBF) {
        f->_flag |= _IONBF;
        f->_base = (char *)&f->_charbuf;
        f->_bufsiz = 2;
    } else if (!buf) {
        buf = malloc(size);
        if (!buf) {
            MsvcrtUnlockFile(f);
            return -1;
        }
        f->_flag |= _IOMYBUF | _IOSETVBUF;
        f->_base = buf;
        f->_bufsiz = (int)size;
    } else {
        f->_flag |= _IOYOURBUF | _IOSETVBUF;
        f->_base = buf;
        f->_bufsiz = (int)size;
    }
    f->_ptr = f->_base;
    f->_cnt = 0;
    MsvcrtUnlockFile(f);
    return 0;
}

void __cdecl setbuf(FILE *f, char *buf)
{
    if (buf)
        setvbuf(f, buf, _IOFBF, BUFSIZ);
    else
        setvbuf(f, nullptr, _IONBF, 0);
}

/* ------------------------------------------------------------------ temporary files */

static char g_tmpfile_name[MAX_PATH];
static unsigned g_tmpfile_count;
static unsigned g_tmpnam_count;

char *__cdecl _ultoa(unsigned long value, char *buf, int radix);

/* names are \t<pid>.<n> in the current drive's root, numbers in base 32 like msvcrt. */
static void next_name(char *buf, const char *prefix, unsigned *counter)
{
    char num[16];
    strcpy(buf, "\\");
    strcat(buf, prefix);
    _ultoa(GetCurrentProcessId(), num, 32);
    strcat(buf, num);
    strcat(buf, ".");
    _ultoa(++*counter, num, 32);
    strcat(buf, num);
}

static FILE *tmpfile_open(int share)
{
    FILE *f = MsvcrtGetStream();
    if (!f)
        return nullptr;
    _lock(_TMPNAM_LOCK);
    int saved = *_errno();
    int fd = -1;
    for (int attempt = 0; attempt < TMP_MAX && fd < 0; attempt++) {
        next_name(g_tmpfile_name, "t", &g_tmpfile_count);
        wchar_t w[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, g_tmpfile_name, -1, w, MAX_PATH);
        fd = MsvcrtOpenW(w, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY | _O_TEMPORARY, share, _S_IREAD | _S_IWRITE, 0);
        if (fd < 0 && *_errno() == EACCES) {
            /* the drive root is not writable, use the temporary directory. */
            char dir[MAX_PATH];
            DWORD n = GetTempPathA(MAX_PATH - 20, dir);
            if (n && n < MAX_PATH - 20) {
                strcat(dir, g_tmpfile_name + 1);
                MultiByteToWideChar(CP_ACP, 0, dir, -1, w, MAX_PATH);
                fd = MsvcrtOpenW(w, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY | _O_TEMPORARY, share,
                                 _S_IREAD | _S_IWRITE, 0);
                if (fd >= 0)
                    strcpy(g_tmpfile_name, dir);
            }
        }
        if (fd < 0 && *_errno() != EEXIST)
            break;
    }
    if (fd >= 0) {
        *_errno() = saved;
        f->_tmpfname = _strdup(g_tmpfile_name);
        f->_flag = _commode | _IORW;
        f->_file = fd;
    }
    _unlock(_TMPNAM_LOCK);
    MsvcrtUnlockFile(f);
    return fd >= 0 ? f : nullptr;
}

FILE *__cdecl tmpfile(void)
{
    return tmpfile_open(_SH_DENYNO);
}

errno_t __cdecl tmpfile_s(FILE **pf)
{
    if (!pf) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *pf = tmpfile_open(_SH_DENYRW);
    return *pf ? 0 : *_errno();
}

static int name_exists(const char *name)
{
    return GetFileAttributesA(name) != INVALID_FILE_ATTRIBUTES;
}

char *__cdecl tmpnam(char *buf)
{
    char *out = buf ? buf : MsvcrtThreadData()->tmpnam_buf;
    char name[MAX_PATH];
    _lock(_TMPNAM_LOCK);
    do
        next_name(name, "s", &g_tmpnam_count);
    while (name_exists(name) && g_tmpnam_count < TMP_MAX);
    _unlock(_TMPNAM_LOCK);
    strcpy(out, name);
    return out;
}

errno_t __cdecl tmpnam_s(char *buf, size_t size)
{
    if (!buf || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    char name[MAX_PATH];
    tmpnam(name);
    if (strlen(name) >= size) {
        *buf = 0;
        MSVCRT_INVALID(ERANGE);
        return ERANGE;
    }
    strcpy(buf, name);
    return 0;
}

wchar_t *__cdecl _wtmpnam(wchar_t *buf)
{
    wchar_t *out = buf ? buf : MsvcrtThreadData()->wtmpnam_buf;
    char name[MAX_PATH];
    tmpnam(name);
    MultiByteToWideChar(CP_ACP, 0, name, -1, out, L_tmpnam + 6);
    return out;
}

static char *tempnam_impl(const char *dir, const char *prefix)
{
    char base[MAX_PATH];
    const char *env = getenv("TMP");
    const char *d = nullptr;
    DWORD attr;
    if (env && (attr = GetFileAttributesA(env)) != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        d = env;
    else if (dir && (attr = GetFileAttributesA(dir)) != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        d = dir;
    else if (GetTempPathA(MAX_PATH, base))
        d = base;
    else
        d = "\\";
    if (!prefix)
        prefix = "";
    size_t dl = strlen(d), pl = strlen(prefix);
    char *out = malloc(dl + pl + 16);
    if (!out)
        return nullptr;
    static unsigned counter;
    _lock(_TMPNAM_LOCK);
    for (unsigned i = 0; i < 0x7fffffff; i++) {
        char num[16];
        strcpy(out, d);
        if (dl && d[dl - 1] != '\\' && d[dl - 1] != '/')
            strcat(out, "\\");
        strcat(out, prefix);
        _ultoa(++counter, num, 32);
        strcat(out, num);
        if (!name_exists(out))
            break;
    }
    _unlock(_TMPNAM_LOCK);
    return out;
}

char *__cdecl getenv(const char *name);

char *__cdecl _tempnam(const char *dir, const char *prefix)
{
    return tempnam_impl(dir, prefix);
}

wchar_t *__cdecl _wtempnam(const wchar_t *dir, const wchar_t *prefix)
{
    char *a_dir = MsvcrtWToA(dir), *a_prefix = MsvcrtWToA(prefix);
    char *r = tempnam_impl(a_dir, a_prefix);
    free(a_dir);
    free(a_prefix);
    wchar_t *w = MsvcrtAToW(r);
    free(r);
    return w;
}

int __cdecl _rmtmp(void)
{
    int count = 0;
    _lock(_IOB_SCAN_LOCK);
    for (int i = 0; i < MsvcrtStreamCount && MsvcrtStreams; i++) {
        FILE *f = MsvcrtStreams[i];
        if (f && inuse(f) && f->_tmpfname) {
            fclose(f);
            count++;
        }
    }
    _unlock(_IOB_SCAN_LOCK);
    return count;
}

errno_t __cdecl _wtmpnam_s(wchar_t *buf, size_t size)
{
    if (!buf || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    char name[MAX_PATH];
    tmpnam(name);
    if (!MultiByteToWideChar(CP_ACP, 0, name, -1, buf, (int)size)) {
        *buf = 0;
        MSVCRT_INVALID(ERANGE);
        return ERANGE;
    }
    return 0;
}
