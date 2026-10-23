/*
 * low level io: the file descriptor table over kernel32 handles, with
 * text mode newline translation, like msvcrt's _open, _read and _write.
 */
#include "msvcrt_private.h"

MSVCRT_IOINFO *MsvcrtIoinfo[IOINFO_ARRAYS];
int MsvcrtNhandle;
MSVCRT_IOINFO MsvcrtBadIoinfo = {-1, FTEXT | 0, 10, TM_ANSI, {10, 10}};
int MsvcrtUmask;

int MsvcrtAppType(void);
#define _CONSOLE_APP 1

/* ------------------------------------------------------------------ descriptor table */

static int grow_table(void)
{
    int block = MsvcrtNhandle >> IOINFO_L2E;
    if (block >= IOINFO_ARRAYS)
        return 0;
    MSVCRT_IOINFO *arr = calloc(IOINFO_ARRAY_ELTS, sizeof(MSVCRT_IOINFO));
    if (!arr)
        return 0;
    for (int i = 0; i < IOINFO_ARRAY_ELTS; i++) {
        arr[i].osfhnd = -1;
        arr[i].pipech = LF;
        arr[i].pipech2[0] = arr[i].pipech2[1] = LF;
        InitializeCriticalSection(&arr[i].lock);
        arr[i].lockinit = 1;
    }
    MsvcrtIoinfo[block] = arr;
    MsvcrtNhandle += IOINFO_ARRAY_ELTS;
    return 1;
}

/* returns a free descriptor, marked open and locked, or -1 with errno EMFILE. */
int MsvcrtAllocFd(void)
{
    int fd = -1;
    _lock(_OSFHND_LOCK);
    for (int i = 0;; i++) {
        if (i >= MsvcrtNhandle && !grow_table())
            break;
        MSVCRT_IOINFO *io = MsvcrtFd(i);
        if (!(io->osfile & FOPEN)) {
            EnterCriticalSection(&io->lock);
            if (io->osfile & FOPEN) {
                LeaveCriticalSection(&io->lock);
                continue;
            }
            io->osfile = FOPEN;
            io->osfhnd = -1;
            io->pipech = LF;
            io->pipech2[0] = io->pipech2[1] = LF;
            io->textmode = TM_ANSI;
            fd = i;
            break;
        }
    }
    _unlock(_OSFHND_LOCK);
    if (fd < 0) {
        MsvcrtSetErrno(EMFILE);
        *__doserrno() = 0;
    }
    return fd;
}

void MsvcrtSetOsfhnd(int fd, intptr_t handle)
{
    MSVCRT_IOINFO *io = MsvcrtFd(fd);
    if (MsvcrtAppType() == _CONSOLE_APP && fd <= 2) {
        static const DWORD std[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
        SetStdHandle(std[fd], (HANDLE)handle);
    }
    io->osfhnd = handle;
}

void MsvcrtFreeFd(int fd)
{
    MSVCRT_IOINFO *io = MsvcrtFd(fd);
    if (io == &MsvcrtBadIoinfo)
        return;
    if (MsvcrtAppType() == _CONSOLE_APP && fd <= 2) {
        static const DWORD std[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
        SetStdHandle(std[fd], nullptr);
    }
    io->osfhnd = -1;
    io->osfile = 0;
}

void MsvcrtLockFd(int fd)
{
    MSVCRT_IOINFO *io = MsvcrtFd(fd);
    if (io != &MsvcrtBadIoinfo)
        EnterCriticalSection(&io->lock);
}

void MsvcrtUnlockFd(int fd)
{
    MSVCRT_IOINFO *io = MsvcrtFd(fd);
    if (io != &MsvcrtBadIoinfo)
        LeaveCriticalSection(&io->lock);
}

/* fails like msvcrt with EBADF and a zero _doserrno when fd is not open. */
static int check_fd(int fd)
{
    if (MsvcrtFdValid(fd))
        return 1;
    *__doserrno() = 0;
    MSVCRT_INVALID(EBADF);
    return 0;
}

static HANDLE fd_handle(int fd)
{
    return (HANDLE)MsvcrtFd(fd)->osfhnd;
}

/* takes descriptors inherited from a parent c runtime, then the standard handles. */
void MsvcrtIoInit(void)
{
    grow_table();
    STARTUPINFOW si;
    GetStartupInfoW(&si);
    if (si.cbReserved2 >= sizeof(int) && si.lpReserved2) {
        const unsigned char *p = si.lpReserved2;
        int count;
        memcpy(&count, p, sizeof(int));
        p += sizeof(int);
        const unsigned char *flags = p;
        const unsigned char *handles = p + count;
        if ((size_t)si.cbReserved2 < sizeof(int) + (size_t)count * (1 + sizeof(intptr_t)))
            count = (int)((si.cbReserved2 - sizeof(int)) / (1 + sizeof(intptr_t)));
        if (count > MSVCRT_MAX_FILES)
            count = MSVCRT_MAX_FILES;
        while (MsvcrtNhandle < count && grow_table())
            ;
        if (count > MsvcrtNhandle)
            count = MsvcrtNhandle;
        for (int i = 0; i < count; i++) {
            intptr_t h;
            memcpy(&h, handles + i * sizeof(intptr_t), sizeof(h));
            if (h != -1 && h != -2 && (flags[i] & FOPEN) && ((flags[i] & FPIPE) || GetFileType((HANDLE)h))) {
                MSVCRT_IOINFO *io = MsvcrtFd(i);
                io->osfhnd = h;
                io->osfile = flags[i];
            }
        }
    }
    static const DWORD std[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    for (int i = 0; i < 3; i++) {
        MSVCRT_IOINFO *io = MsvcrtFd(i);
        if (io->osfhnd != -1 && io->osfhnd != -2 && (io->osfile & FOPEN)) {
            io->osfile |= FTEXT;
            continue;
        }
        io->osfile = FOPEN | FTEXT;
        HANDLE h = GetStdHandle(std[i]);
        DWORD type = h && h != INVALID_HANDLE_VALUE ? GetFileType(h) : FILE_TYPE_UNKNOWN;
        if (type != FILE_TYPE_UNKNOWN) {
            io->osfhnd = (intptr_t)h;
            if ((type & 0xff) == FILE_TYPE_CHAR)
                io->osfile |= FDEV;
            else if ((type & 0xff) == FILE_TYPE_PIPE)
                io->osfile |= FPIPE;
        } else {
            io->osfile |= FDEV;
            io->osfhnd = -2;
        }
    }
}

/* ------------------------------------------------------------------ open */

static int open_flags_to_mode(int oflag)
{
    if (oflag & _O_BINARY)
        return 0;
    if (oflag & (_O_TEXT | _O_WTEXT | _O_U16TEXT | _O_U8TEXT))
        return 1;
    return _fmode != _O_BINARY;
}

extern int _fmode;

/* opens name like _wsopen_s; returns the descriptor or -1 with errno set. */
int MsvcrtOpenW(const wchar_t *name, int oflag, int shflag, int pmode, int secure)
{
    DWORD access, share, disposition, attributes = FILE_ATTRIBUTE_NORMAL;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, !(oflag & _O_NOINHERIT)};

    switch (oflag & (_O_RDONLY | _O_WRONLY | _O_RDWR)) {
    case _O_RDONLY:
        access = GENERIC_READ;
        break;
    case _O_WRONLY:
        access = (oflag & _O_APPEND) && (oflag & (_O_WTEXT | _O_U16TEXT | _O_U8TEXT)) ? GENERIC_READ | GENERIC_WRITE
                                                                                      : GENERIC_WRITE;
        break;
    case _O_RDWR:
        access = GENERIC_READ | GENERIC_WRITE;
        break;
    default:
        *__doserrno() = 0;
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    switch (shflag) {
    case _SH_DENYRW:
        share = 0;
        break;
    case _SH_DENYWR:
        share = FILE_SHARE_READ;
        break;
    case _SH_DENYRD:
        share = FILE_SHARE_WRITE;
        break;
    case _SH_DENYNO:
        share = FILE_SHARE_READ | FILE_SHARE_WRITE;
        break;
    case 0x50: /* _SH_SECURE */
        share = access == GENERIC_READ ? FILE_SHARE_READ : 0;
        break;
    default:
        *__doserrno() = 0;
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    switch (oflag & (_O_CREAT | _O_EXCL | _O_TRUNC)) {
    case 0:
    case _O_EXCL:
        disposition = OPEN_EXISTING;
        break;
    case _O_CREAT:
        disposition = OPEN_ALWAYS;
        break;
    case _O_CREAT | _O_EXCL:
    case _O_CREAT | _O_TRUNC | _O_EXCL:
        disposition = CREATE_NEW;
        break;
    case _O_TRUNC:
    case _O_TRUNC | _O_EXCL:
        disposition = TRUNCATE_EXISTING;
        break;
    default:
        disposition = CREATE_ALWAYS;
        break;
    }
    if ((oflag & _O_CREAT) && !(pmode & ~MsvcrtUmask & _S_IWRITE))
        attributes = FILE_ATTRIBUTE_READONLY;
    if (oflag & _O_TEMPORARY) {
        attributes |= FILE_FLAG_DELETE_ON_CLOSE;
        access |= DELETE;
        share |= FILE_SHARE_DELETE;
    }
    if (oflag & _O_SHORT_LIVED)
        attributes |= FILE_ATTRIBUTE_TEMPORARY;
    if (oflag & _O_SEQUENTIAL)
        attributes |= FILE_FLAG_SEQUENTIAL_SCAN;
    else if (oflag & _O_RANDOM)
        attributes |= FILE_FLAG_RANDOM_ACCESS;

    int fd = MsvcrtAllocFd();
    if (fd < 0)
        return -1;
    HANDLE h = CreateFileW(name, access, share, &sa, disposition, attributes, nullptr);
    if (h == INVALID_HANDLE_VALUE && (access & (GENERIC_READ | GENERIC_WRITE)) == (GENERIC_READ | GENERIC_WRITE) &&
        (oflag & _O_WRONLY)) {
        access &= ~GENERIC_READ;
        h = CreateFileW(name, access, share, &sa, disposition, attributes, nullptr);
    }
    if (h == INVALID_HANDLE_VALUE) {
        MsvcrtFd(fd)->osfile = 0;
        MsvcrtUnlockFd(fd);
        MsvcrtDosMapError(GetLastError());
        return -1;
    }
    DWORD type = GetFileType(h);
    if (type == FILE_TYPE_UNKNOWN) {
        DWORD err = GetLastError();
        CloseHandle(h);
        MsvcrtFd(fd)->osfile = 0;
        MsvcrtUnlockFd(fd);
        MsvcrtDosMapError(err ? err : ERROR_INVALID_HANDLE);
        if (!err)
            MsvcrtSetErrno(EBADF);
        return -1;
    }
    unsigned char osfile = FOPEN;
    if ((type & 0xff) == FILE_TYPE_CHAR)
        osfile |= FDEV;
    else if ((type & 0xff) == FILE_TYPE_PIPE)
        osfile |= FPIPE;
    if (open_flags_to_mode(oflag))
        osfile |= FTEXT;
    if (oflag & _O_NOINHERIT)
        osfile |= FNOINHERIT;
    MSVCRT_IOINFO *io = MsvcrtFd(fd);
    MsvcrtSetOsfhnd(fd, (intptr_t)h);
    io->osfile = osfile;
    io->textmode = TM_ANSI;
    if (oflag & _O_U8TEXT)
        io->textmode = TM_UTF8;
    else if (oflag & (_O_U16TEXT | _O_WTEXT))
        io->textmode = TM_UTF16LE;

    if ((osfile & (FTEXT | FDEV | FPIPE)) == FTEXT && io->textmode == TM_ANSI && (oflag & _O_RDWR)) {
        /* a ctrl-z ending a text file opened for update is removed. */
        LARGE_INTEGER zero = {}, end;
        if (SetFilePointerEx(h, (LARGE_INTEGER){.QuadPart = -1}, &end, FILE_END)) {
            char last = 0;
            DWORD got = 0;
            if (ReadFile(h, &last, 1, &got, nullptr) && got == 1 && last == CTRLZ) {
                SetFilePointerEx(h, end, nullptr, FILE_BEGIN);
                SetEndOfFile(h);
            }
        }
        SetFilePointerEx(h, zero, nullptr, FILE_BEGIN);
    }
    if (io->textmode != TM_ANSI && (osfile & (FDEV | FPIPE)) == 0) {
        LARGE_INTEGER zero = {}, size;
        if (GetFileSizeEx(h, &size) && !size.QuadPart && (access & GENERIC_WRITE)) {
            DWORD written;
            if (io->textmode == TM_UTF8)
                WriteFile(h, "\xef\xbb\xbf", 3, &written, nullptr);
            else if (!(oflag & _O_WTEXT) || (oflag & _O_U16TEXT))
                WriteFile(h, "\xff\xfe", 2, &written, nullptr);
        } else if ((access & GENERIC_READ) && size.QuadPart) {
            unsigned char bom[3];
            DWORD got = 0;
            ReadFile(h, bom, 3, &got, nullptr);
            LONGLONG skip = 0;
            if (got >= 3 && bom[0] == 0xef && bom[1] == 0xbb && bom[2] == 0xbf) {
                skip = 3;
                if (oflag & _O_WTEXT)
                    io->textmode = TM_UTF8;
            } else if (got >= 2 && bom[0] == 0xff && bom[1] == 0xfe) {
                skip = 2;
                if (oflag & _O_WTEXT)
                    io->textmode = TM_UTF16LE;
            }
            SetFilePointerEx(h, (LARGE_INTEGER){.QuadPart = skip}, nullptr, FILE_BEGIN);
            if (oflag & _O_APPEND)
                SetFilePointerEx(h, zero, nullptr, FILE_END);
        }
    }
    if (oflag & _O_APPEND)
        io->osfile |= FAPPEND;
    MsvcrtUnlockFd(fd);
    return fd;
}

static wchar_t *path_to_w(const char *path)
{
    if (!path)
        return nullptr;
    UINT cp = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
    int n = MultiByteToWideChar(cp, 0, path, -1, nullptr, 0);
    if (n <= 0) {
        MsvcrtDosMapError(GetLastError());
        return nullptr;
    }
    wchar_t *w = malloc(n * sizeof(wchar_t));
    if (w)
        MultiByteToWideChar(cp, 0, path, -1, w, n);
    return w;
}

wchar_t *MsvcrtPathToW(const char *path)
{
    return path_to_w(path);
}

int __cdecl _wsopen(const wchar_t *name, int oflag, int shflag, ...)
{
    int pmode = 0;
    if (oflag & _O_CREAT) {
        va_list ap;
        va_start(ap, shflag);
        pmode = va_arg(ap, int);
        va_end(ap);
    }
    if (!name) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    return MsvcrtOpenW(name, oflag, shflag, pmode, 0);
}

int __cdecl _sopen(const char *name, int oflag, int shflag, ...)
{
    int pmode = 0;
    if (oflag & _O_CREAT) {
        va_list ap;
        va_start(ap, shflag);
        pmode = va_arg(ap, int);
        va_end(ap);
    }
    if (!name) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    wchar_t *w = path_to_w(name);
    if (!w)
        return -1;
    int fd = MsvcrtOpenW(w, oflag, shflag, pmode, 0);
    free(w);
    return fd;
}

int __cdecl _wopen(const wchar_t *name, int oflag, ...)
{
    int pmode = 0;
    if (oflag & _O_CREAT) {
        va_list ap;
        va_start(ap, oflag);
        pmode = va_arg(ap, int);
        va_end(ap);
    }
    return _wsopen(name, oflag, _SH_DENYNO, pmode);
}

int __cdecl _open(const char *name, int oflag, ...)
{
    int pmode = 0;
    if (oflag & _O_CREAT) {
        va_list ap;
        va_start(ap, oflag);
        pmode = va_arg(ap, int);
        va_end(ap);
    }
    return _sopen(name, oflag, _SH_DENYNO, pmode);
}

errno_t __cdecl _wsopen_s(int *pfd, const wchar_t *name, int oflag, int shflag, int pmode)
{
    if (!pfd) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *pfd = -1;
    if (!name || (pmode & ~(_S_IREAD | _S_IWRITE))) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *pfd = MsvcrtOpenW(name, oflag, shflag, pmode, 1);
    return *pfd < 0 ? *_errno() : 0;
}

errno_t __cdecl _sopen_s(int *pfd, const char *name, int oflag, int shflag, int pmode)
{
    if (!pfd) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *pfd = -1;
    if (!name || (pmode & ~(_S_IREAD | _S_IWRITE))) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    wchar_t *w = path_to_w(name);
    if (!w)
        return *_errno();
    *pfd = MsvcrtOpenW(w, oflag, shflag, pmode, 1);
    free(w);
    return *pfd < 0 ? *_errno() : 0;
}

/* shared by _sopen and _sopen_s: the secure one checks the mode and reports through the result. */
DECLSPEC_EXPORT errno_t __cdecl _sopen_dispatch(const char *name, int oflag, int shflag, int pmode, int *pfd,
                                                int secure)
{
    if (secure && !pfd) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    int local;
    if (!pfd)
        pfd = &local;
    *pfd = -1;
    if (!name || (secure && (pmode & ~(_S_IREAD | _S_IWRITE)))) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    wchar_t *w = path_to_w(name);
    if (!w)
        return *_errno();
    *pfd = MsvcrtOpenW(w, oflag, shflag, pmode, secure != 0);
    free(w);
    return *pfd < 0 ? *_errno() : 0;
}

DECLSPEC_EXPORT errno_t __cdecl _wsopen_dispatch(const wchar_t *name, int oflag, int shflag, int pmode, int *pfd,
                                                 int secure)
{
    if (secure && !pfd) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    int local;
    if (!pfd)
        pfd = &local;
    *pfd = -1;
    if (!name || (secure && (pmode & ~(_S_IREAD | _S_IWRITE)))) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *pfd = MsvcrtOpenW(name, oflag, shflag, pmode, secure != 0);
    return *pfd < 0 ? *_errno() : 0;
}

int __cdecl _creat(const char *name, int pmode)
{
    return _open(name, _O_CREAT | _O_TRUNC | _O_RDWR, pmode);
}

int __cdecl _wcreat(const wchar_t *name, int pmode)
{
    return _wopen(name, _O_CREAT | _O_TRUNC | _O_RDWR, pmode);
}

int __cdecl _umask(int mode)
{
    int old = MsvcrtUmask;
    MsvcrtUmask = mode & (_S_IREAD | _S_IWRITE);
    return old;
}

errno_t __cdecl _umask_s(int mode, int *old)
{
    if (!old || (mode & ~(_S_IREAD | _S_IWRITE))) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *old = MsvcrtUmask;
    MsvcrtUmask = mode;
    return 0;
}

int __cdecl _open_osfhandle(intptr_t handle, int flags)
{
    DWORD type = GetFileType((HANDLE)handle);
    if (type == FILE_TYPE_UNKNOWN) {
        MsvcrtDosMapError(GetLastError());
        return -1;
    }
    unsigned char osfile = FOPEN;
    if (flags & _O_APPEND)
        osfile |= FAPPEND;
    if (flags & _O_TEXT)
        osfile |= FTEXT;
    if (flags & _O_NOINHERIT)
        osfile |= FNOINHERIT;
    if ((type & 0xff) == FILE_TYPE_CHAR)
        osfile |= FDEV;
    else if ((type & 0xff) == FILE_TYPE_PIPE)
        osfile |= FPIPE;
    int fd = MsvcrtAllocFd();
    if (fd < 0)
        return -1;
    MsvcrtSetOsfhnd(fd, handle);
    MSVCRT_IOINFO *io = MsvcrtFd(fd);
    io->osfile = osfile;
    io->textmode = (flags & _O_U8TEXT) ? TM_UTF8 : (flags & (_O_U16TEXT | _O_WTEXT)) ? TM_UTF16LE : TM_ANSI;
    MsvcrtUnlockFd(fd);
    return fd;
}

intptr_t __cdecl _get_osfhandle(int fd)
{
    if (!check_fd(fd))
        return -1;
    return MsvcrtFd(fd)->osfhnd;
}

int __cdecl _isatty(int fd)
{
    if (fd == -2) {
        MsvcrtSetErrno(EBADF);
        return 0;
    }
    if (fd < 0 || fd >= MsvcrtNhandle) {
        MSVCRT_INVALID(EBADF);
        return 0;
    }
    return MsvcrtFd(fd)->osfile & FDEV;
}

/* ------------------------------------------------------------------ close and dup */

int MsvcrtCloseNolock(int fd)
{
    int result = 0;
    intptr_t h = MsvcrtFd(fd)->osfhnd;
    int shared = (fd == 1 && MsvcrtFdValid(2) && MsvcrtFd(2)->osfhnd == h) ||
                 (fd == 2 && MsvcrtFdValid(1) && MsvcrtFd(1)->osfhnd == h);
    if (h != -1 && h != -2 && !shared && !CloseHandle((HANDLE)h)) {
        MsvcrtDosMapError(GetLastError());
        result = -1;
    }
    MsvcrtFreeFd(fd);
    MsvcrtFd(fd)->osfile = 0;
    return result;
}

int __cdecl _close(int fd)
{
    if (!check_fd(fd))
        return -1;
    MsvcrtLockFd(fd);
    int r = MsvcrtFdValid(fd) ? MsvcrtCloseNolock(fd) : (MsvcrtSetErrno(EBADF), -1);
    MsvcrtUnlockFd(fd);
    return r;
}

static int dup_into(int src, int dst)
{
    HANDLE copy;
    HANDLE process = GetCurrentProcess();
    if (!DuplicateHandle(process, fd_handle(src), process, &copy, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
        MsvcrtDosMapError(GetLastError());
        return -1;
    }
    MsvcrtSetOsfhnd(dst, (intptr_t)copy);
    MSVCRT_IOINFO *s = MsvcrtFd(src), *d = MsvcrtFd(dst);
    d->osfile = s->osfile & ~FNOINHERIT;
    d->textmode = s->textmode;
    return 0;
}

int __cdecl _dup(int fd)
{
    if (!check_fd(fd))
        return -1;
    int nfd = MsvcrtAllocFd();
    if (nfd < 0)
        return -1;
    MsvcrtLockFd(fd);
    int r = dup_into(fd, nfd);
    MsvcrtUnlockFd(fd);
    if (r < 0) {
        MsvcrtFd(nfd)->osfile = 0;
        MsvcrtUnlockFd(nfd);
        return -1;
    }
    MsvcrtUnlockFd(nfd);
    return nfd;
}

int __cdecl _dup2(int fd, int fd2)
{
    if (!check_fd(fd))
        return -1;
    if (fd2 < 0 || fd2 >= MSVCRT_MAX_FILES) {
        *__doserrno() = 0;
        MSVCRT_INVALID(EBADF);
        return -1;
    }
    _lock(_OSFHND_LOCK);
    while (fd2 >= MsvcrtNhandle)
        if (!grow_table()) {
            _unlock(_OSFHND_LOCK);
            MsvcrtSetErrno(EMFILE);
            return -1;
        }
    _unlock(_OSFHND_LOCK);
    if (fd == fd2)
        return 0;
    if (fd < fd2) {
        MsvcrtLockFd(fd);
        MsvcrtLockFd(fd2);
    } else {
        MsvcrtLockFd(fd2);
        MsvcrtLockFd(fd);
    }
    if (MsvcrtFdValid(fd2))
        MsvcrtCloseNolock(fd2);
    int r = dup_into(fd, fd2);
    MsvcrtUnlockFd(fd);
    MsvcrtUnlockFd(fd2);
    return r;
}

/* ------------------------------------------------------------------ read */

/* raw read of up to count bytes, with the peeked characters of pipes and devices first. */
static int raw_read(int fd, char *buf, unsigned count, DWORD *got)
{
    MSVCRT_IOINFO *io = MsvcrtFd(fd);
    *got = 0;
    if ((io->osfile & (FPIPE | FDEV)) && io->pipech != LF && count) {
        *buf++ = io->pipech;
        io->pipech = LF;
        (*got)++;
        count--;
    }
    if (!count)
        return 1;
    DWORD n = 0;
    if (!ReadFile(fd_handle(fd), buf, count, &n, nullptr)) {
        DWORD err = GetLastError();
        if (err == ERROR_BROKEN_PIPE)
            return 1;
        if (err == ERROR_ACCESS_DENIED) {
            MsvcrtSetErrno(EBADF);
            *__doserrno() = err;
        } else {
            MsvcrtDosMapError(err);
        }
        return *got ? 1 : 0;
    }
    *got += n;
    return 1;
}

int MsvcrtReadNolock(int fd, void *buffer, unsigned count)
{
    MSVCRT_IOINFO *io = MsvcrtFd(fd);
    if (!count || (io->osfile & FEOFLAG))
        return 0;
    if (io->textmode == TM_UTF8 && (io->osfile & FTEXT)) {
        /* reads utf-8 bytes and returns utf-16 text in buffer. */
        unsigned room = count / 2;
        if (!room) {
            MSVCRT_INVALID(EINVAL);
            return -1;
        }
        char *tmp = malloc(room);
        if (!tmp) {
            MsvcrtSetErrno(ENOMEM);
            return -1;
        }
        DWORD got;
        if (!raw_read(fd, tmp, room, &got)) {
            free(tmp);
            return -1;
        }
        /* keeps incomplete sequences for the next read. */
        DWORD keep = 0;
        if (got) {
            DWORD i = got;
            while (i > 0 && got - i < 4 && ((unsigned char)tmp[i - 1] & 0xc0) == 0x80)
                i--;
            if (i > 0 && ((unsigned char)tmp[i - 1] & 0xc0) == 0xc0) {
                unsigned char lead = tmp[i - 1];
                DWORD need = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : 2;
                if (got - (i - 1) < need)
                    keep = got - (i - 1);
            }
        }
        if (keep && !(io->osfile & (FPIPE | FDEV)))
            SetFilePointerEx(fd_handle(fd), (LARGE_INTEGER){.QuadPart = -(LONGLONG)keep}, nullptr, FILE_CURRENT);
        int n = MultiByteToWideChar(CP_UTF8, 0, tmp, (int)(got - keep), buffer, (int)room);
        free(tmp);
        wchar_t *w = buffer, *q = buffer;
        for (int i = 0; i < n; i++) {
            if (w[i] == CR && i + 1 < n && w[i + 1] == LF)
                continue;
            *q++ = w[i];
        }
        return (int)((q - (wchar_t *)buffer) * 2);
    }
    DWORD got;
    if (!raw_read(fd, buffer, count, &got))
        return -1;
    if (!got)
        return 0;
    if (!(io->osfile & FTEXT))
        return (int)got;
    /* a buffer starting with a line feed tells ftell its carriage return came with the previous read. */
    if (*(char *)buffer == LF)
        io->osfile |= FCRLF;
    else
        io->osfile &= ~FCRLF;
    if (io->textmode == TM_UTF16LE) {
        wchar_t *p = buffer, *q = buffer, *end = (wchar_t *)((char *)buffer + (got & ~1u));
        while (p < end) {
            if (*p == CTRLZ) {
                if (!(io->osfile & FDEV))
                    io->osfile |= FEOFLAG;
                else
                    *q++ = *p++;
                break;
            }
            if (*p != CR) {
                *q++ = *p++;
            } else if (p + 1 < end) {
                if (p[1] == LF) {
                    p += 2;
                    *q++ = LF;
                } else {
                    *q++ = *p++;
                }
            } else {
                p++;
                wchar_t peek = 0;
                DWORD n = 0;
                if (!ReadFile(fd_handle(fd), &peek, 2, &n, nullptr) || n != 2) {
                    *q++ = CR;
                } else if (io->osfile & (FDEV | FPIPE)) {
                    if (peek == LF) {
                        *q++ = LF;
                    } else {
                        *q++ = CR;
                        io->pipech = (char)peek;
                        io->pipech2[0] = (char)(peek >> 8);
                    }
                } else if (q == (wchar_t *)buffer && peek == LF) {
                    *q++ = LF;
                } else {
                    MsvcrtLseekNolock(fd, -2, SEEK_CUR);
                    if (peek != LF)
                        *q++ = CR;
                }
            }
        }
        return (int)((char *)q - (char *)buffer);
    }
    char *p = buffer, *q = buffer, *end = (char *)buffer + got;
    while (p < end) {
        if (*p == CTRLZ) {
            if (!(io->osfile & FDEV))
                io->osfile |= FEOFLAG;
            else
                *q++ = *p++;
            break;
        }
        if (*p != CR) {
            *q++ = *p++;
        } else if (p + 1 < end) {
            if (p[1] == LF) {
                p += 2;
                *q++ = LF;
            } else {
                *q++ = *p++;
            }
        } else {
            p++;
            char peek = 0;
            DWORD n = 0;
            if (!ReadFile(fd_handle(fd), &peek, 1, &n, nullptr) || !n) {
                *q++ = CR;
            } else if (io->osfile & (FDEV | FPIPE)) {
                if (peek == LF) {
                    *q++ = LF;
                } else {
                    *q++ = CR;
                    io->pipech = peek;
                }
            } else if (q == (char *)buffer && peek == LF) {
                *q++ = LF;
            } else {
                MsvcrtLseekNolock(fd, -1, SEEK_CUR);
                if (peek != LF)
                    *q++ = CR;
            }
        }
    }
    return (int)(q - (char *)buffer);
}

int __cdecl _read(int fd, void *buf, unsigned int count)
{
    if (!check_fd(fd))
        return -1;
    if (count > INT_MAX || !buf) {
        *__doserrno() = 0;
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    MsvcrtLockFd(fd);
    int r = MsvcrtFdValid(fd) ? MsvcrtReadNolock(fd, buf, count) : (MsvcrtSetErrno(EBADF), -1);
    MsvcrtUnlockFd(fd);
    return r;
}

/* ------------------------------------------------------------------ write */

static int write_all(int fd, const void *buf, DWORD size, DWORD *written)
{
    if (!WriteFile(fd_handle(fd), buf, size, written, nullptr)) {
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            MsvcrtSetErrno(EBADF);
            *__doserrno() = err;
        } else {
            MsvcrtDosMapError(err);
        }
        return 0;
    }
    return 1;
}

/* the result when nothing could be written: a device swallowing ctrl-z, else a full disk. */
static int nothing_written(int fd, const void *buf)
{
    if ((MsvcrtFd(fd)->osfile & FDEV) && *(const char *)buf == CTRLZ)
        return 0;
    MsvcrtSetErrno(ENOSPC);
    *__doserrno() = 0;
    return -1;
}

int MsvcrtWriteNolock(int fd, const void *buffer, unsigned count)
{
    MSVCRT_IOINFO *io = MsvcrtFd(fd);
    if (!count)
        return 0;
    if (io->osfile & FAPPEND)
        MsvcrtLseekNolock(fd, 0, SEEK_END);
    DWORD written;
    if (!(io->osfile & FTEXT)) {
        if (!write_all(fd, buffer, count, &written))
            return -1;
        if (!written)
            return nothing_written(fd, buffer);
        return (int)written;
    }
    if (io->textmode == TM_ANSI) {
        const char *p = buffer, *end = p + count;
        char tmp[1025];
        unsigned done = 0;
        while (p < end) {
            DWORD n = 0;
            const char *start = p;
            while (p < end && n < sizeof(tmp) - 1) {
                if (*p == LF)
                    tmp[n++] = CR;
                tmp[n++] = *p++;
            }
            if (!write_all(fd, tmp, n, &written))
                return done ? (int)done : -1;
            if (written < n) {
                /* counts the source bytes written, a newline only along with its carriage return. */
                DWORD out = 0;
                for (const char *s = start; s < p; s++) {
                    DWORD need = *s == LF ? 2 : 1;
                    if (out + need > written)
                        break;
                    out += need;
                    done++;
                }
                if (!done)
                    return nothing_written(fd, buffer);
                return (int)done;
            }
            done += (unsigned)(p - start);
        }
        return (int)done;
    }
    /* Unicode text: the buffer holds utf-16 characters. */
    const wchar_t *p = buffer, *end = (const wchar_t *)((const char *)buffer + (count & ~1u));
    unsigned done = 0;
    while (p < end) {
        wchar_t tmp[512];
        DWORD n = 0;
        const wchar_t *start = p;
        while (p < end && n < 510) {
            if (*p == LF)
                tmp[n++] = CR;
            tmp[n++] = *p++;
        }
        if ((io->osfile & FDEV) && GetFileType(fd_handle(fd)) == FILE_TYPE_CHAR) {
            DWORD chars;
            if (!WriteConsoleW(fd_handle(fd), tmp, n, &chars, nullptr)) {
                MsvcrtDosMapError(GetLastError());
                return done ? (int)done : -1;
            }
        } else if (io->textmode == TM_UTF8) {
            char utf8[512 * 3];
            int len = WideCharToMultiByte(CP_UTF8, 0, tmp, (int)n, utf8, sizeof(utf8), nullptr, nullptr);
            if (!write_all(fd, utf8, (DWORD)len, &written))
                return done ? (int)done : -1;
            if ((int)written < len)
                return done ? (int)done : nothing_written(fd, buffer);
        } else {
            if (!write_all(fd, tmp, n * 2, &written))
                return done ? (int)done : -1;
            if (written < n * 2)
                return done ? (int)done : nothing_written(fd, buffer);
        }
        done += (unsigned)((p - start) * 2);
    }
    return (int)done;
}

int __cdecl _write(int fd, const void *buf, unsigned int count)
{
    if (!check_fd(fd))
        return -1;
    if (!buf && count) {
        *__doserrno() = 0;
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    MsvcrtLockFd(fd);
    int r = MsvcrtFdValid(fd) ? MsvcrtWriteNolock(fd, buf, count) : (MsvcrtSetErrno(EBADF), -1);
    MsvcrtUnlockFd(fd);
    return r;
}

/* ------------------------------------------------------------------ seek and size */

long long MsvcrtLseekNolock(int fd, long long offset, int origin)
{
    LARGE_INTEGER pos;
    if (origin < SEEK_SET || origin > SEEK_END) {
        *__doserrno() = 0;
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    if (!SetFilePointerEx(fd_handle(fd), (LARGE_INTEGER){.QuadPart = offset}, &pos, origin)) {
        MsvcrtDosMapError(GetLastError());
        return -1;
    }
    MsvcrtFd(fd)->osfile &= ~FEOFLAG;
    return pos.QuadPart;
}

long long __cdecl _lseeki64(int fd, long long offset, int origin)
{
    if (!check_fd(fd))
        return -1;
    MsvcrtLockFd(fd);
    long long r = MsvcrtFdValid(fd) ? MsvcrtLseekNolock(fd, offset, origin) : (MsvcrtSetErrno(EBADF), -1);
    MsvcrtUnlockFd(fd);
    return r;
}

long __cdecl _lseek(int fd, long offset, int origin)
{
    if (!check_fd(fd))
        return -1;
    MsvcrtLockFd(fd);
    long long r = -1;
    if (!MsvcrtFdValid(fd)) {
        MsvcrtSetErrno(EBADF);
    } else {
        long long old = MsvcrtLseekNolock(fd, 0, SEEK_CUR);
        r = old < 0 ? -1 : MsvcrtLseekNolock(fd, offset, origin);
        if (r > LONG_MAX) {
            MsvcrtLseekNolock(fd, old, SEEK_SET);
            MsvcrtSetErrno(EINVAL);
            r = -1;
        }
    }
    MsvcrtUnlockFd(fd);
    return (long)r;
}

long __cdecl _tell(int fd)
{
    return _lseek(fd, 0, SEEK_CUR);
}

long long __cdecl _telli64(int fd)
{
    return _lseeki64(fd, 0, SEEK_CUR);
}

long long __cdecl _filelengthi64(int fd)
{
    if (!check_fd(fd))
        return -1;
    LARGE_INTEGER size;
    MsvcrtLockFd(fd);
    long long r = -1;
    if (GetFileSizeEx(fd_handle(fd), &size))
        r = size.QuadPart;
    else
        MsvcrtDosMapError(GetLastError());
    MsvcrtUnlockFd(fd);
    return r;
}

long __cdecl _filelength(int fd)
{
    long long r = _filelengthi64(fd);
    if (r > LONG_MAX) {
        MsvcrtSetErrno(EINVAL);
        return -1;
    }
    return (long)r;
}

int __cdecl _eof(int fd)
{
    if (!check_fd(fd))
        return -1;
    MsvcrtLockFd(fd);
    int r = -1;
    long long here = MsvcrtLseekNolock(fd, 0, SEEK_CUR);
    long long end = here < 0 ? -1 : MsvcrtLseekNolock(fd, 0, SEEK_END);
    if (end >= 0) {
        r = here == end;
        MsvcrtLseekNolock(fd, here, SEEK_SET);
    }
    MsvcrtUnlockFd(fd);
    return r;
}

errno_t __cdecl _chsize_s(int fd, long long size)
{
    if (!check_fd(fd))
        return EBADF;
    if (size < 0) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    MsvcrtLockFd(fd);
    errno_t r = 0;
    long long here = MsvcrtLseekNolock(fd, 0, SEEK_CUR);
    if (here < 0 || MsvcrtLseekNolock(fd, size, SEEK_SET) < 0) {
        r = *_errno();
    } else if (!SetEndOfFile(fd_handle(fd))) {
        MsvcrtDosMapError(GetLastError());
        r = EACCES;
        MsvcrtSetErrno(EACCES);
    }
    if (here >= 0)
        MsvcrtLseekNolock(fd, here, SEEK_SET);
    MsvcrtUnlockFd(fd);
    return r;
}

int __cdecl _chsize(int fd, long size)
{
    return _chsize_s(fd, size) ? -1 : 0;
}

int MsvcrtCommitNolock(int fd)
{
    if (!FlushFileBuffers(fd_handle(fd))) {
        DWORD err = GetLastError();
        /* consoles and pipes cannot be flushed; that is no error. */
        if (MsvcrtFd(fd)->osfile & (FDEV | FPIPE))
            return 0;
        *__doserrno() = err;
        MsvcrtSetErrno(EBADF);
        return -1;
    }
    return 0;
}

int __cdecl _commit(int fd)
{
    if (!check_fd(fd))
        return -1;
    MsvcrtLockFd(fd);
    int r = MsvcrtCommitNolock(fd);
    MsvcrtUnlockFd(fd);
    return r;
}

int __cdecl _setmode(int fd, int mode)
{
    if (!check_fd(fd))
        return -1;
    if (mode != _O_TEXT && mode != _O_BINARY && mode != _O_WTEXT && mode != _O_U8TEXT && mode != _O_U16TEXT) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    MsvcrtLockFd(fd);
    MSVCRT_IOINFO *io = MsvcrtFd(fd);
    int old;
    if (!(io->osfile & FTEXT))
        old = _O_BINARY;
    else if (io->textmode == TM_UTF8)
        old = _O_U8TEXT;
    else if (io->textmode == TM_UTF16LE)
        old = _O_U16TEXT;
    else
        old = _O_TEXT;
    switch (mode) {
    case _O_BINARY:
        io->osfile &= ~FTEXT;
        break;
    case _O_TEXT:
        io->osfile |= FTEXT;
        io->textmode = TM_ANSI;
        break;
    case _O_U8TEXT:
        io->osfile |= FTEXT;
        io->textmode = TM_UTF8;
        break;
    default:
        io->osfile |= FTEXT;
        io->textmode = TM_UTF16LE;
        break;
    }
    MsvcrtUnlockFd(fd);
    return old;
}

#define _LK_UNLCK 0
#define _LK_LOCK 1
#define _LK_NBLCK 2
#define _LK_RLCK 3
#define _LK_NBRLCK 4

int __cdecl _locking(int fd, int mode, long nbytes)
{
    if (!check_fd(fd))
        return -1;
    if (nbytes < 0 || mode < _LK_UNLCK || mode > _LK_NBRLCK) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    MsvcrtLockFd(fd);
    int r = 0;
    long long pos = MsvcrtLseekNolock(fd, 0, SEEK_CUR);
    if (pos < 0) {
        r = -1;
    } else {
        DWORD lo = (DWORD)pos, hi = (DWORD)(pos >> 32);
        if (mode == _LK_UNLCK) {
            if (!UnlockFile(fd_handle(fd), lo, hi, (DWORD)nbytes, 0)) {
                MsvcrtDosMapError(GetLastError());
                MsvcrtSetErrno(EACCES);
                r = -1;
            }
        } else {
            int tries = (mode == _LK_LOCK || mode == _LK_RLCK) ? 10 : 1;
            for (;;) {
                if (LockFile(fd_handle(fd), lo, hi, (DWORD)nbytes, 0))
                    break;
                if (--tries <= 0) {
                    MsvcrtDosMapError(GetLastError());
                    MsvcrtSetErrno((mode == _LK_LOCK || mode == _LK_RLCK) ? EDEADLK : EACCES);
                    r = -1;
                    break;
                }
                Sleep(1000);
            }
        }
    }
    MsvcrtUnlockFd(fd);
    return r;
}

/* ------------------------------------------------------------------ pipes */

typedef BOOL(WINAPI *CREATE_PIPE)(PHANDLE, PHANDLE, LPSECURITY_ATTRIBUTES, DWORD);

int __cdecl _pipe(int *fds, unsigned int size, int textmode)
{
    if (!fds) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    fds[0] = fds[1] = -1;
    if (textmode & ~(_O_NOINHERIT | _O_BINARY | _O_TEXT | _O_WTEXT | _O_U16TEXT | _O_U8TEXT)) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    static CREATE_PIPE create_pipe;
    if (!create_pipe) {
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        create_pipe = k32 ? (CREATE_PIPE)(void *)GetProcAddress(k32, "CreatePipe") : nullptr;
    }
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, !(textmode & _O_NOINHERIT)};
    HANDLE r, w;
    if (!create_pipe) {
        MsvcrtDosMapError(ERROR_CALL_NOT_IMPLEMENTED);
        MsvcrtSetErrno(EMFILE);
        return -1;
    }
    if (!create_pipe(&r, &w, &sa, size)) {
        MsvcrtDosMapError(GetLastError());
        return -1;
    }
    unsigned char osfile = FOPEN | FPIPE;
    int text = (textmode & _O_BINARY) ? 0 : (textmode & (_O_TEXT | _O_WTEXT | _O_U16TEXT | _O_U8TEXT)) ? 1
                                                                                                     : _fmode != _O_BINARY;
    if (text)
        osfile |= FTEXT;
    if (textmode & _O_NOINHERIT)
        osfile |= FNOINHERIT;
    int a = MsvcrtAllocFd();
    if (a < 0) {
        CloseHandle(r);
        CloseHandle(w);
        return -1;
    }
    int b = MsvcrtAllocFd();
    if (b < 0) {
        MsvcrtFd(a)->osfile = 0;
        MsvcrtUnlockFd(a);
        CloseHandle(r);
        CloseHandle(w);
        return -1;
    }
    unsigned char tm = (textmode & _O_U8TEXT) ? TM_UTF8 : (textmode & (_O_U16TEXT | _O_WTEXT)) ? TM_UTF16LE : TM_ANSI;
    MsvcrtSetOsfhnd(a, (intptr_t)r);
    MsvcrtSetOsfhnd(b, (intptr_t)w);
    MsvcrtFd(a)->osfile = osfile;
    MsvcrtFd(b)->osfile = osfile;
    MsvcrtFd(a)->textmode = tm;
    MsvcrtFd(b)->textmode = tm;
    MsvcrtUnlockFd(a);
    MsvcrtUnlockFd(b);
    fds[0] = a;
    fds[1] = b;
    return 0;
}

/* the descriptor table, for programs reading msvcrt's __pioinfo directly. */
MSVCRT_IOINFO **__cdecl __p___pioinfo(void)
{
    return MsvcrtIoinfo;
}

int __cdecl _getmaxstdio(void)
{
    return 512;
}

int __cdecl _setmaxstdio(int max)
{
    if (max < _IOB_ENTRIES || max > _NSTREAM_MAX) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    return max;
}
