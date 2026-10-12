/*
 * the file system: paths, directories, attributes, stat, find and
 * file times, over kernel32.
 */
#include "msvcrt_private.h"

wchar_t *MsvcrtPathToW(const char *path);

/* converts a result path back to the file api code page into buf, allocating when buf is NULL. */
static char *path_to_a(const wchar_t *w, char *buf, size_t size, int alloc_min)
{
    UINT cp = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
    int n = WideCharToMultiByte(cp, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) {
        MsvcrtDosMapError(GetLastError());
        return NULL;
    }
    if (!buf) {
        size_t want = (size_t)n > size ? (size_t)n : size;
        if (alloc_min && want < (size_t)alloc_min)
            want = alloc_min;
        buf = malloc(want);
        if (!buf) {
            MsvcrtSetErrno(ENOMEM);
            return NULL;
        }
        size = want;
    } else if ((size_t)n > size) {
        MsvcrtSetErrno(ERANGE);
        return NULL;
    }
    WideCharToMultiByte(cp, 0, w, -1, buf, (int)size, NULL, NULL);
    return buf;
}

/* ------------------------------------------------------------------ delete and rename */

int __cdecl _wunlink(const wchar_t *path)
{
    if (!path) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    if (!DeleteFileW(path)) {
        MsvcrtDosMapError(GetLastError());
        return -1;
    }
    return 0;
}

int __cdecl _unlink(const char *path)
{
    if (!path) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    wchar_t *w = MsvcrtPathToW(path);
    if (!w)
        return -1;
    int r = _wunlink(w);
    free(w);
    return r;
}

int __cdecl _wremove(const wchar_t *path)
{
    return _wunlink(path);
}

int __cdecl remove(const char *path)
{
    return _unlink(path);
}

int __cdecl _wrename(const wchar_t *from, const wchar_t *to)
{
    if (!from || !to) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    if (!MoveFileExW(from, to, MOVEFILE_COPY_ALLOWED)) {
        MsvcrtDosMapError(GetLastError());
        return -1;
    }
    return 0;
}

int __cdecl rename(const char *from, const char *to)
{
    if (!from || !to) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    wchar_t *wf = MsvcrtPathToW(from), *wt = MsvcrtPathToW(to);
    int r = wf && wt ? _wrename(wf, wt) : -1;
    free(wf);
    free(wt);
    return r;
}

/* ------------------------------------------------------------------ access and attributes */

errno_t __cdecl _waccess_s(const wchar_t *path, int mode)
{
    if (!path || (mode & ~6)) {
        *__doserrno() = 0;
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data)) {
        MsvcrtDosMapError(GetLastError());
        return *_errno();
    }
    if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) &&
        (mode & 2)) {
        *__doserrno() = ERROR_ACCESS_DENIED;
        MsvcrtSetErrno(EACCES);
        return EACCES;
    }
    return 0;
}

int __cdecl _waccess(const wchar_t *path, int mode)
{
    return _waccess_s(path, mode) ? -1 : 0;
}

errno_t __cdecl _access_s(const char *path, int mode)
{
    if (!path) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    wchar_t *w = MsvcrtPathToW(path);
    if (!w)
        return *_errno();
    errno_t r = _waccess_s(w, mode);
    free(w);
    return r;
}

int __cdecl _access(const char *path, int mode)
{
    return _access_s(path, mode) ? -1 : 0;
}

int __cdecl _wchmod(const wchar_t *path, int mode)
{
    if (!path) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        MsvcrtDosMapError(GetLastError());
        return -1;
    }
    if (mode & _S_IWRITE)
        attr &= ~FILE_ATTRIBUTE_READONLY;
    else
        attr |= FILE_ATTRIBUTE_READONLY;
    if (!SetFileAttributesW(path, attr)) {
        MsvcrtDosMapError(GetLastError());
        return -1;
    }
    return 0;
}

int __cdecl _chmod(const char *path, int mode)
{
    if (!path) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    wchar_t *w = MsvcrtPathToW(path);
    if (!w)
        return -1;
    int r = _wchmod(w, mode);
    free(w);
    return r;
}

/* ------------------------------------------------------------------ directories */

int __cdecl _wmkdir(const wchar_t *path)
{
    if (!path) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    if (!CreateDirectoryW(path, NULL)) {
        MsvcrtDosMapError(GetLastError());
        return -1;
    }
    return 0;
}

int __cdecl _mkdir(const char *path)
{
    if (!path) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    wchar_t *w = MsvcrtPathToW(path);
    if (!w)
        return -1;
    int r = _wmkdir(w);
    free(w);
    return r;
}

int __cdecl _wrmdir(const wchar_t *path)
{
    if (!path) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    if (!RemoveDirectoryW(path)) {
        MsvcrtDosMapError(GetLastError());
        return -1;
    }
    return 0;
}

int __cdecl _rmdir(const char *path)
{
    if (!path) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    wchar_t *w = MsvcrtPathToW(path);
    if (!w)
        return -1;
    int r = _wrmdir(w);
    free(w);
    return r;
}

/* sets the "=X:" variable of the drive of the new current directory, like msvcrt. */
static void set_drive_variable(void)
{
    wchar_t cwd[MAX_PATH];
    DWORD n = GetCurrentDirectoryW(MAX_PATH, cwd);
    if (!n || n >= MAX_PATH || cwd[1] != ':')
        return;
    wchar_t name[4] = {'=', (wchar_t)towupper(cwd[0]), ':', 0};
    SetEnvironmentVariableW(name, cwd);
}

int __cdecl _wchdir(const wchar_t *path)
{
    if (!path) {
        *__doserrno() = 0;
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    if (!SetCurrentDirectoryW(path)) {
        MsvcrtDosMapError(GetLastError());
        return -1;
    }
    set_drive_variable();
    return 0;
}

int __cdecl _chdir(const char *path)
{
    if (!path) {
        *__doserrno() = 0;
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    wchar_t *w = MsvcrtPathToW(path);
    if (!w)
        return -1;
    int r = _wchdir(w);
    free(w);
    return r;
}

static wchar_t *get_wcwd(void)
{
    DWORD n = GetCurrentDirectoryW(0, NULL);
    if (!n) {
        MsvcrtDosMapError(GetLastError());
        return NULL;
    }
    wchar_t *w = malloc(n * sizeof(wchar_t));
    if (!w) {
        MsvcrtSetErrno(ENOMEM);
        return NULL;
    }
    GetCurrentDirectoryW(n, w);
    return w;
}

wchar_t *__cdecl _wgetcwd(wchar_t *buf, int size)
{
    if (buf && size <= 0) {
        MSVCRT_INVALID(EINVAL);
        return NULL;
    }
    wchar_t *cwd = get_wcwd();
    if (!cwd)
        return NULL;
    size_t n = wcslen(cwd) + 1;
    if (!buf) {
        size_t want = (size_t)size > n ? (size_t)size : n;
        buf = malloc(want * sizeof(wchar_t));
        if (!buf) {
            free(cwd);
            MsvcrtSetErrno(ENOMEM);
            return NULL;
        }
    } else if (n > (size_t)size) {
        free(cwd);
        MsvcrtSetErrno(ERANGE);
        return NULL;
    }
    memcpy(buf, cwd, n * sizeof(wchar_t));
    free(cwd);
    return buf;
}

char *__cdecl _getcwd(char *buf, int size)
{
    if (buf && size <= 0) {
        MSVCRT_INVALID(EINVAL);
        return NULL;
    }
    wchar_t *cwd = get_wcwd();
    if (!cwd)
        return NULL;
    char *r = path_to_a(cwd, buf, size > 0 ? (size_t)size : 0, 0);
    free(cwd);
    return r;
}

static int drive_valid(int drive)
{
    if (drive < 0 || drive > 26)
        return 0;
    if (!drive)
        return 1;
    wchar_t root[4] = {(wchar_t)('A' + drive - 1), ':', '\\', 0};
    return GetDriveTypeW(root) > DRIVE_NO_ROOT_DIR;
}

wchar_t *__cdecl _wgetdcwd(int drive, wchar_t *buf, int size)
{
    if (buf && size <= 0) {
        MSVCRT_INVALID(EINVAL);
        return NULL;
    }
    if (!drive)
        return _wgetcwd(buf, size);
    if (!drive_valid(drive)) {
        *__doserrno() = ERROR_INVALID_DRIVE;
        MSVCRT_INVALID(EACCES);
        return NULL;
    }
    wchar_t spec[3] = {(wchar_t)('A' + drive - 1), ':', 0};
    wchar_t tmp[MAX_PATH];
    DWORD n = GetFullPathNameW(spec, MAX_PATH, tmp, NULL);
    if (!n || n >= MAX_PATH) {
        MsvcrtDosMapError(n ? ERROR_FILENAME_EXCED_RANGE : GetLastError());
        return NULL;
    }
    size_t len = wcslen(tmp) + 1;
    if (!buf) {
        size_t want = (size_t)size > len ? (size_t)size : len;
        buf = malloc(want * sizeof(wchar_t));
        if (!buf) {
            MsvcrtSetErrno(ENOMEM);
            return NULL;
        }
    } else if (len > (size_t)size) {
        MsvcrtSetErrno(ERANGE);
        return NULL;
    }
    memcpy(buf, tmp, len * sizeof(wchar_t));
    return buf;
}

char *__cdecl _getdcwd(int drive, char *buf, int size)
{
    if (buf && size <= 0) {
        MSVCRT_INVALID(EINVAL);
        return NULL;
    }
    wchar_t *w = _wgetdcwd(drive, NULL, 0);
    if (!w)
        return NULL;
    char *r = path_to_a(w, buf, size > 0 ? (size_t)size : 0, 0);
    free(w);
    return r;
}

int MsvcrtGetDrive(void)
{
    wchar_t cwd[MAX_PATH];
    DWORD n = GetCurrentDirectoryW(MAX_PATH, cwd);
    if (!n || n >= MAX_PATH || cwd[1] != ':')
        return 0;
    return towupper(cwd[0]) - 'A' + 1;
}

int __cdecl _getdrive(void)
{
    return MsvcrtGetDrive();
}

int __cdecl _chdrive(int drive)
{
    if (drive < 1 || drive > 26) {
        *__doserrno() = ERROR_INVALID_DRIVE;
        MSVCRT_INVALID(EACCES);
        return -1;
    }
    wchar_t spec[3] = {(wchar_t)('A' + drive - 1), ':', 0};
    if (!SetCurrentDirectoryW(spec)) {
        MsvcrtDosMapError(GetLastError());
        return -1;
    }
    set_drive_variable();
    return 0;
}

unsigned long __cdecl _getdrives(void)
{
    return GetLogicalDrives();
}

struct _diskfree_t {
    unsigned total_clusters;
    unsigned avail_clusters;
    unsigned sectors_per_cluster;
    unsigned bytes_per_sector;
};

unsigned __cdecl _getdiskfree(unsigned drive, struct _diskfree_t *info)
{
    if (!info || drive > 26) {
        MSVCRT_INVALID(EINVAL);
        return ERROR_INVALID_PARAMETER;
    }
    wchar_t root[4] = {(wchar_t)('A' + drive - 1), ':', '\\', 0};
    DWORD spc, bps, free_clusters, total;
    if (!GetDiskFreeSpaceW(drive ? root : NULL, &spc, &bps, &free_clusters, &total)) {
        DWORD err = GetLastError();
        MsvcrtDosMapError(err);
        return err;
    }
    info->total_clusters = total;
    info->avail_clusters = free_clusters;
    info->sectors_per_cluster = spc;
    info->bytes_per_sector = bps;
    return 0;
}

/* ------------------------------------------------------------------ full paths */

wchar_t *MsvcrtWfullpath(wchar_t *buf, const wchar_t *path, size_t size)
{
    if (!path || !*path)
        return _wgetcwd(buf, (int)(size > INT_MAX ? INT_MAX : size));
    DWORD n = GetFullPathNameW(path, 0, NULL, NULL);
    if (!n) {
        MsvcrtDosMapError(GetLastError());
        return NULL;
    }
    int allocated = 0;
    if (!buf) {
        size_t want = n > MAX_PATH ? n : MAX_PATH;
        buf = malloc(want * sizeof(wchar_t));
        if (!buf) {
            MsvcrtSetErrno(ENOMEM);
            return NULL;
        }
        size = want;
        allocated = 1;
    } else if (n > size) {
        MsvcrtSetErrno(ERANGE);
        return NULL;
    }
    DWORD r = GetFullPathNameW(path, (DWORD)size, buf, NULL);
    if (!r || r >= size) {
        if (allocated)
            free(buf);
        if (r)
            MsvcrtSetErrno(ERANGE);
        else
            MsvcrtDosMapError(GetLastError());
        return NULL;
    }
    return buf;
}

wchar_t *__cdecl _wfullpath(wchar_t *buf, const wchar_t *path, size_t size)
{
    if (buf && !size) {
        MSVCRT_INVALID(EINVAL);
        return NULL;
    }
    return MsvcrtWfullpath(buf, path, size);
}

char *__cdecl _fullpath(char *buf, const char *path, size_t size)
{
    if (buf && !size) {
        MSVCRT_INVALID(EINVAL);
        return NULL;
    }
    wchar_t *wpath = path && *path ? MsvcrtPathToW(path) : NULL;
    if (path && *path && !wpath)
        return NULL;
    wchar_t *full = MsvcrtWfullpath(NULL, wpath, 0);
    free(wpath);
    if (!full)
        return NULL;
    char *r = path_to_a(full, buf, buf ? size : 0, MAX_PATH);
    free(full);
    return r;
}

/* ------------------------------------------------------------------ split and make paths */

#define SPLIT_TEMPLATE(NAME, T, STRLEN)                                                                           \
    static errno_t NAME(const T *path, T *drive, size_t ndrive, T *dir, size_t ndir, T *fname, size_t nfname,  \
                        T *ext, size_t next)                                                                      \
    {                                                                                                             \
        if (!path || (drive && !ndrive) || (dir && !ndir) || (fname && !nfname) || (ext && !next)) {              \
            if (drive && ndrive)                                                                                  \
                *drive = 0;                                                                                       \
            if (dir && ndir)                                                                                      \
                *dir = 0;                                                                                         \
            if (fname && nfname)                                                                                  \
                *fname = 0;                                                                                       \
            if (ext && next)                                                                                      \
                *ext = 0;                                                                                         \
            MSVCRT_INVALID(EINVAL);                                                                               \
            return EINVAL;                                                                                        \
        }                                                                                                         \
        const T *p = path;                                                                                        \
        const T *drive_end = path;                                                                                \
        if (path[0] && path[1] == ':')                                                                            \
            drive_end = path + 2;                                                                                 \
        p = drive_end;                                                                                            \
        const T *last_slash = NULL, *last_dot = NULL;                                                             \
        for (const T *q = p; *q; q++) {                                                                           \
            if (*q == '\\' || *q == '/')                                                                          \
                last_slash = q;                                                                                   \
            else if (*q == '.')                                                                                   \
                last_dot = q;                                                                                     \
        }                                                                                                         \
        const T *dir_end = last_slash ? last_slash + 1 : p;                                                       \
        const T *end = path + STRLEN(path);                                                                       \
        const T *name_end = last_dot && last_dot >= dir_end ? last_dot : end;                                     \
        struct {                                                                                                  \
            T *out;                                                                                               \
            size_t size;                                                                                          \
            const T *from, *to;                                                                                   \
        } parts[4] = {{drive, ndrive, path, drive_end},                                                           \
                      {dir, ndir, drive_end, dir_end},                                                            \
                      {fname, nfname, dir_end, name_end},                                                         \
                      {ext, next, name_end, end}};                                                                \
        for (int i = 0; i < 4; i++) {                                                                             \
            if (!parts[i].out)                                                                                    \
                continue;                                                                                         \
            size_t n = parts[i].to - parts[i].from;                                                               \
            if (n >= parts[i].size) {                                                                             \
                for (int k = 0; k < 4; k++)                                                                       \
                    if (parts[k].out)                                                                             \
                        *parts[k].out = 0;                                                                        \
                MSVCRT_INVALID(ERANGE);                                                                           \
                return ERANGE;                                                                                    \
            }                                                                                                     \
            memcpy(parts[i].out, parts[i].from, n * sizeof(T));                                                   \
            parts[i].out[n] = 0;                                                                                  \
        }                                                                                                         \
        return 0;                                                                                                 \
    }

SPLIT_TEMPLATE(split_a, char, strlen)
SPLIT_TEMPLATE(split_w, wchar_t, wcslen)

errno_t __cdecl _splitpath_s(const char *path, char *drive, size_t ndrive, char *dir, size_t ndir, char *fname,
                             size_t nfname, char *ext, size_t next)
{
    return split_a(path, drive, ndrive, dir, ndir, fname, nfname, ext, next);
}

errno_t __cdecl _wsplitpath_s(const wchar_t *path, wchar_t *drive, size_t ndrive, wchar_t *dir, size_t ndir,
                              wchar_t *fname, size_t nfname, wchar_t *ext, size_t next)
{
    return split_w(path, drive, ndrive, dir, ndir, fname, nfname, ext, next);
}

void __cdecl _splitpath(const char *path, char *drive, char *dir, char *fname, char *ext)
{
    split_a(path, drive, 3, dir, 256, fname, 256, ext, 256);
}

void __cdecl _wsplitpath(const wchar_t *path, wchar_t *drive, wchar_t *dir, wchar_t *fname, wchar_t *ext)
{
    split_w(path, drive, 3, dir, 256, fname, 256, ext, 256);
}

#define MAKE_TEMPLATE(NAME, T, STRLEN)                                                                            \
    static errno_t NAME(T *path, size_t size, const T *drive, const T *dir, const T *fname, const T *ext)        \
    {                                                                                                             \
        if (!path || !size) {                                                                                     \
            MSVCRT_INVALID(EINVAL);                                                                               \
            return EINVAL;                                                                                        \
        }                                                                                                         \
        size_t n = 0;                                                                                             \
        T sep[2] = {'\\', 0};                                                                                     \
        T dot[2] = {'.', 0};                                                                                      \
        T colon[2] = {':', 0};                                                                                    \
        const T *pieces[7] = {0};                                                                                 \
        int k = 0;                                                                                                \
        if (drive && *drive) {                                                                                    \
            pieces[k++] = drive;                                                                                  \
            if (!drive[1])                                                                                        \
                pieces[k++] = colon;                                                                              \
        }                                                                                                         \
        if (dir && *dir) {                                                                                        \
            pieces[k++] = dir;                                                                                    \
            T last = dir[STRLEN(dir) - 1];                                                                        \
            if (last != '\\' && last != '/')                                                                      \
                pieces[k++] = sep;                                                                                \
        }                                                                                                         \
        if (fname && *fname)                                                                                      \
            pieces[k++] = fname;                                                                                  \
        if (ext && *ext) {                                                                                        \
            if (*ext != '.')                                                                                      \
                pieces[k++] = dot;                                                                                \
            pieces[k++] = ext;                                                                                    \
        }                                                                                                         \
        for (int i = 0; i < k; i++) {                                                                             \
            size_t len = STRLEN(pieces[i]);                                                                       \
            if (n + len >= size) {                                                                                \
                *path = 0;                                                                                        \
                MSVCRT_INVALID(ERANGE);                                                                           \
                return ERANGE;                                                                                    \
            }                                                                                                     \
            memcpy(path + n, pieces[i], len * sizeof(T));                                                         \
            n += len;                                                                                             \
        }                                                                                                         \
        path[n] = 0;                                                                                              \
        return 0;                                                                                                 \
    }

MAKE_TEMPLATE(make_a, char, strlen)
MAKE_TEMPLATE(make_w, wchar_t, wcslen)

errno_t __cdecl _makepath_s(char *path, size_t size, const char *drive, const char *dir, const char *fname,
                            const char *ext)
{
    return make_a(path, size, drive, dir, fname, ext);
}

errno_t __cdecl _wmakepath_s(wchar_t *path, size_t size, const wchar_t *drive, const wchar_t *dir,
                             const wchar_t *fname, const wchar_t *ext)
{
    return make_w(path, size, drive, dir, fname, ext);
}

void __cdecl _makepath(char *path, const char *drive, const char *dir, const char *fname, const char *ext)
{
    make_a(path, MAX_PATH, drive, dir, fname, ext);
}

void __cdecl _wmakepath(wchar_t *path, const wchar_t *drive, const wchar_t *dir, const wchar_t *fname,
                        const wchar_t *ext)
{
    make_w(path, MAX_PATH, drive, dir, fname, ext);
}

/* ------------------------------------------------------------------ stat */

struct _stat64 {
    _dev_t st_dev;
    _ino_t st_ino;
    unsigned short st_mode;
    short st_nlink;
    short st_uid;
    short st_gid;
    _dev_t st_rdev;
    long long st_size;
    __time64_t st_atime;
    __time64_t st_mtime;
    __time64_t st_ctime;
};
MIME_FIELD_AT(struct _stat64, st_rdev, 16);
MIME_FIELD_AT(struct _stat64, st_size, 24);
MIME_FIELD_AT(struct _stat64, st_ctime, 48);

struct _stat64i32 {
    _dev_t st_dev;
    _ino_t st_ino;
    unsigned short st_mode;
    short st_nlink;
    short st_uid;
    short st_gid;
    _dev_t st_rdev;
    _off_t st_size;
    __time64_t st_atime;
    __time64_t st_mtime;
    __time64_t st_ctime;
};
MIME_FIELD_AT(struct _stat64i32, st_size, 20);
MIME_FIELD_AT(struct _stat64i32, st_atime, 24);

struct _stat32 {
    _dev_t st_dev;
    _ino_t st_ino;
    unsigned short st_mode;
    short st_nlink;
    short st_uid;
    short st_gid;
    _dev_t st_rdev;
    _off_t st_size;
    __time32_t st_atime;
    __time32_t st_mtime;
    __time32_t st_ctime;
};

static int has_exec_ext(const wchar_t *name)
{
    const wchar_t *dot = wcsrchr(name, '.');
    if (!dot)
        return 0;
    return !_wcsicmp(dot, L".exe") || !_wcsicmp(dot, L".cmd") || !_wcsicmp(dot, L".bat") ||
           !_wcsicmp(dot, L".com");
}

/* msvcrt's __wdtoxmode: the unix mode of file attributes, owner bits copied to group and others. */
unsigned short MsvcrtAttribToMode(DWORD attributes, const wchar_t *name)
{
    unsigned short mode;
    if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        mode = _S_IFDIR | _S_IEXEC;
    else
        mode = _S_IFREG;
    mode |= (attributes & FILE_ATTRIBUTE_READONLY) ? _S_IREAD : _S_IREAD | _S_IWRITE;
    if (name && has_exec_ext(name))
        mode |= _S_IEXEC;
    mode |= (mode & 0700) >> 3;
    mode |= (mode & 0700) >> 6;
    return mode;
}

static int is_root(const wchar_t *full)
{
    size_t n = wcslen(full);
    if (n == 3 && full[1] == ':' && (full[2] == '\\' || full[2] == '/'))
        return 1;
    /* \\server\share\ */
    if ((full[0] == '\\' || full[0] == '/') && (full[1] == '\\' || full[1] == '/')) {
        const wchar_t *p = full + 2;
        int seps = 0;
        for (; *p; p++)
            if (*p == '\\' || *p == '/') {
                seps++;
                if (seps == 2 && p[1])
                    return 0;
            }
        return seps == 1 || (seps == 2);
    }
    return 0;
}

static int stat_w(const wchar_t *path, struct _stat64 *st)
{
    if (!path || !st) {
        *__doserrno() = 0;
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    memset(st, 0, sizeof(*st));
    if (wcspbrk(path, L"?*")) {
        MsvcrtSetErrno(ENOENT);
        *__doserrno() = ERROR_FILE_NOT_FOUND;
        return -1;
    }
    int drive;
    if (path[0] && path[1] == ':') {
        if (!path[2]) {
            MsvcrtSetErrno(ENOENT);
            *__doserrno() = ERROR_FILE_NOT_FOUND;
            return -1;
        }
        drive = towlower(path[0]) - 'a' + 1;
    } else {
        drive = MsvcrtGetDrive();
    }
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data)) {
        /* a drive or share root may refuse attributes but still exist. */
        wchar_t *full = MsvcrtWfullpath(NULL, path, 0);
        if (full && is_root(full) && GetDriveTypeW(full) > DRIVE_NO_ROOT_DIR) {
            free(full);
            st->st_mode = MsvcrtAttribToMode(FILE_ATTRIBUTE_DIRECTORY, NULL);
            st->st_nlink = 1;
            st->st_dev = st->st_rdev = drive - 1;
            st->st_mtime = st->st_atime = st->st_ctime = MsvcrtLocalToTime(1980, 1, 1, 0, 0, 0, -1);
            return 0;
        }
        free(full);
        MsvcrtSetErrno(ENOENT);
        *__doserrno() = ERROR_FILE_NOT_FOUND;
        return -1;
    }
    st->st_mode = MsvcrtAttribToMode(data.dwFileAttributes, path);
    st->st_nlink = 1;
    st->st_size = (long long)data.nFileSizeHigh << 32 | data.nFileSizeLow;
    st->st_dev = st->st_rdev = drive - 1;
    st->st_mtime = MsvcrtFileTimeToTime(&data.ftLastWriteTime);
    st->st_atime = (data.ftLastAccessTime.dwLowDateTime || data.ftLastAccessTime.dwHighDateTime)
                       ? MsvcrtFileTimeToTime(&data.ftLastAccessTime)
                       : st->st_mtime;
    st->st_ctime = (data.ftCreationTime.dwLowDateTime || data.ftCreationTime.dwHighDateTime)
                       ? MsvcrtFileTimeToTime(&data.ftCreationTime)
                       : st->st_mtime;
    return 0;
}

static int stat_a(const char *path, struct _stat64 *st)
{
    if (!path || !st) {
        *__doserrno() = 0;
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    wchar_t *w = MsvcrtPathToW(path);
    if (!w)
        return -1;
    int r = stat_w(w, st);
    free(w);
    return r;
}

static int fstat_fd(int fd, struct _stat64 *st)
{
    if (!st) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    memset(st, 0, sizeof(*st));
    if (!MsvcrtFdValid(fd)) {
        *__doserrno() = 0;
        MSVCRT_INVALID(EBADF);
        return -1;
    }
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    DWORD type = GetFileType(h) & 0xff;
    st->st_dev = st->st_rdev = fd;
    st->st_nlink = 1;
    if (type == FILE_TYPE_CHAR || type == FILE_TYPE_PIPE) {
        st->st_mode = type == FILE_TYPE_CHAR ? _S_IFCHR : _S_IFIFO;
        if (type == FILE_TYPE_PIPE) {
            typedef BOOL(WINAPI * PEEK)(HANDLE, LPVOID, DWORD, LPDWORD, LPDWORD, LPDWORD);
            static PEEK peek;
            if (!peek) {
                HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
                peek = k32 ? (PEEK)(void *)GetProcAddress(k32, "PeekNamedPipe") : NULL;
            }
            DWORD avail = 0;
            if (peek && peek(h, NULL, 0, NULL, &avail, NULL))
                st->st_size = avail;
        }
        return 0;
    }
    if (type != FILE_TYPE_DISK) {
        if (GetLastError() != NO_ERROR) {
            MsvcrtDosMapError(GetLastError());
            return -1;
        }
        MsvcrtSetErrno(EBADF);
        return -1;
    }
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(h, &info)) {
        MsvcrtDosMapError(GetLastError());
        return -1;
    }
    st->st_mode = MsvcrtAttribToMode(info.dwFileAttributes, NULL);
    st->st_nlink = (short)info.nNumberOfLinks;
    st->st_size = (long long)info.nFileSizeHigh << 32 | info.nFileSizeLow;
    st->st_mtime = MsvcrtFileTimeToTime(&info.ftLastWriteTime);
    st->st_atime = (info.ftLastAccessTime.dwLowDateTime || info.ftLastAccessTime.dwHighDateTime)
                       ? MsvcrtFileTimeToTime(&info.ftLastAccessTime)
                       : st->st_mtime;
    st->st_ctime = (info.ftCreationTime.dwLowDateTime || info.ftCreationTime.dwHighDateTime)
                       ? MsvcrtFileTimeToTime(&info.ftCreationTime)
                       : st->st_mtime;
    return 0;
}

static int to_64i32(int r, const struct _stat64 *s, struct _stat64i32 *d)
{
    if (r)
        return r;
    d->st_dev = s->st_dev;
    d->st_ino = s->st_ino;
    d->st_mode = s->st_mode;
    d->st_nlink = s->st_nlink;
    d->st_uid = s->st_uid;
    d->st_gid = s->st_gid;
    d->st_rdev = s->st_rdev;
    d->st_size = (_off_t)s->st_size;
    d->st_atime = s->st_atime;
    d->st_mtime = s->st_mtime;
    d->st_ctime = s->st_ctime;
    return 0;
}

static int to_32(int r, const struct _stat64 *s, struct _stat32 *d)
{
    if (r)
        return r;
    d->st_dev = s->st_dev;
    d->st_ino = s->st_ino;
    d->st_mode = s->st_mode;
    d->st_nlink = s->st_nlink;
    d->st_uid = s->st_uid;
    d->st_gid = s->st_gid;
    d->st_rdev = s->st_rdev;
    d->st_size = (_off_t)s->st_size;
    d->st_atime = (__time32_t)s->st_atime;
    d->st_mtime = (__time32_t)s->st_mtime;
    d->st_ctime = (__time32_t)s->st_ctime;
    return 0;
}

int __cdecl _stat64(const char *path, struct _stat64 *st)
{
    return stat_a(path, st);
}

int __cdecl _stati64(const char *path, struct _stat64 *st)
{
    return stat_a(path, st);
}

int __cdecl _wstat64(const wchar_t *path, struct _stat64 *st)
{
    return stat_w(path, st);
}

int __cdecl _wstati64(const wchar_t *path, struct _stat64 *st)
{
    return stat_w(path, st);
}

int __cdecl _fstat64(int fd, struct _stat64 *st)
{
    return fstat_fd(fd, st);
}

int __cdecl _fstati64(int fd, struct _stat64 *st)
{
    return fstat_fd(fd, st);
}

/* the 64 bit msvcrt's _stat uses 64 bit times and a 32 bit size. */
int __cdecl _stat(const char *path, struct _stat64i32 *st)
{
    struct _stat64 s;
    return to_64i32(stat_a(path, &s), &s, st);
}

int __cdecl _wstat(const wchar_t *path, struct _stat64i32 *st)
{
    struct _stat64 s;
    return to_64i32(stat_w(path, &s), &s, st);
}

int __cdecl _fstat(int fd, struct _stat64i32 *st)
{
    struct _stat64 s;
    return to_64i32(fstat_fd(fd, &s), &s, st);
}

int __cdecl _stat32(const char *path, struct _stat32 *st)
{
    struct _stat64 s;
    return to_32(stat_a(path, &s), &s, st);
}

int __cdecl _wstat32(const wchar_t *path, struct _stat32 *st)
{
    struct _stat64 s;
    return to_32(stat_w(path, &s), &s, st);
}

int __cdecl _fstat32(int fd, struct _stat32 *st)
{
    struct _stat64 s;
    return to_32(fstat_fd(fd, &s), &s, st);
}

/* ------------------------------------------------------------------ find */

struct _finddata64_t {
    unsigned attrib;
    __time64_t time_create;
    __time64_t time_access;
    __time64_t time_write;
    long long size;
    char name[260];
};
MIME_FIELD_AT(struct _finddata64_t, size, 32);
MIME_FIELD_AT(struct _finddata64_t, name, 40);

struct _wfinddata64_t {
    unsigned attrib;
    __time64_t time_create;
    __time64_t time_access;
    __time64_t time_write;
    long long size;
    wchar_t name[260];
};

/* the 64 bit msvcrt's _finddata_t: 64 bit times and a 32 bit size. */
struct _finddata64i32_t {
    unsigned attrib;
    __time64_t time_create;
    __time64_t time_access;
    __time64_t time_write;
    unsigned long size;
    char name[260];
};
MIME_FIELD_AT(struct _finddata64i32_t, name, 36);

struct _wfinddata64i32_t {
    unsigned attrib;
    __time64_t time_create;
    __time64_t time_access;
    __time64_t time_write;
    unsigned long size;
    wchar_t name[260];
};

/* the fields every find structure shares; times are -1 when the file system has none. */
static void find_common(const WIN32_FIND_DATAW *fd, unsigned *attrib, __time64_t *create, __time64_t *access,
                        __time64_t *write)
{
    *attrib = fd->dwFileAttributes == FILE_ATTRIBUTE_NORMAL ? 0 : fd->dwFileAttributes;
    *write = MsvcrtFileTimeToTime(&fd->ftLastWriteTime);
    *create = (fd->ftCreationTime.dwLowDateTime || fd->ftCreationTime.dwHighDateTime)
                  ? MsvcrtFileTimeToTime(&fd->ftCreationTime)
                  : -1;
    *access = (fd->ftLastAccessTime.dwLowDateTime || fd->ftLastAccessTime.dwHighDateTime)
                  ? MsvcrtFileTimeToTime(&fd->ftLastAccessTime)
                  : -1;
}

static void find_fail(void)
{
    DWORD err = GetLastError();
    switch (err) {
    case ERROR_NO_MORE_FILES:
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME:
        MsvcrtSetErrno(ENOENT);
        break;
    case ERROR_NOT_ENOUGH_MEMORY:
        MsvcrtSetErrno(ENOMEM);
        break;
    case ERROR_FILE_EXISTS:
        MsvcrtSetErrno(EEXIST);
        break;
    default:
        MsvcrtSetErrno(EINVAL);
        break;
    }
}

/* the narrow name of a find result in the file api code page; 0 when it does not convert. */
static int find_name_a(const WIN32_FIND_DATAW *fd, char *name)
{
    UINT cp = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
    BOOL used = FALSE;
    if (!WideCharToMultiByte(cp, 0, fd->cFileName, -1, name, 260, NULL, &used)) {
        MsvcrtSetErrno(EINVAL);
        return 0;
    }
    return 1;
}

static int fill64(const WIN32_FIND_DATAW *fd, struct _finddata64_t *out)
{
    find_common(fd, &out->attrib, &out->time_create, &out->time_access, &out->time_write);
    out->size = (long long)fd->nFileSizeHigh << 32 | fd->nFileSizeLow;
    return find_name_a(fd, out->name);
}

static int wfill64(const WIN32_FIND_DATAW *fd, struct _wfinddata64_t *out)
{
    find_common(fd, &out->attrib, &out->time_create, &out->time_access, &out->time_write);
    out->size = (long long)fd->nFileSizeHigh << 32 | fd->nFileSizeLow;
    wcscpy(out->name, fd->cFileName);
    return 1;
}

static int fill64i32(const WIN32_FIND_DATAW *fd, struct _finddata64i32_t *out)
{
    find_common(fd, &out->attrib, &out->time_create, &out->time_access, &out->time_write);
    out->size = fd->nFileSizeLow;
    return find_name_a(fd, out->name);
}

static int wfill64i32(const WIN32_FIND_DATAW *fd, struct _wfinddata64i32_t *out)
{
    find_common(fd, &out->attrib, &out->time_create, &out->time_access, &out->time_write);
    out->size = fd->nFileSizeLow;
    wcscpy(out->name, fd->cFileName);
    return 1;
}

static intptr_t find_first(const wchar_t *spec, WIN32_FIND_DATAW *fd)
{
    HANDLE h = FindFirstFileExW(spec, FindExInfoStandard, fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE) {
        find_fail();
        return -1;
    }
    return (intptr_t)h;
}

static intptr_t find_first_a(const char *spec, WIN32_FIND_DATAW *fd)
{
    if (!spec) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    wchar_t *w = MsvcrtPathToW(spec);
    if (!w)
        return -1;
    intptr_t h = find_first(w, fd);
    free(w);
    return h;
}

static int find_next(intptr_t h, WIN32_FIND_DATAW *fd)
{
    if (!FindNextFileW((HANDLE)h, fd)) {
        find_fail();
        return -1;
    }
    return 0;
}

intptr_t __cdecl _findfirst64(const char *spec, struct _finddata64_t *out)
{
    if (!out) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    WIN32_FIND_DATAW fd;
    intptr_t h = find_first_a(spec, &fd);
    if (h != -1 && !fill64(&fd, out)) {
        FindClose((HANDLE)h);
        return -1;
    }
    return h;
}

int __cdecl _findnext64(intptr_t h, struct _finddata64_t *out)
{
    if (!out) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    WIN32_FIND_DATAW fd;
    if (find_next(h, &fd))
        return -1;
    return fill64(&fd, out) ? 0 : -1;
}

intptr_t __cdecl _wfindfirst64(const wchar_t *spec, struct _wfinddata64_t *out)
{
    if (!spec || !out) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    WIN32_FIND_DATAW fd;
    intptr_t h = find_first(spec, &fd);
    if (h != -1)
        wfill64(&fd, out);
    return h;
}

int __cdecl _wfindnext64(intptr_t h, struct _wfinddata64_t *out)
{
    if (!out) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    WIN32_FIND_DATAW fd;
    if (find_next(h, &fd))
        return -1;
    wfill64(&fd, out);
    return 0;
}

intptr_t __cdecl _findfirsti64(const char *spec, struct _finddata64_t *out)
{
    return _findfirst64(spec, out);
}

int __cdecl _findnexti64(intptr_t h, struct _finddata64_t *out)
{
    return _findnext64(h, out);
}

intptr_t __cdecl _wfindfirsti64(const wchar_t *spec, struct _wfinddata64_t *out)
{
    return _wfindfirst64(spec, out);
}

int __cdecl _wfindnexti64(intptr_t h, struct _wfinddata64_t *out)
{
    return _wfindnext64(h, out);
}

intptr_t __cdecl _findfirst(const char *spec, struct _finddata64i32_t *out)
{
    if (!out) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    WIN32_FIND_DATAW fd;
    intptr_t h = find_first_a(spec, &fd);
    if (h != -1 && !fill64i32(&fd, out)) {
        FindClose((HANDLE)h);
        return -1;
    }
    return h;
}

int __cdecl _findnext(intptr_t h, struct _finddata64i32_t *out)
{
    if (!out) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    WIN32_FIND_DATAW fd;
    if (find_next(h, &fd))
        return -1;
    return fill64i32(&fd, out) ? 0 : -1;
}

intptr_t __cdecl _wfindfirst(const wchar_t *spec, struct _wfinddata64i32_t *out)
{
    if (!spec || !out) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    WIN32_FIND_DATAW fd;
    intptr_t h = find_first(spec, &fd);
    if (h != -1)
        wfill64i32(&fd, out);
    return h;
}

int __cdecl _wfindnext(intptr_t h, struct _wfinddata64i32_t *out)
{
    if (!out) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    WIN32_FIND_DATAW fd;
    if (find_next(h, &fd))
        return -1;
    wfill64i32(&fd, out);
    return 0;
}

int __cdecl _findclose(intptr_t h)
{
    if (!FindClose((HANDLE)h)) {
        MsvcrtSetErrno(EINVAL);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ file times */

struct __utimbuf64 {
    __time64_t actime;
    __time64_t modtime;
};

struct __utimbuf32 {
    __time32_t actime;
    __time32_t modtime;
};

static void time_to_filetime(__time64_t t, FILETIME *ft)
{
    ULONGLONG v = (ULONGLONG)t * 10000000 + 116444736000000000ull;
    ft->dwLowDateTime = (DWORD)v;
    ft->dwHighDateTime = (DWORD)(v >> 32);
}

int __cdecl _futime64(int fd, struct __utimbuf64 *times)
{
    if (!MsvcrtFdValid(fd)) {
        *__doserrno() = 0;
        MSVCRT_INVALID(EBADF);
        return -1;
    }
    struct __utimbuf64 now;
    if (!times) {
        now.actime = now.modtime = _time64(NULL);
        times = &now;
    }
    if (times->actime < 0 || times->modtime < 0) {
        MsvcrtSetErrno(EINVAL);
        return -1;
    }
    FILETIME access, write;
    time_to_filetime(times->actime, &access);
    time_to_filetime(times->modtime, &write);
    if (!SetFileTime((HANDLE)_get_osfhandle(fd), NULL, &access, &write)) {
        MsvcrtSetErrno(EINVAL);
        return -1;
    }
    return 0;
}

int __cdecl _wutime64(const wchar_t *path, struct __utimbuf64 *times)
{
    if (!path) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    int fd = MsvcrtOpenW(path, _O_RDWR | _O_BINARY, _SH_DENYNO, 0, 0);
    if (fd < 0)
        return -1;
    int r = _futime64(fd, times);
    int saved = *_errno();
    _close(fd);
    *_errno() = saved;
    return r;
}

int __cdecl _utime64(const char *path, struct __utimbuf64 *times)
{
    if (!path) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    wchar_t *w = MsvcrtPathToW(path);
    if (!w)
        return -1;
    int r = _wutime64(w, times);
    free(w);
    return r;
}

int __cdecl _futime(int fd, struct __utimbuf64 *times)
{
    return _futime64(fd, times);
}

int __cdecl _utime(const char *path, struct __utimbuf64 *times)
{
    return _utime64(path, times);
}

int __cdecl _wutime(const wchar_t *path, struct __utimbuf64 *times)
{
    return _wutime64(path, times);
}

static struct __utimbuf64 *widen_times(const struct __utimbuf32 *t, struct __utimbuf64 *out)
{
    if (!t)
        return NULL;
    out->actime = t->actime;
    out->modtime = t->modtime;
    return out;
}

int __cdecl _futime32(int fd, struct __utimbuf32 *times)
{
    struct __utimbuf64 t;
    return _futime64(fd, widen_times(times, &t));
}

int __cdecl _utime32(const char *path, struct __utimbuf32 *times)
{
    struct __utimbuf64 t;
    return _utime64(path, widen_times(times, &t));
}

int __cdecl _wutime32(const wchar_t *path, struct __utimbuf32 *times)
{
    struct __utimbuf64 t;
    return _wutime64(path, widen_times(times, &t));
}

/* ------------------------------------------------------------------ temporary names */

/* replaces the trailing XXXXXX with a letter and the process id, like msvcrt. */
#define MKTEMP_TEMPLATE(NAME, T, STRLEN, EXISTS)                                                                  \
    static errno_t NAME(T *tmpl, size_t size)                                                                     \
    {                                                                                                             \
        if (!tmpl || !size) {                                                                                     \
            MSVCRT_INVALID(EINVAL);                                                                               \
            return EINVAL;                                                                                        \
        }                                                                                                         \
        size_t len = STRLEN(tmpl);                                                                                \
        if (len + 1 > size || len < 6) {                                                                          \
            MSVCRT_INVALID(EINVAL);                                                                               \
            return EINVAL;                                                                                        \
        }                                                                                                         \
        T *x = tmpl + len - 6;                                                                                    \
        for (int i = 0; i < 6; i++)                                                                               \
            if (x[i] != 'X') {                                                                                    \
                MSVCRT_INVALID(EINVAL);                                                                           \
                return EINVAL;                                                                                    \
            }                                                                                                     \
        unsigned pid = GetCurrentProcessId();                                                                     \
        for (int i = 5; i >= 1; i--) {                                                                            \
            x[i] = (T)('0' + pid % 10);                                                                           \
            pid /= 10;                                                                                            \
        }                                                                                                         \
        for (T letter = 'a'; letter <= 'z'; letter++) {                                                           \
            x[0] = letter;                                                                                        \
            if (!EXISTS(tmpl))                                                                                    \
                return 0;                                                                                         \
        }                                                                                                         \
        *tmpl = 0;                                                                                                \
        MsvcrtSetErrno(EEXIST);                                                                                   \
        return EEXIST;                                                                                            \
    }

static int exists_a(const char *name)
{
    return _access(name, 0) == 0 || *_errno() != ENOENT;
}

static int exists_w(const wchar_t *name)
{
    return _waccess(name, 0) == 0 || *_errno() != ENOENT;
}

MKTEMP_TEMPLATE(mktemp_a, char, strlen, exists_a)
MKTEMP_TEMPLATE(mktemp_w, wchar_t, wcslen, exists_w)

errno_t __cdecl _mktemp_s(char *tmpl, size_t size)
{
    int saved = *_errno();
    errno_t r = mktemp_a(tmpl, size);
    if (!r)
        *_errno() = saved;
    return r;
}

errno_t __cdecl _wmktemp_s(wchar_t *tmpl, size_t size)
{
    int saved = *_errno();
    errno_t r = mktemp_w(tmpl, size);
    if (!r)
        *_errno() = saved;
    return r;
}

char *__cdecl _mktemp(char *tmpl)
{
    return tmpl && !_mktemp_s(tmpl, strlen(tmpl) + 1) ? tmpl : NULL;
}

wchar_t *__cdecl _wmktemp(wchar_t *tmpl)
{
    return tmpl && !_wmktemp_s(tmpl, wcslen(tmpl) + 1) ? tmpl : NULL;
}
