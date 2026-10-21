/*
 * directory change notifications of the host backed volumes: a request on the handle of a directory waits for a file
 * of it, or of the tree below it, to be made, removed, renamed or changed, and is answered with the names and what
 * happened to them. the changes the guest makes through mimefs are reported as they happen. what the host's own programs
 * do to the files is told by the host, with MIME_HC_FS_WATCH and on Linux through inotify: a directory is watched there while a
 * handle of it has made a request, and the changes come as records the host has queued, fetched when it raises
 * MIME_IRQ_FS by a thread of the kernel, and added to the watch like the guest's own.
 *
 * a handle keeps what happened between its requests, up to the size of the buffer of the last one, as windows does, and
 * answers the next request with it; past that it only knows something happened and the answer is STATUS_NOTIFY_ENUM_DIR
 * with nothing in the buffer, as for a request with no buffer at all. a request is answered as soon as there is
 * something to say, with as many whole records as fit.
 */
#include "../ntos.h"

#define STATUS_NOTIFY_CLEANUP ((NTSTATUS)0x0000010BL)
#define STATUS_NOTIFY_ENUM_DIR ((NTSTATUS)0x0000010CL)

#define SAVED_LIMIT 0x10000


typedef struct _FILE_NOTIFY_INFORMATION {
    ULONG NextEntryOffset;
    ULONG Action;
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_NOTIFY_INFORMATION, *PFILE_NOTIFY_INFORMATION;

/* one record as a request's buffer holds it: FILE_NOTIFY_INFORMATION with the name behind it. */
#define RECORD_HEADER 12

typedef struct FS_WATCH {
    LIST_ENTRY Links;
    PFILE_OBJECT File;
    ULONG Volume;
    PWCHAR Path; /* of the directory below its volume, without a closing backslash */
    ULONG PathChars;
    ULONG Filter;
    BOOLEAN Tree;
    BOOLEAN Overflow;
    LIST_ENTRY Pending; /* IO_USER_REQUEST, oldest first */
    PUCHAR Saved;       /* whole records, the last one's NextEntryOffset 0 */
    ULONG SavedLength, SavedCapacity;
    ULONG Limit; /* the length of the last request's buffer */
    ULONG HostId;     /* the host's name for the watch, 0 when the host does not watch it */
    ULONG HostFilter; /* what the host watches it for */
    BOOLEAN HostTree;
} FS_WATCH;

static KMUTEX g_lock;
static LIST_ENTRY g_watches;
static KEVENT g_host_event;
static BOOLEAN g_host_thread, g_host_unsupported;
static ULONG g_host_ids;

void FsNotifyInitialize(void)
{
    KeInitializeMutex(&g_lock, 0);
    KeInitializeEvent(&g_host_event, SynchronizationEvent, FALSE);
    InitializeListHead(&g_watches);
}

static void lock(void)
{
    KeWaitForSingleObject(&g_lock, Executive, KernelMode, FALSE, nullptr);
}

static void unlock(void)
{
    KeReleaseMutex(&g_lock, FALSE);
}

static WCHAR fold(WCHAR c)
{
    return c >= 'a' && c <= 'z' ? (WCHAR)(c - 32) : c;
}

static FS_WATCH *find(PFILE_OBJECT file)
{
    for (PLIST_ENTRY e = g_watches.Flink; e != &g_watches; e = e->Flink) {
        FS_WATCH *w = CONTAINING_RECORD(e, FS_WATCH, Links);
        if (w->File == file)
            return w;
    }
    return nullptr;
}

static void free_watch(FS_WATCH *w)
{
    if (w->HostId)
        HalHypercallUnlocked(MIME_HC_FS_UNWATCH, w->HostId, 0, 0, 0, 0, 0, nullptr);
    RemoveEntryList(&w->Links);
    ExFreePool(w->Path);
    ExFreePool(w->Saved);
    ExFreePool(w);
}

/*
 * the name of a changed path as the watch sees it, below its directory: the direct children of the directory, and
 * for a watch of a tree everything under it. NULL when the path is outside.
 */
static const WCHAR *relative(const FS_WATCH *w, const WCHAR *path, ULONG chars, ULONG *out_chars)
{
    ULONG p = w->PathChars;
    if (chars <= p || path[p] != '\\')
        return nullptr;
    for (ULONG i = 0; i < p; i++)
        if (fold(path[i]) != fold(w->Path[i]))
            return nullptr;
    const WCHAR *name = path + p + 1;
    ULONG n = chars - p - 1;
    if (!n)
        return nullptr;
    if (!w->Tree)
        for (ULONG i = 0; i < n; i++)
            if (name[i] == '\\')
                return nullptr;
    *out_chars = n;
    return name;
}

/* what a request's buffer takes of the saved records: as many whole ones as fit, which are removed from the saved ones. */
static ULONG take(FS_WATCH *w, PUCHAR out, ULONG capacity)
{
    ULONG used = 0, last = (ULONG)-1;
    while (w->SavedLength) {
        const FILE_NOTIFY_INFORMATION *r = (const FILE_NOTIFY_INFORMATION *)w->Saved;
        ULONG size = (ULONG)((RECORD_HEADER + r->FileNameLength + 3) & ~3u);
        if (used + size > capacity)
            break;
        memcpy(out + used, w->Saved, size);
        if (last != (ULONG)-1)
            ((PFILE_NOTIFY_INFORMATION)(out + last))->NextEntryOffset = used - last;
        last = used;
        ((PFILE_NOTIFY_INFORMATION)(out + used))->NextEntryOffset = 0;
        used += size;
        memmove(w->Saved, w->Saved + size, w->SavedLength - size);
        w->SavedLength -= size;
    }
    return used;
}

/*
 * what a request is told now: the saved records that fit its buffer, or that something happened that it cannot be told of,
 * because more was lost than the handle keeps or the request has no buffer. nothing saved is no answer yet.
 */
static BOOLEAN deliver(FS_WATCH *w, IO_USER_REQUEST *r, NTSTATUS *status, ULONG_PTR *information)
{
    *information = 0;
    if (!w->SavedLength && !w->Overflow)
        return FALSE;
    if (w->Overflow || !r->Length) {
        *status = STATUS_NOTIFY_ENUM_DIR;
        w->Overflow = FALSE;
        w->SavedLength = 0;
        return TRUE;
    }
    PUCHAR out = ExAllocatePool2(POOL_FLAG_NON_PAGED, r->Length, KI_TAG('F', 's', 'N', 'o'));
    if (!out) {
        *status = STATUS_INSUFFICIENT_RESOURCES;
        return TRUE;
    }
    ULONG got = take(w, out, r->Length);
    if (!got) {
        *status = STATUS_NOTIFY_ENUM_DIR;
        w->SavedLength = 0;
    } else {
        *status = IopCopyToRequester(r, r->Buffer, out, got);
        *information = NT_SUCCESS(*status) ? got : 0;
    }
    ExFreePool(out);
    return TRUE;
}

/* answers the oldest requests while there is something to tell them. */
static void flush(FS_WATCH *w)
{
    while (!IsListEmpty(&w->Pending)) {
        IO_USER_REQUEST *r = CONTAINING_RECORD(w->Pending.Flink, IO_USER_REQUEST, Links);
        NTSTATUS status;
        ULONG_PTR information;
        if (!deliver(w, r, &status, &information))
            return;
        RemoveEntryList(&r->Links);
        IopCompleteUserRequest(r, status, information);
    }
}

/* adds a record to the saved ones, unless it repeats the last. */
static void append(FS_WATCH *w, ULONG action, const WCHAR *name, ULONG chars)
{
    ULONG name_bytes = chars * sizeof(WCHAR);
    ULONG size = (ULONG)((RECORD_HEADER + name_bytes + 3) & ~3u);
    if (w->Overflow)
        return;
    /* a request with no buffer only wants to know that something happened. */
    if (w->Limit == 0) {
        w->Overflow = TRUE;
        return;
    }
    /* a record that repeats the last one tells nothing new. */
    for (ULONG at = 0; at < w->SavedLength;) {
        const FILE_NOTIFY_INFORMATION *r = (const FILE_NOTIFY_INFORMATION *)(w->Saved + at);
        ULONG rsize = (ULONG)((RECORD_HEADER + r->FileNameLength + 3) & ~3u);
        if (at + rsize == w->SavedLength) {
            if (r->Action == action && r->FileNameLength == name_bytes && !memcmp(r->FileName, name, name_bytes))
                return;
            break;
        }
        at += rsize;
    }
    ULONG need = w->SavedLength + size;
    if (need > w->Limit || need > SAVED_LIMIT) {
        w->Overflow = TRUE;
        return;
    }
    if (need > w->SavedCapacity) {
        ULONG capacity = need < 1024 ? 1024 : need * 2;
        if (capacity > SAVED_LIMIT)
            capacity = SAVED_LIMIT;
        PUCHAR saved = ExAllocatePool2(POOL_FLAG_NON_PAGED, capacity, KI_TAG('F', 's', 'N', 's'));
        if (!saved) {
            w->Overflow = TRUE;
            return;
        }
        if (w->SavedLength)
            memcpy(saved, w->Saved, w->SavedLength);
        ExFreePool(w->Saved);
        w->Saved = saved;
        w->SavedCapacity = capacity;
    }
    PFILE_NOTIFY_INFORMATION r = (PFILE_NOTIFY_INFORMATION)(w->Saved + w->SavedLength);
    memset(r, 0, size);
    r->Action = action;
    r->FileNameLength = name_bytes;
    memcpy(r->FileName, name, name_bytes);
    w->SavedLength += size;
}

/* a change of a path, for the watches of the volume that want it. */
static void report_one(ULONG volume, const WCHAR *path, ULONG chars, ULONG action, ULONG filter)
{
    for (PLIST_ENTRY e = g_watches.Flink; e != &g_watches; e = e->Flink) {
        FS_WATCH *w = CONTAINING_RECORD(e, FS_WATCH, Links);
        ULONG n;
        const WCHAR *name;
        if (w->Volume == volume && (w->Filter & filter) && (name = relative(w, path, chars, &n)))
            append(w, action, name, n);
    }
}

static void flush_all(void)
{
    for (PLIST_ENTRY e = g_watches.Flink; e != &g_watches; e = e->Flink)
        flush(CONTAINING_RECORD(e, FS_WATCH, Links));
}

/* a path of the volume as the file object holds it, a backslash first. */
void FsNotifyReport(ULONG volume, PCUNICODE_STRING path, ULONG action, ULONG filter)
{
    if (path && path->Length)
        FsAttrChanged(volume, path->Buffer, path->Length / sizeof(WCHAR),
                      action == FILE_ACTION_REMOVED && (filter & FILE_NOTIFY_CHANGE_DIR_NAME));
    /* nothing watched, the usual case, costs no lock: a watch made at this very moment misses what it did not see. */
    if (!path || !path->Length || IsListEmpty(&g_watches))
        return;
    lock();
    report_one(volume, path->Buffer, path->Length / sizeof(WCHAR), action, filter);
    flush_all();
    unlock();
}

/*
 * a file moved from one name to another. a watch that sees both is told the old name and the new one; one that sees only
 * the first is told the file is gone, and one that sees only the second, that it came.
 */
void FsNotifyRename(ULONG volume, PCUNICODE_STRING from, PCUNICODE_STRING to, BOOLEAN directory)
{
    if (!from || !to || !from->Length || !to->Length)
        return;
    ULONG filter = directory ? FILE_NOTIFY_CHANGE_DIR_NAME : FILE_NOTIFY_CHANGE_FILE_NAME;
    FsAttrChanged(volume, from->Buffer, from->Length / sizeof(WCHAR), directory);
    FsAttrChanged(volume, to->Buffer, to->Length / sizeof(WCHAR), directory);
    if (IsListEmpty(&g_watches))
        return;
    lock();
    for (PLIST_ENTRY e = g_watches.Flink; e != &g_watches; e = e->Flink) {
        FS_WATCH *w = CONTAINING_RECORD(e, FS_WATCH, Links);
        if (w->Volume != volume || !(w->Filter & filter))
            continue;
        ULONG old_chars, new_chars;
        const WCHAR *old_name = relative(w, from->Buffer, from->Length / sizeof(WCHAR), &old_chars);
        const WCHAR *new_name = relative(w, to->Buffer, to->Length / sizeof(WCHAR), &new_chars);
        if (old_name && new_name) {
            append(w, FILE_ACTION_RENAMED_OLD_NAME, old_name, old_chars);
            append(w, FILE_ACTION_RENAMED_NEW_NAME, new_name, new_chars);
        } else if (old_name) {
            append(w, FILE_ACTION_REMOVED, old_name, old_chars);
        } else if (new_name) {
            append(w, FILE_ACTION_ADDED, new_name, new_chars);
        }
    }
    flush_all();
    unlock();
}

/* the last handle of a directory closed: what waits on it is told so, and what it kept is dropped. */
void FsNotifyCleanup(PFILE_OBJECT file)
{
    lock();
    FS_WATCH *w = find(file);
    if (w) {
        while (!IsListEmpty(&w->Pending)) {
            IO_USER_REQUEST *r = CONTAINING_RECORD(w->Pending.Flink, IO_USER_REQUEST, Links);
            RemoveEntryList(&r->Links);
            IopCompleteUserRequest(r, STATUS_NOTIFY_CLEANUP, 0);
        }
        free_watch(w);
    }
    unlock();
}

/* CancelIo with the thread, CancelIoEx with the status block of one request or neither. */
NTSTATUS FsNotifyCancel(PFILE_OBJECT file, PETHREAD thread, PVOID iosb)
{
    BOOLEAN found = FALSE;
    lock();
    FS_WATCH *w = find(file);
    if (w) {
        PLIST_ENTRY e = w->Pending.Flink;
        while (e != &w->Pending) {
            IO_USER_REQUEST *r = CONTAINING_RECORD(e, IO_USER_REQUEST, Links);
            e = e->Flink;
            if (r->Process != PsCurrentProcess() || (thread && r->Thread != thread) ||
                (iosb && ((ULONG_PTR)r->Iosb & ~(ULONG_PTR)1) != (ULONG_PTR)iosb))
                continue;
            RemoveEntryList(&r->Links);
            IopCompleteUserRequest(r, STATUS_CANCELLED, 0);
            found = TRUE;
        }
    }
    unlock();
    return found ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

/* ------------------------------------------------------------ the host's changes */

#define HOST_BUFFER 0x8000

/* the records the host queued for the watches, told as the guest's own changes are. */
static void host_records(const UCHAR *buffer, ULONG length)
{
    lock();
    for (ULONG at = 0; at + sizeof(MimeFsChange) <= length;) {
        const MimeFsChange *r = (const MimeFsChange *)(buffer + at);
        if (r->size < sizeof(*r) || at + r->size > length || r->name_chars > (r->size - sizeof(*r)) / sizeof(WCHAR))
            break;
        at += r->size;
        if (FsAttrIsHostWatch(r->watch)) {
            FsAttrHostChange(r->watch, r->action, (const WCHAR *)r->name, r->name_chars, r->action == MIME_FS_CHANGE_OVERFLOW);
            continue;
        }
        FS_WATCH *w = nullptr;
        for (PLIST_ENTRY e = g_watches.Flink; e != &g_watches; e = e->Flink) {
            FS_WATCH *candidate = CONTAINING_RECORD(e, FS_WATCH, Links);
            if (candidate->HostId == r->watch) {
                w = candidate;
                break;
            }
        }
        if (!w)
            continue;
        if (r->action == MIME_FS_CHANGE_OVERFLOW)
            w->Overflow = TRUE;
        else if (r->name_chars)
            append(w, r->action, (const WCHAR *)r->name, r->name_chars);
    }
    flush_all();
    unlock();
}

static void NTAPI host_worker(PVOID context)
{
    PUCHAR buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, HOST_BUFFER, KI_TAG('F', 's', 'N', 'h'));
    for (;;) {
        KeWaitForSingleObject(&g_host_event, Executive, KernelMode, FALSE, nullptr);
        for (;buffer;) {
            ULONG64 bytes = 0;
            if (HalHypercallUnlocked(MIME_HC_FS_CHANGES, HalPhysicalOf(buffer), HOST_BUFFER, 0, 0, 0, 0, &bytes) != MIME_HC_STATUS_OK ||
                !bytes)
                break;
            host_records(buffer, (ULONG)bytes);
        }
    }
}

/* at interrupt level of the session: the thread that fetches does the rest. */
void FsNotifyHostInterrupt(void)
{
    KeSetEvent(&g_host_event, IO_NO_INCREMENT, FALSE);
}

void FsNotifyHostStart(void)
{
    if (__atomic_exchange_n(&g_host_thread, TRUE, __ATOMIC_SEQ_CST))
        return;
    if (!NT_SUCCESS(PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, host_worker, nullptr)))
        __atomic_store_n(&g_host_thread, FALSE, __ATOMIC_SEQ_CST);
}

/* has the host watch the directory for this request's filter, with the lock held; a host that cannot leaves the guest's own. */
static void host_watch(FS_WATCH *w, ULONG64 host_handle)
{
    if (g_host_unsupported || !host_handle)
        return;
    if (w->HostId && w->HostFilter == w->Filter && w->HostTree == w->Tree)
        return;
    ULONG id = w->HostId ? w->HostId : ++g_host_ids;
    ULONG64 status = HalHypercallUnlocked(MIME_HC_FS_WATCH, host_handle, id, w->Filter, w->Tree, 0, 0, nullptr);
    if (status == MIME_HC_STATUS_OK) {
        w->HostId = id;
        w->HostFilter = w->Filter;
        w->HostTree = w->Tree;
        FsNotifyHostStart();
    } else if (status == MIME_HC_STATUS_UNSUPPORTED) {
        g_host_unsupported = TRUE;
    }
}

static NTSTATUS notify_change(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context, PIO_STATUS_BLOCK iosb,
                              PVOID buffer, ULONG length, ULONG filter, BOOLEAN tree)
{
    PVOID routine = (PVOID)apc;
    if (KeGetPreviousMode() == UserMode)
        IopTakeIosb32(&routine, &iosb);
    if (!filter || (filter & ~FILE_NOTIFY_VALID_MASK))
        return STATUS_INVALID_PARAMETER;
    PFILE_OBJECT file;
    ACCESS_MASK granted = 0;
    NTSTATUS status = ObReferenceHandle(handle, IoFileObjectTypeObject, KeGetPreviousMode(), (PVOID *)&file, &granted);
    if (!NT_SUCCESS(status))
        return status;
    if (!(granted & (FILE_LIST_DIRECTORY | GENERIC_READ | GENERIC_ALL | MAXIMUM_ALLOWED))) {
        ObDereferenceObject(file);
        return STATUS_ACCESS_DENIED;
    }
    ULONG volume;
    ULONG64 host_handle = 0;
    status = MimeFsDirectoryVolume(file, &volume, &host_handle);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(file);
        return status;
    }
    IO_USER_REQUEST *r = IopNewUserRequest(file, event, routine, apc_context, iosb, buffer, length, &status);
    /* a request with no event or routine starts afresh the file's own event, which waiting on the handle sees. */
    if (r && !event)
        KeClearEvent(&file->Event);
    ObDereferenceObject(file);
    if (!r)
        return status;
    /* the request holds a reference of the file, the watch is the file's while it has a handle. */
    lock();
    FS_WATCH *w = find(file);
    if (!w) {
        w = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*w), KI_TAG('F', 's', 'N', 'w'));
        PWCHAR path = ExAllocatePool2(POOL_FLAG_NON_PAGED, file->FileName.Length + sizeof(WCHAR), KI_TAG('F', 's', 'N', 'p'));
        if (!w || !path) {
            ExFreePool(w);
            ExFreePool(path);
            unlock();
            return IopImmediateUserRequest(r, STATUS_INSUFFICIENT_RESOURCES, 0);
        }
        memset(w, 0, sizeof(*w));
        ULONG chars = file->FileName.Length / sizeof(WCHAR);
        memcpy(path, file->FileName.Buffer, chars * sizeof(WCHAR));
        while (chars && path[chars - 1] == '\\')
            chars--;
        path[chars] = 0;
        w->File = file;
        w->Volume = volume;
        w->Path = path;
        w->PathChars = chars;
        InitializeListHead(&w->Pending);
        InsertTailList(&g_watches, &w->Links);
    }
    w->Filter = filter;
    w->Tree = tree != 0;
    w->Limit = length > SAVED_LIMIT ? SAVED_LIMIT : length;
    host_watch(w, host_handle);
    /* what the handle kept since its last request is the answer, with nothing waiting for it. */
    ULONG_PTR information;
    if (IsListEmpty(&w->Pending) && deliver(w, r, &status, &information)) {
        unlock();
        return IopImmediateUserRequest(r, status, information);
    }
    InsertTailList(&w->Pending, &r->Links);
    BOOLEAN waiter = r->Waiter;
    unlock();
    return waiter ? IopPendUserRequest(r) : STATUS_PENDING;
}

NTSTATUS NTAPI NtNotifyChangeDirectoryFile(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context,
                                           PIO_STATUS_BLOCK iosb, PVOID buffer, ULONG length, ULONG filter,
                                           BOOLEAN tree)
{
    return notify_change(handle, event, apc, apc_context, iosb, buffer, length, filter, tree);
}

NTSTATUS NTAPI NtNotifyChangeDirectoryFileEx(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context,
                                             PIO_STATUS_BLOCK iosb, PVOID buffer, ULONG length, ULONG filter,
                                             BOOLEAN tree, ULONG information_class)
{
    /* the extended records of the other class carry more than a name; only the plain ones are made. */
    if (information_class != 1)
        return STATUS_INVALID_PARAMETER;
    return notify_change(handle, event, apc, apc_context, iosb, buffer, length, filter, tree);
}
