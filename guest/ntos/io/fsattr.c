/*
 * the attributes of names, kept for a short time. A browser asks about the same names again and again, a name it probes to
 * find one that is free, a file it checks before it opens it, and each ask was an exit to the host, 7 microseconds and the
 * host's own stat. A query by name, NtQueryAttributesFile or NtQueryFullAttributesFile, is answered from here when the
 * name was asked about lately, and so is a name that was not there.
 *
 * An entry is the volume, the name below it as the file object holds it, compared without regard to case, and what the
 * host answered: the attributes of the file, or that there was none. It lasts until one of these.
 *
 *  - The guest changes the file or its directory in any way. The notification of changes hears of all of them, writes,
 *    sizes, times, creations, removals and renames, through FsNotifyReport and FsNotifyRename. The entry of the name goes,
 *    so does the entry of its directory, whose times move, and for a directory removed or moved everything below it.
 *  - The host's own programs change it. The host tells that through the watch it keeps on the directory, inotify on Linux,
 *    see host/vmm/fswatch.c. A name is kept only while its directory has such a watch. The watch is made when a directory
 *    is asked about for the second time lately. There are 24 at most, and the one asked about least recently makes room. The
 *    host tells the changes of its programs and not those it made for the guest, which the guest knew first.
 *  - Half a second passes. That is all that is left of the changes the watch cannot tell: a write of the host in the 8
 *    milliseconds after one of the guest's to the same file, or a file the guest changed through a view of it, whose time
 *    the host sets when it writes the pages out.
 *
 * An answer of the host that crossed a change is not kept. Every change counts, and an answer is kept only if no change
 * counted between its question and its answer.
 */
#include "../ntos.h"

#define ATTR_BUCKETS 1024
#define ATTR_MAX 4096
#define ATTR_LIFE 5000000ll /* 500 ms, in 100 nanosecond units */
#define ATTR_WATCHES 24
#define ATTR_RING 128
#define ATTR_VISITS 32
#define ATTR_VISIT_LIFE (20000000ll)

typedef struct ATTR_ENTRY {
    struct ATTR_ENTRY *Next; /* hash chain */
    LIST_ENTRY Lru;          /* most recently used first */
    ULONG Volume;
    ULONG Hash;
    LONG Watch; /* the slot of the watch that covers its directory */
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
    ULONG Hash; /* of the directory's name */
    USHORT Chars;
    PWCHAR Path; /* of the directory below its volume, no closing backslash, the root as one backslash */
    LONGLONG Used_At;
} ATTR_WATCH;

static FAST_MUTEX g_lock;
static ATTR_ENTRY *g_buckets[ATTR_BUCKETS];
static LIST_ENTRY g_lru;
static ULONG g_entries;
/*
 * the changes that came lately, to tell an answer of the host that crossed one that touches it: an answer is kept only if no
 * change to its name, or to a name below it for a directory, counted between its question and its answer. a change of other
 * names, which a browser makes all the time, does not stop it.
 */
typedef struct ATTR_CHANGE {
    ULONG Volume, Name, Parent;
    BOOLEAN Tree;
} ATTR_CHANGE;
static ATTR_CHANGE g_ring[ATTR_RING];
static LONG g_seq;           /* changes counted, with the lock */
static volatile LONG g_fills; /* questions of the host asked and not answered */
static ATTR_WATCH g_watches[ATTR_WATCHES];
static ULONG g_next_id = MIME_FS_WATCH_ID_ATTRIBUTES;
static BOOLEAN g_disabled; /* the host cannot watch a directory */
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

/* the length of the directory part of a name: up to the last backslash, a name directly below the root has the root. */
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

/* with the lock held. */
static void drop_name(ULONG volume, const WCHAR *name, ULONG chars, ULONG hash)
{
    ATTR_ENTRY *e = find_entry(volume, name, chars, hash);
    if (e)
        unlink_entry(e);
}

/* with the lock held: the name, and everything below it when `tree`. */
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
 * the names a change of `name` touches: itself and its directory, whose times and size follow it. A directory that goes or
 * moves takes the watches of it and of the directories below it, which stay with the directory of the host and not with its
 * name, and a directory made later under that name is not what they watch. The change is also written down for the answers
 * that are on their way.
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
    /* nothing kept and no question on its way: nothing to drop and no one to tell. */
    if (!__atomic_load_n(&g_entries, __ATOMIC_ACQUIRE) && !__atomic_load_n(&g_fills, __ATOMIC_ACQUIRE))
        return;
    if (!chars)
        return;
    ExAcquireFastMutex(&g_lock);
    changed_locked(volume, name, chars, tree);
    ExReleaseFastMutex(&g_lock);
}

/* whether a change touched a name since the question, with the lock held. */
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

/* with the lock held. */
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
    /* an answer on its way from that directory is not kept. */
    g_ring[(ULONG)g_seq % ATTR_RING].Tree = TRUE;
    g_seq++;
}

/* the watch of a directory, with the lock held. */
static LONG watch_of(ULONG volume, const WCHAR *dir, ULONG chars, ULONG hash)
{
    for (LONG i = 0; i < ATTR_WATCHES; i++) {
        ATTR_WATCH *w = &g_watches[i];
        if (w->Used && w->Volume == volume && w->Hash == hash && w->Chars == chars && same_name(w->Path, dir, chars))
            return i;
    }
    return -1;
}

/* whether the directory was asked about lately, and if it was not, that it is now. */
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

/* has the host watch a directory, with the lock held. the slot, or -1. */
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
 * the start of a query that may be kept. When a directory that was asked about lately is not watched, the host is asked to
 * watch it before the question, so that nothing can change unseen between the answer and the watch. TRUE means the answer
 * may be kept, and FsAttrKeep is called with it, whatever the answer was.
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

/* what is known of a name, when it is: missing or its attributes. */
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

/*
 * ends a question of the host that FsAttrBegin allowed to keep, and keeps its answer if no change touched the name since and its
 * directory is still watched.
 */
void FsAttrKeep(ULONG volume, PCUNICODE_STRING name, ULONG kind, const MimeFsInfo *info, LONG token)
{
    ULONG chars = name->Length / sizeof(WCHAR);
    ULONG hash = hash_of(name->Buffer, chars);
    ATTR_ENTRY *fresh = nullptr;
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

/* a change the host's programs made to a name of a directory it watches for the entries, or more than it could tell. */
void FsAttrHostChange(ULONG id, ULONG action, const WCHAR *name, ULONG chars, BOOLEAN overflow)
{
    ExAcquireFastMutex(&g_lock);
    for (LONG i = 0; i < ATTR_WATCHES; i++) {
        ATTR_WATCH *w = &g_watches[i];
        if (!w->Used || w->Id != id)
            continue;
        if (overflow || !chars) {
            /* the directory is gone or its events were lost: nothing about it holds, and it is watched again if asked about. */
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
        /* whether a name removed or moved was a directory is not told, so what was below it goes too. */
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
