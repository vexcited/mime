/* backup and restore: the database files of an instance copied out and back, the external backup calls that read them
   in pieces, and the information about the instance that a backup program asks for. */
#include "ese.h"
#include "api.h"

#define LOCK() EnterCriticalSection(&g_ese_lock)
#define UNLOCK() LeaveCriticalSection(&g_ese_lock)

typedef struct BFILE {
    struct BFILE *next;
    HANDLE file;
    INSTANCE *inst;
    ULONG64 id;
} BFILE;

static BFILE *g_files;
static ULONG64 g_next_file_id = 1;

static INSTANCE *backup_instance(JET_INSTANCE handle, JET_ERR *err)
{
    INSTANCE *inst = handle && handle != JET_instanceNil ? ese_instance(handle) : nullptr;
    if (!inst) {
        for (INSTANCE *i = g_instances; i; i = i->next)
            if (i->legacy)
                inst = i;
    }
    if (!inst) {
        *err = handle && handle != JET_instanceNil ? JET_errInvalidInstance : JET_errNotInitialized;
        return nullptr;
    }
    if (!inst->initialized) {
        *err = JET_errNotInitialized;
        return nullptr;
    }
    *err = JET_errSuccess;
    return inst;
}

static void file_name(const WCHAR *path, WCHAR *out)
{
    const WCHAR *slash = wcsrchr(path, L'\\');
    lstrcpyW(out, slash ? slash + 1 : path);
}

/* every database of an instance is written out and copied into the directory. */
static JET_ERR backup_instance_files(INSTANCE *inst, LPCWSTR dir, JET_GRBIT grbit)
{
    if (!dir || !*dir)
        return JET_errInvalidParameter;
    if (grbit & ~(0x1 | 0x2 | 0x4 | 0x10 | 0x20 | 0x40))
        return JET_errInvalidGrbit;
    DWORD attributes = GetFileAttributesW(dir);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryW(dir, nullptr))
            return JET_errInvalidPath;
    } else if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return JET_errInvalidPath;
    }
    for (DB *db = inst->dbs; db; db = db->next) {
        ese_save_db(db, FALSE);
        WCHAR name[MAX_PATH], target[MAX_PATH];
        file_name(db->path, name);
        lstrcpyW(target, dir);
        ULONG n = (ULONG)lstrlenW(target);
        if (n && target[n - 1] != L'\\')
            lstrcatW(target, L"\\");
        lstrcatW(target, name);
        if (!CopyFileW(db->path, target, FALSE))
            return JET_errFileAccessDenied;
    }
    return JET_errSuccess;
}

static JET_ERR backup_instance_impl(JET_INSTANCE handle, LPCWSTR dir, JET_GRBIT grbit)
{
    JET_ERR err;
    LOCK();
    INSTANCE *inst = backup_instance(handle, &err);
    if (inst) {
        if (inst->backup_active)
            err = JET_errBackupInProgress;
        else if (inst->params.values[JET_paramCircularLog] && !(grbit & 0x4) && FALSE)
            err = JET_errInvalidBackupSequence;
        else
            err = backup_instance_files(inst, dir, grbit);
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetBackupInstanceW(JET_INSTANCE inst, LPCWSTR dir, JET_GRBIT grbit, void *status)
{
    return backup_instance_impl(inst, dir, grbit);
}

JET_ERR WINAPI JetBackupInstanceA(JET_INSTANCE inst, LPCSTR dir, JET_GRBIT grbit, void *status)
{
    WCHAR w[MAX_PATH];
    api_wide(dir, w, MAX_PATH);
    return backup_instance_impl(inst, dir ? w : nullptr, grbit);
}

JET_ERR WINAPI JetBackupInstance(JET_INSTANCE inst, LPCSTR dir, JET_GRBIT grbit, void *status)
{
    return JetBackupInstanceA(inst, dir, grbit, status);
}

JET_ERR WINAPI JetBackupW(LPCWSTR dir, JET_GRBIT grbit, void *status)
{
    return backup_instance_impl(0, dir, grbit);
}

JET_ERR WINAPI JetBackupA(LPCSTR dir, JET_GRBIT grbit, void *status)
{
    return JetBackupInstanceA(0, dir, grbit, status);
}

JET_ERR WINAPI JetBackup(LPCSTR dir, JET_GRBIT grbit, void *status)
{
    return JetBackupInstanceA(0, dir, grbit, status);
}

/* a restore puts the databases of a backup back where they were attached from, to the directory named or the old place. */
static JET_ERR restore_impl(JET_INSTANCE handle, LPCWSTR source, LPCWSTR dest)
{
    JET_ERR err;
    LOCK();
    INSTANCE *inst = nullptr;
    if (handle) {
        inst = ese_instance(handle);
        if (!inst)
            err = JET_errInvalidInstance;
        else if (inst->initialized)
            err = JET_errAlreadyInitialized;
        else
            err = JET_errSuccess;
    } else {
        err = JET_errSuccess;
        for (INSTANCE *i = g_instances; i; i = i->next)
            if (i->initialized)
                err = JET_errAlreadyInitialized;
    }
    if (!err && (!source || !*source))
        err = JET_errInvalidParameter;
    if (!err) {
        DWORD attributes = GetFileAttributesW(source);
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            err = JET_errFileNotFound;
        } else if (dest && *dest) {
            WIN32_FIND_DATAW find;
            WCHAR pattern[MAX_PATH];
            lstrcpyW(pattern, source);
            lstrcatW(pattern, L"\\*.edb");
            HANDLE h = FindFirstFileW(pattern, &find);
            if (h == INVALID_HANDLE_VALUE) {
                err = JET_errFileNotFound;
            } else {
                CreateDirectoryW(dest, nullptr);
                do {
                    WCHAR from[MAX_PATH], to[MAX_PATH];
                    lstrcpyW(from, source);
                    lstrcatW(from, L"\\");
                    lstrcatW(from, find.cFileName);
                    lstrcpyW(to, dest);
                    lstrcatW(to, L"\\");
                    lstrcatW(to, find.cFileName);
                    if (!CopyFileW(from, to, FALSE))
                        err = JET_errFileAccessDenied;
                } while (!err && FindNextFileW(h, &find));
                FindClose(h);
            }
        } else {
            err = JET_errFileNotFound;
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetRestoreInstanceW(JET_INSTANCE inst, LPCWSTR source, LPCWSTR dest, void *status)
{
    return restore_impl(inst, source, dest);
}

JET_ERR WINAPI JetRestoreInstanceA(JET_INSTANCE inst, LPCSTR source, LPCSTR dest, void *status)
{
    WCHAR a[MAX_PATH], b[MAX_PATH];
    api_wide(source, a, MAX_PATH);
    api_wide(dest, b, MAX_PATH);
    return restore_impl(inst, source ? a : nullptr, dest ? b : nullptr);
}

JET_ERR WINAPI JetRestoreInstance(JET_INSTANCE inst, LPCSTR source, LPCSTR dest, void *status)
{
    return JetRestoreInstanceA(inst, source, dest, status);
}

JET_ERR WINAPI JetRestoreW(LPCWSTR source, void *status)
{
    return restore_impl(0, source, nullptr);
}

JET_ERR WINAPI JetRestoreA(LPCSTR source, void *status)
{
    return JetRestoreInstanceA(0, source, nullptr, status);
}

JET_ERR WINAPI JetRestore(LPCSTR source, void *status)
{
    return JetRestoreInstanceA(0, source, nullptr, status);
}

JET_ERR WINAPI JetRestore2W(LPCWSTR source, LPCWSTR dest, void *status)
{
    return restore_impl(0, source, dest);
}

JET_ERR WINAPI JetRestore2A(LPCSTR source, LPCSTR dest, void *status)
{
    return JetRestoreInstanceA(0, source, dest, status);
}

JET_ERR WINAPI JetRestore2(LPCSTR source, LPCSTR dest, void *status)
{
    return JetRestoreInstanceA(0, source, dest, status);
}

/* ------------------------------------------------------------------ external backup */

JET_ERR WINAPI JetBeginExternalBackupInstance(JET_INSTANCE handle, JET_GRBIT grbit)
{
    JET_ERR err;
    LOCK();
    INSTANCE *inst = backup_instance(handle, &err);
    if (inst) {
        if (grbit & ~(0x1 | 0x2 | 0x4))
            err = JET_errInvalidGrbit;
        else if (inst->backup_active)
            err = JET_errBackupInProgress;
        else
            inst->backup_active = TRUE;
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetBeginExternalBackup(JET_GRBIT grbit)
{
    return JetBeginExternalBackupInstance(0, grbit);
}

JET_ERR WINAPI JetEndExternalBackupInstance2(JET_INSTANCE handle, JET_GRBIT grbit)
{
    JET_ERR err;
    LOCK();
    INSTANCE *inst = backup_instance(handle, &err);
    if (inst) {
        if (!inst->backup_active)
            err = JET_errNoBackup;
        else {
            inst->backup_active = FALSE;
            for (BFILE **link = &g_files; *link;) {
                BFILE *f = *link;
                if (f->inst == inst) {
                    *link = f->next;
                    CloseHandle(f->file);
                    ese_free(f);
                } else {
                    link = &f->next;
                }
            }
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetEndExternalBackupInstance(JET_INSTANCE handle)
{
    return JetEndExternalBackupInstance2(handle, 0);
}

JET_ERR WINAPI JetEndExternalBackup(void)
{
    return JetEndExternalBackupInstance2(0, 0);
}

static JET_ERR double_string_out(const WCHAR *names, ULONG chars, void *out, ULONG cb, ULONG *actual, BOOL wide)
{
    ULONG need = wide ? chars * sizeof(WCHAR) : 0;
    if (!wide) {
        int n = WideCharToMultiByte(CP_ACP, 0, names, (int)chars, nullptr, 0, nullptr, nullptr);
        need = n > 0 ? (ULONG)n : 0;
    }
    if (actual)
        *actual = need;
    if (!out)
        return actual ? JET_errSuccess : JET_errInvalidParameter;
    if (cb < need) {
        if (wide)
            memcpy(out, names, cb & ~1u);
        else
            WideCharToMultiByte(CP_ACP, 0, names, (int)chars, out, (int)cb, nullptr, nullptr);
        return JET_wrnBufferTruncated;
    }
    if (wide)
        memcpy(out, names, need);
    else
        WideCharToMultiByte(CP_ACP, 0, names, (int)chars, out, (int)cb, nullptr, nullptr);
    return JET_errSuccess;
}

static JET_ERR attach_info(JET_INSTANCE handle, void *out, ULONG cb, ULONG *actual, BOOL wide)
{
    JET_ERR err;
    LOCK();
    INSTANCE *inst = backup_instance(handle, &err);
    if (inst) {
        if (!inst->backup_active)
            err = JET_errNoBackup;
        else {
            WCHAR names[4096];
            ULONG chars = 0;
            for (DB *db = inst->dbs; db; db = db->next) {
                ULONG n = (ULONG)lstrlenW(db->path) + 1;
                if (chars + n + 1 >= 4096)
                    break;
                lstrcpyW(names + chars, db->path);
                chars += n;
            }
            names[chars++] = 0;
            err = double_string_out(names, chars, out, cb, actual, wide);
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetGetAttachInfoInstanceW(JET_INSTANCE inst, WCHAR *out, ULONG cb, ULONG *actual)
{
    return attach_info(inst, out, cb, actual, TRUE);
}

JET_ERR WINAPI JetGetAttachInfoInstanceA(JET_INSTANCE inst, char *out, ULONG cb, ULONG *actual)
{
    return attach_info(inst, out, cb, actual, FALSE);
}

JET_ERR WINAPI JetGetAttachInfoInstance(JET_INSTANCE inst, char *out, ULONG cb, ULONG *actual)
{
    return attach_info(inst, out, cb, actual, FALSE);
}

JET_ERR WINAPI JetGetAttachInfoW(WCHAR *out, ULONG cb, ULONG *actual)
{
    return attach_info(0, out, cb, actual, TRUE);
}

JET_ERR WINAPI JetGetAttachInfoA(char *out, ULONG cb, ULONG *actual)
{
    return attach_info(0, out, cb, actual, FALSE);
}

JET_ERR WINAPI JetGetAttachInfo(char *out, ULONG cb, ULONG *actual)
{
    return attach_info(0, out, cb, actual, FALSE);
}

static JET_ERR log_info(JET_INSTANCE handle, void *out, ULONG cb, ULONG *actual, BOOL wide)
{
    JET_ERR err;
    LOCK();
    INSTANCE *inst = backup_instance(handle, &err);
    if (inst) {
        if (!inst->backup_active)
            err = JET_errNoBackup;
        else {
            const WCHAR names[1] = {0};
            err = double_string_out(names, 1, out, cb, actual, wide);
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetGetLogInfoInstanceW(JET_INSTANCE inst, WCHAR *out, ULONG cb, ULONG *actual)
{
    return log_info(inst, out, cb, actual, TRUE);
}

JET_ERR WINAPI JetGetLogInfoInstanceA(JET_INSTANCE inst, char *out, ULONG cb, ULONG *actual)
{
    return log_info(inst, out, cb, actual, FALSE);
}

JET_ERR WINAPI JetGetLogInfoInstance(JET_INSTANCE inst, char *out, ULONG cb, ULONG *actual)
{
    return log_info(inst, out, cb, actual, FALSE);
}

JET_ERR WINAPI JetGetLogInfoInstance2W(JET_INSTANCE inst, WCHAR *out, ULONG cb, ULONG *actual, void *info)
{
    return log_info(inst, out, cb, actual, TRUE);
}

JET_ERR WINAPI JetGetLogInfoInstance2A(JET_INSTANCE inst, char *out, ULONG cb, ULONG *actual, void *info)
{
    return log_info(inst, out, cb, actual, FALSE);
}

JET_ERR WINAPI JetGetLogInfoInstance2(JET_INSTANCE inst, char *out, ULONG cb, ULONG *actual, void *info)
{
    return log_info(inst, out, cb, actual, FALSE);
}

JET_ERR WINAPI JetGetLogInfoW(WCHAR *out, ULONG cb, ULONG *actual)
{
    return log_info(0, out, cb, actual, TRUE);
}

JET_ERR WINAPI JetGetLogInfoA(char *out, ULONG cb, ULONG *actual)
{
    return log_info(0, out, cb, actual, FALSE);
}

JET_ERR WINAPI JetGetLogInfo(char *out, ULONG cb, ULONG *actual)
{
    return log_info(0, out, cb, actual, FALSE);
}

JET_ERR WINAPI JetGetTruncateLogInfoInstanceW(JET_INSTANCE inst, WCHAR *out, ULONG cb, ULONG *actual)
{
    return log_info(inst, out, cb, actual, TRUE);
}

JET_ERR WINAPI JetGetTruncateLogInfoInstanceA(JET_INSTANCE inst, char *out, ULONG cb, ULONG *actual)
{
    return log_info(inst, out, cb, actual, FALSE);
}

JET_ERR WINAPI JetGetTruncateLogInfoInstance(JET_INSTANCE inst, char *out, ULONG cb, ULONG *actual)
{
    return log_info(inst, out, cb, actual, FALSE);
}

static JET_ERR open_file(JET_INSTANCE handle, LPCWSTR path, JET_HANDLE *out, ULONG *low, ULONG *high)
{
    JET_ERR err;
    LOCK();
    INSTANCE *inst = backup_instance(handle, &err);
    if (inst) {
        if (!inst->backup_active)
            err = JET_errNoBackup;
        else if (!path || !out)
            err = JET_errInvalidParameter;
        else {
            DB *found = nullptr;
            for (DB *db = inst->dbs; db; db = db->next)
                if (!lstrcmpiW(db->path, path))
                    found = db;
            if (!found) {
                err = GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES ? JET_errFileNotFound : JET_errInvalidParameter;
            } else {
                ese_save_db(found, FALSE);
                HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h == INVALID_HANDLE_VALUE) {
                    err = JET_errFileAccessDenied;
                } else {
                    BFILE *f = ese_alloc(sizeof(*f));
                    f->file = h;
                    f->inst = inst;
                    f->id = g_next_file_id++;
                    f->next = g_files;
                    g_files = f;
                    LARGE_INTEGER size;
                    GetFileSizeEx(h, &size);
                    if (low)
                        *low = size.LowPart;
                    if (high)
                        *high = size.HighPart;
                    *out = (JET_HANDLE)f;
                }
            }
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetOpenFileInstanceW(JET_INSTANCE inst, LPCWSTR path, JET_HANDLE *out, ULONG *low, ULONG *high)
{
    return open_file(inst, path, out, low, high);
}

JET_ERR WINAPI JetOpenFileInstanceA(JET_INSTANCE inst, LPCSTR path, JET_HANDLE *out, ULONG *low, ULONG *high)
{
    WCHAR w[MAX_PATH];
    api_wide(path, w, MAX_PATH);
    return open_file(inst, path ? w : nullptr, out, low, high);
}

JET_ERR WINAPI JetOpenFileInstance(JET_INSTANCE inst, LPCSTR path, JET_HANDLE *out, ULONG *low, ULONG *high)
{
    return JetOpenFileInstanceA(inst, path, out, low, high);
}

JET_ERR WINAPI JetOpenFileW(LPCWSTR path, JET_HANDLE *out, ULONG *low, ULONG *high)
{
    return open_file(0, path, out, low, high);
}

JET_ERR WINAPI JetOpenFileA(LPCSTR path, JET_HANDLE *out, ULONG *low, ULONG *high)
{
    return JetOpenFileInstanceA(0, path, out, low, high);
}

JET_ERR WINAPI JetOpenFile(LPCSTR path, JET_HANDLE *out, ULONG *low, ULONG *high)
{
    return JetOpenFileInstanceA(0, path, out, low, high);
}

static BFILE *find_file(JET_HANDLE handle)
{
    for (BFILE *f = g_files; f; f = f->next)
        if ((JET_HANDLE)f == handle)
            return f;
    return nullptr;
}

JET_ERR WINAPI JetReadFileInstance(JET_INSTANCE handle, JET_HANDLE file, void *buffer, ULONG cb, ULONG *actual)
{
    JET_ERR err;
    LOCK();
    INSTANCE *inst = backup_instance(handle, &err);
    if (inst) {
        BFILE *f = find_file(file);
        if (!inst->backup_active)
            err = JET_errNoBackup;
        else if (!f || f->inst != inst)
            err = JET_errInvalidParameter;
        else if (!buffer)
            err = JET_errInvalidParameter;
        else {
            DWORD got = 0;
            if (!ReadFile(f->file, buffer, cb, &got, nullptr))
                err = JET_errDiskIO;
            else if (actual)
                *actual = got;
            if (!err && got < cb)
                err = JET_errSuccess;
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetReadFile(JET_HANDLE file, void *buffer, ULONG cb, ULONG *actual)
{
    return JetReadFileInstance(0, file, buffer, cb, actual);
}

JET_ERR WINAPI JetCloseFileInstance(JET_INSTANCE handle, JET_HANDLE file)
{
    JET_ERR err;
    LOCK();
    INSTANCE *inst = backup_instance(handle, &err);
    if (inst) {
        BFILE *f = find_file(file);
        if (!f || f->inst != inst)
            err = JET_errInvalidParameter;
        else {
            for (BFILE **link = &g_files; *link; link = &(*link)->next)
                if (*link == f) {
                    *link = f->next;
                    break;
                }
            CloseHandle(f->file);
            ese_free(f);
        }
    }
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetCloseFile(JET_HANDLE file)
{
    return JetCloseFileInstance(0, file);
}

JET_ERR WINAPI JetTruncateLogInstance(JET_INSTANCE handle)
{
    JET_ERR err;
    LOCK();
    INSTANCE *inst = backup_instance(handle, &err);
    if (inst && !inst->backup_active)
        err = JET_errNoBackup;
    UNLOCK();
    return err;
}

JET_ERR WINAPI JetTruncateLog(void)
{
    return JetTruncateLogInstance(0);
}

/* ------------------------------------------------------------------ the instances there are */

static JET_ERR instance_info(ULONG *count, void **out, BOOL wide)
{
    if (!count || !out)
        return JET_errInvalidParameter;
    LOCK();
    ULONG n = 0;
    for (INSTANCE *inst = g_instances; inst; inst = inst->next)
        if (inst->initialized)
            n++;
    SIZE_T per = wide ? sizeof(WCHAR) : 1;
    BYTE *block = ese_alloc(n * sizeof(JET_INSTANCE_INFO) + 1);
    JET_INSTANCE_INFO *infos = (JET_INSTANCE_INFO *)block;
    ULONG i = 0;
    for (INSTANCE *inst = g_instances; inst; inst = inst->next) {
        if (!inst->initialized)
            continue;
        JET_INSTANCE_INFO *info = &infos[i++];
        info->hInstanceId = (JET_INSTANCE)inst;
        SIZE_T len = lstrlenW(inst->name) + 1;
        void *name = HeapAlloc(GetProcessHeap(), 0, len * per);
        if (wide)
            lstrcpyW(name, inst->name);
        else
            api_ansi_out(inst->name, name, (ULONG)len);
        info->szInstanceName = name;
        ULONG dbs = 0;
        for (DB *db = inst->dbs; db; db = db->next)
            dbs++;
        info->cDatabases = dbs;
        info->szDatabaseFileName = HeapAlloc(GetProcessHeap(), 0, (dbs + 1) * sizeof(void *));
        info->szDatabaseDisplayName = HeapAlloc(GetProcessHeap(), 0, (dbs + 1) * sizeof(void *));
        info->szDatabaseSLVFileName = HeapAlloc(GetProcessHeap(), 0, (dbs + 1) * sizeof(void *));
        ULONG k = 0;
        for (DB *db = inst->dbs; db; db = db->next, k++) {
            SIZE_T plen = lstrlenW(db->path) + 1;
            void *path = HeapAlloc(GetProcessHeap(), 0, plen * per);
            if (wide)
                lstrcpyW(path, db->path);
            else
                api_ansi_out(db->path, path, (ULONG)plen);
            info->szDatabaseFileName[k] = path;
            info->szDatabaseDisplayName[k] = nullptr;
            info->szDatabaseSLVFileName[k] = nullptr;
        }
    }
    UNLOCK();
    *count = n;
    *out = infos;
    return JET_errSuccess;
}

JET_ERR WINAPI JetGetInstanceInfoW(ULONG *count, void **out)
{
    return instance_info(count, out, TRUE);
}

JET_ERR WINAPI JetGetInstanceInfoA(ULONG *count, void **out)
{
    return instance_info(count, out, FALSE);
}

JET_ERR WINAPI JetGetInstanceInfo(ULONG *count, void **out)
{
    return instance_info(count, out, FALSE);
}

JET_ERR WINAPI JetGetInstanceMiscInfo(JET_INSTANCE handle, void *out, ULONG cb, ULONG level)
{
    JET_ERR err;
    LOCK();
    backup_instance(handle, &err);
    UNLOCK();
    if (err)
        return err;
    if (!out)
        return JET_errInvalidParameter;
    return JET_errInvalidParameter;
}
