/*
 * file queues: copies, deletions and renames that a program or an inf collects and then writes together. A handler gets
 * a note before and after each operation and may skip it or stop the whole commit; the default handler goes on with all,
 * and stops where something is missing that it cannot ask for.
 */
#include "setup_private.h"
#include "../lib/cabinet/cabinet.h"

FILE_QUEUE *SetupQueueOf(HSPFILEQ handle)
{
    FILE_QUEUE *q = handle;
    if (!q || handle == INVALID_HANDLE_VALUE || q->Magic != QUEUE_MAGIC) {
        SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    return q;
}

SETUPAPI HSPFILEQ WINAPI SetupOpenFileQueue(void)
{
    FILE_QUEUE *q = SetupAlloc(sizeof(*q));
    if (!q) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE_VALUE;
    }
    q->Magic = QUEUE_MAGIC;
    return q;
}

static void free_item(QUEUE_ITEM *item)
{
    SetupFree(item->SourceDirectory);
    SetupFree(item->SourceName);
    SetupFree(item->TargetDirectory);
    SetupFree(item->TargetName);
    SetupFree(item->Description);
    SetupFree(item->TagFile);
}

SETUPAPI BOOL WINAPI SetupCloseFileQueue(HSPFILEQ handle)
{
    FILE_QUEUE *q = SetupQueueOf(handle);
    if (!q)
        return FALSE;
    q->Magic = 0;
    for (ULONG i = 0; i < q->Count; i++)
        free_item(&q->Items[i]);
    SetupFree(q->Items);
    SetupFree(q);
    return TRUE;
}

BOOL SetupQueueAdd(FILE_QUEUE *queue, QUEUE_KIND kind, PCWSTR source_directory, PCWSTR source_name, PCWSTR target_directory,
                   PCWSTR target_name, PCWSTR description, PCWSTR tag_file, DWORD style)
{
    if (queue->Count == queue->Capacity) {
        ULONG wanted = queue->Capacity ? queue->Capacity * 2 : 32;
        QUEUE_ITEM *bigger = SetupAlloc(wanted * sizeof(QUEUE_ITEM));
        if (!bigger) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        if (queue->Items) {
            memcpy(bigger, queue->Items, queue->Count * sizeof(QUEUE_ITEM));
            SetupFree(queue->Items);
        }
        queue->Items = bigger;
        queue->Capacity = wanted;
    }
    QUEUE_ITEM *item = &queue->Items[queue->Count];
    memset(item, 0, sizeof(*item));
    item->Kind = kind;
    item->SourceDirectory = SetupDuplicate(source_directory);
    item->SourceName = SetupDuplicate(source_name);
    item->TargetDirectory = SetupDuplicate(target_directory);
    item->TargetName = SetupDuplicate(target_name);
    item->Description = SetupDuplicate(description);
    item->TagFile = SetupDuplicate(tag_file);
    item->Style = style;
    queue->Count++;
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupQueueCopyW(HSPFILEQ handle, PCWSTR source_root, PCWSTR source_path, PCWSTR source_file,
                                     PCWSTR description, PCWSTR tag_file, PCWSTR target_dir, PCWSTR target_file, DWORD style)
{
    FILE_QUEUE *q = SetupQueueOf(handle);
    if (!q)
        return FALSE;
    if (!source_root || !source_file || !target_dir) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR directory[MAX_PATH];
    SIZE_T n = wcslen(source_root);
    if (n + (source_path ? wcslen(source_path) : 0) + 2 >= MAX_PATH) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    wcscpy(directory, source_root);
    if (source_path && *source_path) {
        if (n && directory[n - 1] != '\\')
            wcscat(directory, L"\\");
        wcscat(directory, source_path[0] == '\\' ? source_path + 1 : source_path);
    }
    return SetupQueueAdd(q, QUEUE_COPY, directory, source_file, target_dir, target_file ? target_file : source_file,
                         description, tag_file, style);
}

SETUPAPI BOOL WINAPI SetupQueueCopyA(HSPFILEQ queue, PCSTR source_root, PCSTR source_path, PCSTR source_file,
                                     PCSTR description, PCSTR tag_file, PCSTR target_dir, PCSTR target_file, DWORD style)
{
    PWSTR parts[8] = {SetupToWide(source_root), SetupToWide(source_path), SetupToWide(source_file),
                      SetupToWide(description), SetupToWide(tag_file), SetupToWide(target_dir),
                      SetupToWide(target_file), nullptr};
    BOOL ok = SetupQueueCopyW(queue, parts[0], parts[1], parts[2], parts[3], parts[4], parts[5], parts[6], style);
    for (int i = 0; i < 7; i++)
        SetupFree(parts[i]);
    return ok;
}

SETUPAPI BOOL WINAPI SetupQueueDeleteW(HSPFILEQ handle, PCWSTR path, PCWSTR file)
{
    FILE_QUEUE *q = SetupQueueOf(handle);
    if (!q)
        return FALSE;
    if (!path) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return SetupQueueAdd(q, QUEUE_DELETE, nullptr, nullptr, path, file, nullptr, nullptr, 0);
}

SETUPAPI BOOL WINAPI SetupQueueDeleteA(HSPFILEQ queue, PCSTR path, PCSTR file)
{
    PWSTR a = SetupToWide(path), b = SetupToWide(file);
    BOOL ok = SetupQueueDeleteW(queue, a, b);
    SetupFree(a);
    SetupFree(b);
    return ok;
}

SETUPAPI BOOL WINAPI SetupQueueRenameW(HSPFILEQ handle, PCWSTR source_path, PCWSTR source_file, PCWSTR target_path,
                                       PCWSTR target_file)
{
    FILE_QUEUE *q = SetupQueueOf(handle);
    if (!q)
        return FALSE;
    if (!source_path || !source_file || !target_file) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return SetupQueueAdd(q, QUEUE_RENAME, source_path, source_file, target_path ? target_path : source_path, target_file,
                         nullptr, nullptr, 0);
}

SETUPAPI BOOL WINAPI SetupQueueRenameA(HSPFILEQ queue, PCSTR source_path, PCSTR source_file, PCSTR target_path,
                                       PCSTR target_file)
{
    PWSTR parts[4] = {SetupToWide(source_path), SetupToWide(source_file), SetupToWide(target_path), SetupToWide(target_file)};
    BOOL ok = SetupQueueRenameW(queue, parts[0], parts[1], parts[2], parts[3]);
    for (int i = 0; i < 4; i++)
        SetupFree(parts[i]);
    return ok;
}

/* ------------------------------------------------------------------ writing the files */

static void make_path(PWSTR out, SIZE_T chars, PCWSTR directory, PCWSTR name)
{
    SIZE_T n = directory ? wcslen(directory) : 0;
    if (n >= chars)
        n = chars - 1;
    if (directory)
        memcpy(out, directory, n * sizeof(WCHAR));
    if (n && out[n - 1] != '\\' && name && n + 1 < chars)
        out[n++] = '\\';
    out[n] = 0;
    if (name) {
        SIZE_T more = wcslen(name);
        if (n + more >= chars)
            more = chars - n - 1;
        memcpy(out + n, name, more * sizeof(WCHAR));
        out[n + more] = 0;
    }
}

static BOOL file_version(PCWSTR path, ULONGLONG *version)
{
    DWORD handle;
    DWORD size = GetFileVersionInfoSizeW(path, &handle);
    if (!size)
        return FALSE;
    BYTE *data = SetupAlloc(size);
    BOOL ok = FALSE;
    if (data && GetFileVersionInfoW(path, 0, size, data)) {
        VS_FIXEDFILEINFO *info;
        UINT length;
        if (VerQueryValueW(data, L"\\", (LPVOID *)&info, &length) && length >= sizeof(*info)) {
            *version = ((ULONGLONG)info->dwFileVersionMS << 32) | info->dwFileVersionLS;
            ok = TRUE;
        }
    }
    SetupFree(data);
    return ok;
}

static void make_directories(PWSTR path)
{
    for (PWSTR p = path + 3; *p; p++) {
        if (*p == '\\') {
            *p = 0;
            CreateDirectoryW(path, nullptr);
            *p = '\\';
        }
    }
}

/* whether the target is already what the style wants it to be: a copy that is not made is not an error. */
static BOOL leave_target(const QUEUE_ITEM *item, PCWSTR source, PCWSTR target)
{
    BOOL exists = GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES;
    if (!exists)
        return (item->Style & SP_COPY_REPLACEONLY) != 0;
    if (item->Style & SP_COPY_NOOVERWRITE)
        return TRUE;
    if (item->Style & (SP_COPY_FORCE_NEWER | SP_COPY_NOSKIP))
        return FALSE;
    ULONGLONG from, to;
    BOOL have_from = file_version(source, &from), have_to = file_version(target, &to);
    if (item->Style & SP_COPY_NEWER_ONLY) {
        if (have_from && have_to)
            return from <= to;
        WIN32_FILE_ATTRIBUTE_DATA a, b;
        return GetFileAttributesExW(source, GetFileExInfoStandard, &a) && GetFileAttributesExW(target, GetFileExInfoStandard, &b) &&
               CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) <= 0;
    }
    /* by default a target that is newer than the source is kept. */
    return have_from && have_to && to > from;
}

static DWORD copy_one(const QUEUE_ITEM *item, PCWSTR source, PCWSTR target)
{
    make_directories((PWSTR)target);
    SetFileAttributesW(target, FILE_ATTRIBUTE_NORMAL);
    if (CopyFileW(source, target, FALSE)) {
        if (item->Style & SP_COPY_DELETESOURCE)
            DeleteFileW(source);
        return ERROR_SUCCESS;
    }
    DWORD error = GetLastError();
    /* a file that runs can be renamed away, and the copy then takes its name. */
    if ((error == ERROR_SHARING_VIOLATION || error == ERROR_ACCESS_DENIED) && GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES) {
        WCHAR old[MAX_PATH + 16];
        SetupCopyText(old, target, MAX_PATH);
        _snwprintf(old + wcslen(old), 16, L".%08lx.old", (unsigned long)GetTickCount());
        if (MoveFileExW(target, old, 0) && CopyFileW(source, target, FALSE)) {
            if (item->Style & SP_COPY_DELETESOURCE)
                DeleteFileW(source);
            DeleteFileW(old);
            return ERROR_SUCCESS;
        }
        error = GetLastError();
    }
    return error;
}

/*
 * the notes a handler gets carry paths in the kind of strings it was written for: a handler of the ansi kind gets the
 * paths as ansi, and a wide one wide.
 */
typedef struct HANDLER {
    PVOID Routine;
    PVOID Context;
    BOOL Ansi;
} HANDLER;

static UINT call_paths(const HANDLER *h, UINT note, PCWSTR target, PCWSTR source, UINT error, DWORD flags, UINT_PTR param2)
{
    if (!h->Routine)
        return FILEOP_DOIT;
    if (h->Ansi) {
        PSTR t = SetupToAnsi(target), s = SetupToAnsi(source);
        FILEPATHS_A paths = {t, s, error, flags};
        UINT r = ((PSP_FILE_CALLBACK_A)h->Routine)(h->Context, note, (UINT_PTR)&paths, param2);
        SetupFree(t);
        SetupFree(s);
        return r;
    }
    FILEPATHS_W paths = {target, source, error, flags};
    return ((PSP_FILE_CALLBACK_W)h->Routine)(h->Context, note, (UINT_PTR)&paths, param2);
}

static UINT call_plain(const HANDLER *h, UINT note, UINT_PTR param1, UINT_PTR param2)
{
    if (!h->Routine)
        return NO_ERROR;
    return h->Ansi ? ((PSP_FILE_CALLBACK_A)h->Routine)(h->Context, note, param1, param2)
                   : ((PSP_FILE_CALLBACK_W)h->Routine)(h->Context, note, param1, param2);
}

BOOL SetupCommit(FILE_QUEUE *queue, PSP_FILE_CALLBACK_W handler_w, PVOID context, BOOL ansi_handler, PVOID raw)
{
    HANDLER h = {raw, context, ansi_handler};
    if (!raw) {
        h.Routine = nullptr;
    }
    UINT_PTR counts[3] = {0, 0, 0};
    for (ULONG i = 0; i < queue->Count; i++)
        counts[queue->Items[i].Kind == QUEUE_DELETE ? 2 : queue->Items[i].Kind == QUEUE_RENAME ? 1 : 0]++;
    call_plain(&h, SPFILENOTIFY_STARTQUEUE, 0, 0);
    BOOL ok = TRUE;
    DWORD error = ERROR_SUCCESS;
    static const int order[3] = {QUEUE_DELETE, QUEUE_RENAME, QUEUE_COPY};
    for (int pass = 0; pass < 3 && ok; pass++) {
        UINT_PTR count = pass == 0 ? counts[2] : pass == 1 ? counts[1] : counts[0];
        if (!count)
            continue;
        UINT kind = pass == 0 ? FILEOP_DELETE : pass == 1 ? FILEOP_RENAME : FILEOP_COPY;
        call_plain(&h, SPFILENOTIFY_STARTSUBQUEUE, kind, count);
        for (ULONG i = 0; i < queue->Count && ok; i++) {
            QUEUE_ITEM *item = &queue->Items[i];
            if ((int)item->Kind != order[pass])
                continue;
            WCHAR source[MAX_PATH], target[MAX_PATH];
            make_path(target, MAX_PATH, item->TargetDirectory, item->TargetName);
            make_path(source, MAX_PATH, item->SourceDirectory, item->SourceName);
            if (item->Kind == QUEUE_DELETE) {
                UINT answer = call_paths(&h, SPFILENOTIFY_STARTDELETE, target, nullptr, 0, 0, 0);
                if (answer == FILEOP_ABORT) {
                    ok = FALSE;
                    error = ERROR_CANCELLED;
                } else if (answer == FILEOP_DOIT) {
                    DWORD result = ERROR_SUCCESS;
                    if (GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES) {
                        SetFileAttributesW(target, FILE_ATTRIBUTE_NORMAL);
                        if (!DeleteFileW(target))
                            result = GetLastError();
                    }
                    if (result)
                        call_paths(&h, SPFILENOTIFY_DELETEERROR, target, nullptr, result, 0, 0);
                    call_paths(&h, SPFILENOTIFY_ENDDELETE, target, nullptr, result, 0, 0);
                }
            } else if (item->Kind == QUEUE_RENAME) {
                UINT answer = call_paths(&h, SPFILENOTIFY_STARTRENAME, target, source, 0, 0, 0);
                if (answer == FILEOP_ABORT) {
                    ok = FALSE;
                    error = ERROR_CANCELLED;
                } else if (answer == FILEOP_DOIT) {
                    DWORD result = ERROR_SUCCESS;
                    if (GetFileAttributesW(source) != INVALID_FILE_ATTRIBUTES && !MoveFileExW(source, target, MOVEFILE_REPLACE_EXISTING))
                        result = GetLastError();
                    if (result)
                        call_paths(&h, SPFILENOTIFY_RENAMEERROR, target, source, result, 0, 0);
                    call_paths(&h, SPFILENOTIFY_ENDRENAME, target, source, result, 0, 0);
                }
            } else {
                UINT answer = call_paths(&h, SPFILENOTIFY_STARTCOPY, target, source, 0, 0, 0);
                if (answer == FILEOP_ABORT) {
                    ok = FALSE;
                    error = ERROR_CANCELLED;
                } else if (answer == FILEOP_DOIT) {
                    DWORD result = ERROR_SUCCESS;
                    if (GetFileAttributesW(source) == INVALID_FILE_ATTRIBUTES) {
                        result = GetLastError();
                        if (result != ERROR_FILE_NOT_FOUND && result != ERROR_PATH_NOT_FOUND)
                            result = ERROR_FILE_NOT_FOUND;
                    } else if (!leave_target(item, source, target)) {
                        result = copy_one(item, source, target);
                    }
                    if (result) {
                        /* a note of the failure, which a handler may answer by skipping or by stopping. */
                        WCHAR path[MAX_PATH];
                        wcscpy(path, source);
                        UINT answer2 = call_paths(&h, SPFILENOTIFY_COPYERROR, target, source, result, 0, (UINT_PTR)path);
                        if (answer2 != FILEOP_SKIP) {
                            ok = FALSE;
                            error = result;
                        }
                    }
                    call_paths(&h, SPFILENOTIFY_ENDCOPY, target, source, result, 0, 0);
                }
            }
        }
        call_plain(&h, SPFILENOTIFY_ENDSUBQUEUE, kind, 0);
    }
    call_plain(&h, SPFILENOTIFY_ENDQUEUE, ok, 0);
    if (!ok)
        SetLastError(error);
    return ok;
}

SETUPAPI BOOL WINAPI SetupCommitFileQueueW(HWND owner, HSPFILEQ handle, PSP_FILE_CALLBACK_W handler, PVOID context)
{
    FILE_QUEUE *q = SetupQueueOf(handle);
    if (!q)
        return FALSE;
    if (!handler) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return SetupCommit(q, handler, context, FALSE, handler);
}

SETUPAPI BOOL WINAPI SetupCommitFileQueueA(HWND owner, HSPFILEQ handle, PSP_FILE_CALLBACK_A handler, PVOID context)
{
    FILE_QUEUE *q = SetupQueueOf(handle);
    if (!q)
        return FALSE;
    if (!handler) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return SetupCommit(q, nullptr, context, TRUE, handler);
}

/* ------------------------------------------------------------------ the default handler */

typedef struct DEFAULT_CONTEXT {
    HWND Owner;
    HWND Progress;
    UINT Message;
} DEFAULT_CONTEXT;

SETUPAPI PVOID WINAPI SetupInitDefaultQueueCallback(HWND owner)
{
    return SetupInitDefaultQueueCallbackEx(owner, nullptr, 0, 0, nullptr);
}

SETUPAPI PVOID WINAPI SetupInitDefaultQueueCallbackEx(HWND owner, HWND progress, UINT message, DWORD reserved1, PVOID reserved2)
{
    DEFAULT_CONTEXT *c = SetupAlloc(sizeof(*c));
    if (c) {
        c->Owner = owner;
        c->Progress = progress;
        c->Message = message;
    }
    return c;
}

SETUPAPI VOID WINAPI SetupTermDefaultQueueCallback(PVOID context)
{
    SetupFree(context);
}

/* what the default handler answers: every operation is done, and what needs a person is refused. */
static UINT default_answer(UINT notification)
{
    switch (notification) {
    case SPFILENOTIFY_STARTDELETE:
    case SPFILENOTIFY_STARTRENAME:
    case SPFILENOTIFY_STARTCOPY:
        return FILEOP_DOIT;
    case SPFILENOTIFY_NEEDMEDIA:
        return FILEOP_SKIP;
    case SPFILENOTIFY_DELETEERROR:
    case SPFILENOTIFY_RENAMEERROR:
    case SPFILENOTIFY_COPYERROR:
        return FILEOP_ABORT;
    default:
        return NO_ERROR;
    }
}

SETUPAPI UINT WINAPI SetupDefaultQueueCallbackW(PVOID context, UINT notification, UINT_PTR param1, UINT_PTR param2)
{
    return default_answer(notification);
}

SETUPAPI UINT WINAPI SetupDefaultQueueCallbackA(PVOID context, UINT notification, UINT_PTR param1, UINT_PTR param2)
{
    return default_answer(notification);
}

/* ------------------------------------------------------------------ cabinets */

typedef struct EXTRACT {
    PVOID Handler;
    PVOID Context;
    BOOL Ansi;
    PCWSTR Cabinet;
    DWORD Error;
    BOOL Stopped;
    HANDLE File;
    WCHAR Target[MAX_PATH];
} EXTRACT;

static UINT call_handler(const EXTRACT *e, UINT note, UINT_PTR a, UINT_PTR b)
{
    return e->Ansi ? ((PSP_FILE_CALLBACK_A)e->Handler)(e->Context, note, a, b)
                   : ((PSP_FILE_CALLBACK_W)e->Handler)(e->Context, note, a, b);
}

typedef struct FILE_CONTEXT {
    HANDLE File;
    WCHAR Target[MAX_PATH];
    WORD Date, Time, Attributes;
} FILE_CONTEXT;

static void *cab_begin(void *state, const MIME_CAB_FILE *file)
{
    EXTRACT *e = state;
    if (e->Stopped)
        return nullptr;
    WCHAR name[MAX_PATH];
    UINT page = (file->Attributes & 0x80) ? CP_UTF8 : CP_ACP;
    MultiByteToWideChar(page, 0, file->Name, -1, name, MAX_PATH);
    UINT answer;
    FILE_CONTEXT *context = SetupAlloc(sizeof(*context));
    if (!context)
        return nullptr;
    if (e->Ansi) {
        FILE_IN_CABINET_INFO_A info;
        memset(&info, 0, sizeof(info));
        PSTR ansi = SetupToAnsi(name);
        info.NameInCabinet = ansi;
        info.FileSize = file->Size;
        info.DosDate = file->Date;
        info.DosTime = file->Time;
        info.DosAttribs = file->Attributes;
        PSTR cab = SetupToAnsi(e->Cabinet);
        answer = call_handler(e, SPFILENOTIFY_FILEINCABINET, (UINT_PTR)&info, (UINT_PTR)cab);
        MultiByteToWideChar(CP_ACP, 0, info.FullTargetName, -1, context->Target, MAX_PATH);
        SetupFree(ansi);
        SetupFree(cab);
    } else {
        FILE_IN_CABINET_INFO_W info;
        memset(&info, 0, sizeof(info));
        info.NameInCabinet = name;
        info.FileSize = file->Size;
        info.DosDate = file->Date;
        info.DosTime = file->Time;
        info.DosAttribs = file->Attributes;
        answer = call_handler(e, SPFILENOTIFY_FILEINCABINET, (UINT_PTR)&info, (UINT_PTR)e->Cabinet);
        wcscpy(context->Target, info.FullTargetName);
    }
    if (answer == FILEOP_ABORT) {
        e->Stopped = TRUE;
        e->Error = ERROR_CANCELLED;
        SetupFree(context);
        return nullptr;
    }
    if (answer != FILEOP_DOIT || !context->Target[0]) {
        SetupFree(context);
        return nullptr;
    }
    make_directories(context->Target);
    SetFileAttributesW(context->Target, FILE_ATTRIBUTE_NORMAL);
    context->File = CreateFileW(context->Target, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (context->File == INVALID_HANDLE_VALUE) {
        e->Error = GetLastError();
        e->Stopped = TRUE;
        SetupFree(context);
        return nullptr;
    }
    context->Date = file->Date;
    context->Time = file->Time;
    context->Attributes = file->Attributes;
    return context;
}

static int cab_write(void *state, void *file_context, const uint8_t *data, size_t length)
{
    EXTRACT *e = state;
    FILE_CONTEXT *c = file_context;
    DWORD done;
    if (!WriteFile(c->File, data, (DWORD)length, &done, nullptr) || done != length) {
        e->Error = GetLastError();
        e->Stopped = TRUE;
        return 1;
    }
    return 0;
}

static int cab_end(void *state, void *file_context, const MIME_CAB_FILE *file)
{
    EXTRACT *e = state;
    FILE_CONTEXT *c = file_context;
    FILETIME local, utc;
    if (DosDateTimeToFileTime(c->Date, c->Time, &local) && LocalFileTimeToFileTime(&local, &utc))
        SetFileTime(c->File, nullptr, nullptr, &utc);
    CloseHandle(c->File);
    DWORD attributes = c->Attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE);
    if (attributes)
        SetFileAttributesW(c->Target, attributes);
    UINT answer;
    if (e->Ansi) {
        PSTR target = SetupToAnsi(c->Target), source = SetupToAnsi(e->Cabinet);
        FILEPATHS_A paths = {target, source, 0, 0};
        answer = call_handler(e, SPFILENOTIFY_FILEEXTRACTED, (UINT_PTR)&paths, 0);
        SetupFree(target);
        SetupFree(source);
    } else {
        FILEPATHS_W paths = {c->Target, e->Cabinet, 0, 0};
        answer = call_handler(e, SPFILENOTIFY_FILEEXTRACTED, (UINT_PTR)&paths, 0);
    }
    SetupFree(c);
    if (answer != NO_ERROR) {
        e->Error = answer;
        e->Stopped = TRUE;
        return 1;
    }
    return 0;
}

static BOOL iterate_cabinet(PCWSTR cabinet, PVOID handler, PVOID context, BOOL ansi)
{
    if (!cabinet || !handler) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HANDLE file = CreateFileW(cabinet, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    LARGE_INTEGER size;
    BYTE *data = nullptr;
    DWORD done = 0;
    if (!GetFileSizeEx(file, &size) || size.QuadPart > 0x7FFFFFFF || !(data = SetupAlloc((SIZE_T)size.QuadPart + 1)) ||
        !ReadFile(file, data, (DWORD)size.QuadPart, &done, nullptr) || done != size.QuadPart) {
        DWORD error = GetLastError();
        CloseHandle(file);
        SetupFree(data);
        SetLastError(error ? error : ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    CloseHandle(file);
    MIME_CAB *cab;
    int status = MimeCabOpen(&cab, data, (size_t)size.QuadPart, SetupAlloc, SetupFree);
    if (status != MIME_CAB_OK) {
        SetupFree(data);
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    EXTRACT e = {handler, context, ansi, cabinet, 0, FALSE, nullptr, L""};
    /* the note of the cabinet: where it is and what set it belongs to. */
    WCHAR path[MAX_PATH];
    wcscpy(path, cabinet);
    PWSTR slash = wcsrchr(path, '\\');
    PCWSTR name = slash ? slash + 1 : path;
    WCHAR directory[MAX_PATH];
    wcscpy(directory, path);
    if (slash)
        directory[slash - path + 1] = 0;
    else
        directory[0] = 0;
    uint16_t id, index, flags;
    MimeCabSet(cab, &id, &index, &flags);
    UINT answer;
    if (ansi) {
        PSTR a = SetupToAnsi(directory), b = SetupToAnsi(name);
        CABINET_INFO_A info = {a, b, "", id, index};
        answer = call_handler(&e, SPFILENOTIFY_CABINETINFO, (UINT_PTR)&info, 0);
        SetupFree(a);
        SetupFree(b);
    } else {
        CABINET_INFO_W info = {directory, name, L"", id, index};
        answer = call_handler(&e, SPFILENOTIFY_CABINETINFO, (UINT_PTR)&info, 0);
    }
    BOOL ok = answer == NO_ERROR;
    if (!ok)
        e.Error = answer;
    MIME_CAB_FILE_SINK sink = {cab_begin, cab_write, cab_end};
    if (ok) {
        status = MimeCabExtract(cab, &sink, &e);
        if (e.Stopped)
            ok = FALSE;
        else if (status == MIME_CAB_UNSUPPORTED) {
            ok = FALSE;
            e.Error = ERROR_NOT_SUPPORTED;
        } else if (status != MIME_CAB_OK) {
            ok = FALSE;
            e.Error = ERROR_INVALID_DATA;
        }
    }
    MimeCabClose(cab);
    SetupFree(data);
    if (!ok)
        SetLastError(e.Error ? e.Error : ERROR_CANCELLED);
    return ok;
}

SETUPAPI BOOL WINAPI SetupIterateCabinetW(PCWSTR cabinet, DWORD reserved, PSP_FILE_CALLBACK_W handler, PVOID context)
{
    return iterate_cabinet(cabinet, handler, context, FALSE);
}

SETUPAPI BOOL WINAPI SetupIterateCabinetA(PCSTR cabinet, DWORD reserved, PSP_FILE_CALLBACK_A handler, PVOID context)
{
    PWSTR wide = SetupToWide(cabinet);
    BOOL ok = wide ? iterate_cabinet(wide, handler, context, TRUE) : FALSE;
    SetupFree(wide);
    return ok;
}
