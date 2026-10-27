/*
 * a short-lived cache of name attributes for NtQueryAttributesFile and NtQueryFullAttributesFile. programs probe the
 * same names over and over, and each query cost a host exit plus a host stat. missing names are cached too.
 *
 * an entry is keyed by volume and case-insensitive name, and lasts until:
 *
 *  - the guest changes the file or its directory. FsNotifyReport and FsNotifyRename drop the name, its directory,
 *    whose times move, and for a removed or moved directory everything below it.
 *  - a host program changes it, which the host reports through a watch on the directory, inotify on Linux. names are
 *    kept only while their directory is watched. a directory gets a watch the second time it is asked about lately,
 *    24 at most, least recently used evicted. the host only reports changes it did not make for the guest.
 *  - half a second passes. this covers what the watch cannot see: a host write within 8 ms of a guest write to the
 *    same file, or a file changed through a view, whose time the host sets when it writes the pages out.
 *
 * an answer is kept only if no change counted between its query and its answer.
 */
#include "../ntos.h"

#define ATTR_BUCKETS 1024
#define ATTR_MAX 4096
#define ATTR_LIFE 5000000ll /* 500 ms in 100 ns units. */
#define ATTR_WATCHES 24
#define ATTR_RING 128
#define ATTR_VISITS 32
#define ATTR_VISIT_LIFE (20000000ll)

typedef struct ATTR_ENTRY {
    struct ATTR_ENTRY *Next;
    LIST_ENTRY Lru;          /* most recently used first. */
    ULONG Volume;
    ULONG Hash;
    LONG Watch; /* watch slot of its directory. */
    LONGLONG Born;
    BOOLEAN Missing;
    USHORT Chars;
    MimeFsInfo Info;
    WCHAR Name[1];
} ATTR_ENTRY;

typedef struct ATTR_WATCH {
    BOOLEAN Used;
    ULONG Id;
    ULONG Volume;
    ULONG Hash; /* of the directory name. */
    USHORT Chars;
    PWCHAR Path; /* volume relative, no trailing backslash, the root is one backslash. */
    LONGLONG Used_At;
} ATTR_WATCH;

static FAST_MUTEX g_lock;
static ATTR_ENTRY *g_buckets[ATTR_BUCKETS];
static LIST_ENTRY g_lru;
static ULONG g_entries;
/*
 * recent changes, so an answer that raced a change to its name, or below it for a directory, is not kept. changes to
 * other names do not count.
 */
typedef struct ATTR_CHANGE {
    ULONG Volume, Name, Parent;
    BOOLEAN Tree;
} ATTR_CHANGE;
static ATTR_CHANGE g_ring[ATTR_RING];
static LONG g_seq;           /* change counter, under the lock. */
static volatile LONG g_fills; /* host queries in flight. */
static ATTR_WATCH g_watches[ATTR_WATCHES];
static ULONG g_next_id = MIME_FS_WATCH_ID_ATTRIBUTES;
static BOOLEAN g_disabled; /* the host cannot watch directories. */
static struct {
    ULONG Volume, Hash;
    LONGLONG At;
} g_visits[ATTR_VISITS];
static ULONG g_visit_next;

void FsAttrInitialize(void)
{
    ExInitializeFastMutex(&g_lock);
    InitializeListHead(&g_lru);
}

static inline WCHAR upper(WCHAR c)
{
    return c < 0x80 ? (c >= 'a' && c <= 'z' ? (WCHAR)(c - 32) : c) : RtlUpcaseUnicodeChar(c);
}

static ULONG hash_of(const WCHAR *name, ULONG chars)
{
    ULONG h = 2166136261u;
    for (ULONG i = 0; i < chars; i++)
        h = (h ^ upper(name[i])) * 16777619u;
    return h;
}

static BOOLEAN same_name(const WCHAR *a, const WCHAR *b, ULONG chars)
{
    for (ULONG i = 0; i < chars; i++)
        if (a[i] != b[i] && upper(a[i]) != upper(b[i]))
            return FALSE;
    return TRUE;
}

/* up to the last backslash; a name directly below the root keeps the root. */
static ULONG parent_chars(const WCHAR *name, ULONG chars)
{
    while (chars > 1 && name[chars - 1] == '\\')
        chars--;
    ULONG i = chars;
    while (i > 1 && name[i - 1] != '\\')
        i--;
    return i <= 1 ? 1 : i - 1;
}

static void unlink_entry(ATTR_ENTRY *e)
{
    ATTR_ENTRY **link = &g_buckets[e->Hash % ATTR_BUCKETS];
    while (*link != e)
        link = &(*link)->Next;
    *link = e->Next;
    RemoveEntryList(&e->Lru);
    g_entries--;
    ExFreePool(e);
}

static ATTR_ENTRY *find_entry(ULONG volume, const WCHAR *name, ULONG chars, ULONG hash)
{
    for (ATTR_ENTRY *e = g_buckets[hash % ATTR_BUCKETS]; e; e = e->Next)
        if (e->Hash == hash && e->Volume == volume && e->Chars == chars && same_name(e->Name, name, chars))
            return e;
    return nullptr;
}

/* lock held. */
static void drop_name(ULONG volume, const WCHAR *name, ULONG chars, ULONG hash)
{
    ATTR_ENTRY *e = find_entry(volume, name, chars, hash);
    if (e)
        unlink_entry(e);
}

/* lock held. drops the name and everything below it. */
static void drop_below(ULONG volume, const WCHAR *name, ULONG chars)
{
    for (PLIST_ENTRY l = g_lru.Flink; l != &g_lru;) {
        ATTR_ENTRY *e = CONTAINING_RECORD(l, ATTR_ENTRY, Lru);
        l = l->Flink;
        if (e->Volume == volume && e->Chars >= chars && same_name(e->Name, name, chars) &&
            (e->Chars == chars || e->Name[chars] == '\\' || chars == 1))
            unlink_entry(e);
    }
}

static void drop_watch(LONG slot);

/*
 * drops the name and its directory, whose times and size follow it. a directory that goes or moves also loses the
 * watches on it and below it: they follow the host directory, not the name. the change is logged for queries in flight.
 */
static void changed_locked(ULONG volume, const WCHAR *name, ULONG chars, BOOLEAN tree)
{
    ULONG parent = parent_chars(name, chars);
    ATTR_CHANGE *c = &g_ring[(ULONG)g_seq % ATTR_RING];
    c->Volume = volume;
    c->Tree = tree;
    c->Name = hash_of(name, chars);
    c->Parent = parent == chars ? c->Name : hash_of(name, parent);
    g_seq++;
    if (!g_entries)
        return;
    if (tree) {
        drop_below(volume, name, chars);
        for (LONG i = 0; i < ATTR_WATCHES; i++) {
            ATTR_WATCH *w = &g_watches[i];
            if (w->Used && w->Volume == volume && w->Chars >= chars && same_name(w->Path, name, chars) &&
                (w->Chars == chars || w->Path[chars] == '\\' || chars == 1))
                drop_watch(i);
        }
    } else {
        drop_name(volume, name, chars, c->Name);
    }
    drop_name(volume, name, parent, c->Parent);
}

void FsAttrChanged(ULONG volume, const WCHAR *name, ULONG chars, BOOLEAN tree)
{
    /* nothing cached and no query in flight. */
    if (!__atomic_load_n(&g_entries, __ATOMIC_ACQUIRE) && !__atomic_load_n(&g_fills, __ATOMIC_ACQUIRE))
        return;
    if (!chars)
        return;
    ExAcquireFastMutex(&g_lock);
    changed_locked(volume, name, chars, tree);
    ExReleaseFastMutex(&g_lock);
}

/* lock held. whether a change touched the name since the query began. */
static BOOLEAN crossed(ULONG volume, ULONG hash, LONG token)
{
    LONG n = g_seq - token;
    if (n <= 0)
        return FALSE;
    if (n > ATTR_RING)
        return TRUE;
    for (LONG i = token; i < g_seq; i++) {
        const ATTR_CHANGE *c = &g_ring[(ULONG)i % ATTR_RING];
        if (c->Tree || (c->Volume == volume && (c->Name == hash || c->Parent == hash)))
            return TRUE;
    }
    return FALSE;
}

/* lock held. */
static void drop_watch(LONG slot)
{
    ATTR_WATCH *w = &g_watches[slot];
    for (PLIST_ENTRY l = g_lru.Flink; l != &g_lru;) {
        ATTR_ENTRY *e = CONTAINING_RECORD(l, ATTR_ENTRY, Lru);
        l = l->Flink;
        if (e->Watch == slot)
            unlink_entry(e);
    }
    ExFreePool(w->Path);
    HalHypercallUnlocked(MIME_HC_FS_UNWATCH, w->Id, 0, 0, 0, 0, 0, nullptr);
    memset(w, 0, sizeof(*w));
    /* a query in flight under that directory is not kept. */
    g_ring[(ULONG)g_seq % ATTR_RING].Tree = TRUE;
    g_seq++;
}

/* lock held. */
static LONG watch_of(ULONG volume, const WCHAR *dir, ULONG chars, ULONG hash)
{
    for (LONG i = 0; i < ATTR_WATCHES; i++) {
        ATTR_WATCH *w = &g_watches[i];
        if (w->Used && w->Volume == volume && w->Hash == hash && w->Chars == chars && same_name(w->Path, dir, chars))
            return i;
    }
    return -1;
}

/* whether the directory was asked about lately; marks it if not. */
static BOOLEAN seen_lately(ULONG volume, ULONG hash)
{
    LONGLONG now = (LONGLONG)KeQueryInterruptTime();
    ULONG free_slot = ATTR_VISITS;
    for (ULONG i = 0; i < ATTR_VISITS; i++) {
        if (g_visits[i].At && now - g_visits[i].At > ATTR_VISIT_LIFE)
            g_visits[i].At = 0;
        if (g_visits[i].At && g_visits[i].Volume == volume && g_visits[i].Hash == hash) {
            g_visits[i].At = 0;
            return TRUE;
        }
        if (!g_visits[i].At && free_slot == ATTR_VISITS)
            free_slot = i;
    }
    if (free_slot == ATTR_VISITS)
        free_slot = g_visit_next++ % ATTR_VISITS;
    g_visits[free_slot].Volume = volume;
    g_visits[free_slot].Hash = hash;
    g_visits[free_slot].At = now;
    return FALSE;
}

/* lock held. returns the slot, or -1. */
static LONG make_watch(ULONG volume, const WCHAR *dir, ULONG chars, ULONG hash)
{
    LONG slot = -1, oldest = 0;
    for (LONG i = 0; i < ATTR_WATCHES; i++) {
        if (!g_watches[i].Used) {
            slot = i;
            break;
        }
        if (g_watches[i].Used_At < g_watches[oldest].Used_At)
            oldest = i;
    }
    if (slot < 0) {
        drop_watch(oldest);
        slot = oldest;
    }
    PWCHAR path = ExAllocatePool2(POOL_FLAG_NON_PAGED, (chars + 1) * sizeof(WCHAR), KI_TAG('F', 's', 'A', 'p'));
    MimeFsOpen *req = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MimeFsOpen), KI_TAG('F', 's', 'A', 'o'));
    LONG result = -1;
    if (path && req && chars <= RTL_NUMBER_OF(req->path)) {
        memcpy(path, dir, chars * sizeof(WCHAR));
        path[chars] = 0;
        memset(req, 0, offsetof(MimeFsOpen, sd_data));
        memcpy(req->path, dir, chars * sizeof(WCHAR));
        req->path_chars = chars;
        req->volume = volume;
        req->flags = MIME_FS_OPEN_READ | MIME_FS_OPEN_DIRECTORY;
        req->disposition = MIME_FS_DISP_OPEN;
        ULONG id = ++g_next_id;
        if (HalHypercallUnlocked(MIME_HC_FS_OPEN, HalPhysicalOf(req), 0, 0, 0, 0, 0, nullptr) == MIME_HC_STATUS_OK) {
            ULONG filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES |
                           FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
            ULONG64 status = HalHypercallUnlocked(MIME_HC_FS_WATCH, req->handle_out, id, filter, 0, 0, 0, nullptr);
            HalHypercallUnlocked(MIME_HC_FS_CLOSE, req->handle_out, 0, 0, 0, 0, 0, nullptr);
            if (status == MIME_HC_STATUS_OK) {
                ATTR_WATCH *w = &g_watches[slot];
                w->Used = TRUE;
                w->Id = id;
                w->Volume = volume;
                w->Hash = hash;
                w->Chars = (USHORT)chars;
                w->Path = path;
                w->Used_At = (LONGLONG)KeQueryInterruptTime();
                path = nullptr;
                result = slot;
                FsNotifyHostStart();
            } else if (status == MIME_HC_STATUS_UNSUPPORTED) {
                g_disabled = TRUE;
            }
        }
    }
    ExFreePool(req);
    ExFreePool(path);
    return result;
}

/*
 * an unwatched directory asked about lately gets its watch before the query, so nothing changes unseen between the
 * answer and the watch. on TRUE the caller passes the answer to FsAttrKeep, whatever it is.
 */
BOOLEAN FsAttrBegin(ULONG volume, PCUNICODE_STRING name, PLONG token)
{
    ULONG chars = name->Length / sizeof(WCHAR);
    *token = 0;
    if (g_disabled || (KiOptions & KI_OPT_NO_ATTR_CACHE) || !chars || chars > 400)
        return FALSE;
    ULONG dir = parent_chars(name->Buffer, chars);
    ULONG hash = hash_of(name->Buffer, dir);
    ExAcquireFastMutex(&g_lock);
    LONG slot = watch_of(volume, name->Buffer, dir, hash);
    if (slot < 0 && seen_lately(volume, hash))
        slot = make_watch(volume, name->Buffer, dir, hash);
    BOOLEAN covered = slot >= 0;
    if (covered) {
        g_watches[slot].Used_At = (LONGLONG)KeQueryInterruptTime();
        __atomic_add_fetch(&g_fills, 1, __ATOMIC_SEQ_CST);
        *token = g_seq;
    }
    ExReleaseFastMutex(&g_lock);
    return covered;
}

/* a cached answer: missing, or the attributes. */
BOOLEAN FsAttrLookup(ULONG volume, PCUNICODE_STRING name, PBOOLEAN missing, MimeFsInfo *info)
{
    if (!__atomic_load_n(&g_entries, __ATOMIC_ACQUIRE))
        return FALSE;
    ULONG chars = name->Length / sizeof(WCHAR);
    if (!chars)
        return FALSE;
    ULONG hash = hash_of(name->Buffer, chars);
    BOOLEAN found = FALSE;
    ExAcquireFastMutex(&g_lock);
    ATTR_ENTRY *e = find_entry(volume, name->Buffer, chars, hash);
    if (e) {
        if ((LONGLONG)KeQueryInterruptTime() - e->Born > ATTR_LIFE) {
            unlink_entry(e);
        } else {
            *missing = e->Missing;
            *info = e->Info;
            RemoveEntryList(&e->Lru);
            InsertHeadList(&g_lru, &e->Lru);
            g_watches[e->Watch].Used_At = (LONGLONG)KeQueryInterruptTime();
            found = TRUE;
        }
    }
    ExReleaseFastMutex(&g_lock);
    return found;
}

/* caches the answer if nothing touched the name since FsAttrBegin and the directory is still watched. */
void FsAttrKeep(ULONG volume, PCUNICODE_STRING name, ULONG kind, const MimeFsInfo *info, LONG token)
{
    ULONG chars = name->Length / sizeof(WCHAR);
    ULONG hash = hash_of(name->Buffer, chars);
    ATTR_ENTRY *fresh = nullptr;
    /* a file with other names is not kept: a change made through one of them would not reach the entries of the rest. */
    if (kind == ATTR_KEEP_FILE && info && info->links > 1)
        kind = ATTR_KEEP_NOTHING;
    if (kind != ATTR_KEEP_NOTHING)
        fresh = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(ATTR_ENTRY) + chars * sizeof(WCHAR), KI_TAG('F', 's', 'A', 'e'));
    ExAcquireFastMutex(&g_lock);
    __atomic_sub_fetch(&g_fills, 1, __ATOMIC_SEQ_CST);
    LONG slot = fresh ? watch_of(volume, name->Buffer, parent_chars(name->Buffer, chars),
                                 hash_of(name->Buffer, parent_chars(name->Buffer, chars)))
                      : -1;
    if (slot < 0 || crossed(volume, hash, token)) {
        ExReleaseFastMutex(&g_lock);
        ExFreePool(fresh);
        return;
    }
    ATTR_ENTRY *old = find_entry(volume, name->Buffer, chars, hash);
    if (old)
        unlink_entry(old);
    while (g_entries >= ATTR_MAX)
        unlink_entry(CONTAINING_RECORD(g_lru.Blink, ATTR_ENTRY, Lru));
    fresh->Volume = volume;
    fresh->Hash = hash;
    fresh->Watch = slot;
    fresh->Born = (LONGLONG)KeQueryInterruptTime();
    fresh->Missing = kind == ATTR_KEEP_MISSING;
    fresh->Chars = (USHORT)chars;
    if (info)
        fresh->Info = *info;
    memcpy(fresh->Name, name->Buffer, chars * sizeof(WCHAR));
    fresh->Next = g_buckets[hash % ATTR_BUCKETS];
    g_buckets[hash % ATTR_BUCKETS] = fresh;
    InsertHeadList(&g_lru, &fresh->Lru);
    g_entries++;
    ExReleaseFastMutex(&g_lock);
}

/* a host program changed a name in a watched directory, or the host lost events. */
void FsAttrHostChange(ULONG id, ULONG action, const WCHAR *name, ULONG chars, BOOLEAN overflow)
{
    ExAcquireFastMutex(&g_lock);
    for (LONG i = 0; i < ATTR_WATCHES; i++) {
        ATTR_WATCH *w = &g_watches[i];
        if (!w->Used || w->Id != id)
            continue;
        if (overflow || !chars) {
            /* the directory is gone or its events were lost: drop it all, it gets a new watch on demand. */
            drop_watch(i);
            break;
        }
        ULONG total = w->Chars + (w->Chars > 1 ? 1 : 0) + chars;
        PWCHAR full = ExAllocatePool2(POOL_FLAG_NON_PAGED, (total + 1) * sizeof(WCHAR), KI_TAG('F', 's', 'A', 'f'));
        if (!full) {
            drop_watch(i);
            break;
        }
        memcpy(full, w->Path, w->Chars * sizeof(WCHAR));
        ULONG at = w->Chars;
        if (w->Chars > 1)
            full[at++] = '\\';
        memcpy(full + at, name, chars * sizeof(WCHAR));
        /* the host does not say whether a removed or moved name was a directory, so drop what was below it too. */
        BOOLEAN below = action == MIME_FS_CHANGE_REMOVED || action == MIME_FS_CHANGE_RENAMED_OLD ||
                        action == MIME_FS_CHANGE_RENAMED_NEW;
        changed_locked(w->Volume, full, total, below);
        ExFreePool(full);
        break;
    }
    ExReleaseFastMutex(&g_lock);
}

BOOLEAN FsAttrIsHostWatch(ULONG id)
{
    return id > MIME_FS_WATCH_ID_ATTRIBUTES;
}
