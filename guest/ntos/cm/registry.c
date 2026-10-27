/*
 * the registry: a tree of key nodes in memory, loaded lazily from hives,
 * exposed as Key objects below \REGISTRY, and the Nt*Key system services.
 * links such as CurrentControlSet are keys with a SymbolicLinkValue,
 * followed while parsing a path.
 */
#include "cm.h"

#define MAX_REPARSE 16
#define MAX_PATH_CHARS 2048

FAST_MUTEX CmpLock;
CM_NODE *CmpRoot;
POBJECT_TYPE CmKeyType;

typedef struct CM_PARSE_CONTEXT {
    BOOLEAN Create;
    BOOLEAN Kernel; /* boot time creation: children of volatile keys become volatile */
    ULONG Options;
    ULONG Disposition;
    BOOLEAN Replay;
    KPROCESSOR_MODE Mode; /* of the creating program */
    SD_BLOB *Creator;     /* the descriptor it offered for the new key */
    BOOLEAN Check;        /* key descriptors apply to the program */
    ACCESS_MASK Access;   /* requested, then granted once checked */
    ACCESS_MASK Granted;
    BOOLEAN Checked;
    BOOLEAN ByHandle;        /* the walk starts at a handle's key, opened for HandleAccess */
    ACCESS_MASK HandleAccess;
    UCHAR RegistryView;
    BOOLEAN SelectView;
    BOOLEAN ViewContainer;
} CM_PARSE_CONTEXT;

#define KEY_CREATE_SUB_KEY_ 0x0004
#define KEY_WOW64_64KEY_ 0x0100
#define KEY_WOW64_32KEY_ 0x0200
#define REG_OPTION_BACKUP_RESTORE_ 0x00000004

static WCHAR upcase(WCHAR c)
{
    return (WCHAR)towupper(c);
}

static LONG compare_names(const WCHAR *a, ULONG alen, const WCHAR *b, ULONG blen)
{
    for (ULONG i = 0; i < alen && i < blen; i++) {
        WCHAR x = upcase(a[i]), y = upcase(b[i]);
        if (x != y)
            return x < y ? -1 : 1;
    }
    return alen == blen ? 0 : alen < blen ? -1 : 1;
}

static LARGE_INTEGER now(void)
{
    LARGE_INTEGER t;
    KeQuerySystemTime(&t);
    return t;
}

static CM_NODE *new_node(CM_NODE *parent, const WCHAR *name, ULONG chars)
{
    CM_NODE *n = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(CM_NODE) + chars * sizeof(WCHAR), KI_TAG('C', 'm', 'N', 'd'));
    if (!n)
        return nullptr;
    n->Parent = parent;
    n->NameLength = (USHORT)chars;
    memcpy(n->Name, name, chars * sizeof(WCHAR));
    return n;
}

static void link_child(CM_NODE *parent, CM_NODE *child)
{
    CM_NODE **link = &parent->Child;
    while (*link && compare_names((*link)->Name, (*link)->NameLength, child->Name, child->NameLength) < 0)
        link = &(*link)->Next;
    child->Next = *link;
    *link = child;
}

typedef struct LOAD_CONTEXT {
    CM_NODE *Parent;
    CM_NODE **Tail;
    CM_HIVE_KEY Key;
} LOAD_CONTEXT;

static NTSTATUS load_child(PVOID context, ULONG cell)
{
    LOAD_CONTEXT *c = context;
    NTSTATUS status = CmpReadKey(c->Parent->Hive, cell, &c->Key);
    if (!NT_SUCCESS(status))
        return status;
    CM_NODE *n = new_node(c->Parent, c->Key.Name, c->Key.NameLength);
    if (!n)
        return STATUS_INSUFFICIENT_RESOURCES;
    n->Hive = c->Parent->Hive;
    n->Cell = cell;
    n->LastWrite = c->Key.LastWrite;
    if (c->Key.Flags & CM_HIVE_KEY_LINK)
        n->Flags |= CM_NODE_LINK;
    /* the hive keeps subkeys sorted; keys created earlier are merged in order. */
    while (*c->Tail && compare_names((*c->Tail)->Name, (*c->Tail)->NameLength, n->Name, n->NameLength) < 0)
        c->Tail = &(*c->Tail)->Next;
    if (*c->Tail && compare_names((*c->Tail)->Name, (*c->Tail)->NameLength, n->Name, n->NameLength) == 0) {
        ExFreePool(n);
        c->Tail = &(*c->Tail)->Next;
        return STATUS_SUCCESS;
    }
    n->Next = *c->Tail;
    *c->Tail = n;
    c->Tail = &n->Next;
    return STATUS_SUCCESS;
}

NTSTATUS CmpLoadChildren(CM_NODE *node)
{
    if (node->Flags & CM_NODE_CHILDREN_LOADED)
        return STATUS_SUCCESS;
    if (!node->Hive) {
        node->Flags |= CM_NODE_CHILDREN_LOADED;
        return STATUS_SUCCESS;
    }
    LOAD_CONTEXT *c = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*c), KI_TAG('C', 'm', 'L', 'c'));
    if (!c)
        return STATUS_INSUFFICIENT_RESOURCES;
    c->Parent = node;
    c->Tail = &node->Child;
    NTSTATUS status = CmpReadKey(node->Hive, node->Cell, &c->Key);
    if (NT_SUCCESS(status)) {
        CM_HIVE_KEY key = c->Key;
        status = CmpForEachSubKey(node->Hive, &key, load_child, c);
    }
    ExFreePool(c);
    if (!NT_SUCCESS(status))
        KiLogPrintf("registry: cannot read the subkeys of %.*ls: %08x", node->NameLength, node->Name, status);
    else
        node->Flags |= CM_NODE_CHILDREN_LOADED;
    return status;
}

static CM_VALUE *new_value(const WCHAR *name, ULONG chars)
{
    CM_VALUE *v = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(CM_VALUE) + chars * sizeof(WCHAR), KI_TAG('C', 'm', 'V', 'l'));
    if (!v)
        return nullptr;
    v->NameLength = (USHORT)chars;
    memcpy(v->Name, name, chars * sizeof(WCHAR));
    return v;
}

NTSTATUS CmpLoadValues(CM_NODE *node)
{
    if (node->Flags & CM_NODE_VALUES_LOADED)
        return STATUS_SUCCESS;
    if (!node->Hive) {
        node->Flags |= CM_NODE_VALUES_LOADED;
        return STATUS_SUCCESS;
    }
    CM_HIVE_KEY *key = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*key) + 256 * sizeof(WCHAR), KI_TAG('C', 'm', 'L', 'v'));
    if (!key)
        return STATUS_INSUFFICIENT_RESOURCES;
    PWCHAR name = (PWCHAR)(key + 1);
    NTSTATUS status = CmpReadKey(node->Hive, node->Cell, key);
    CM_VALUE **tail = &node->Values;
    while (*tail)
        tail = &(*tail)->Next;
    for (ULONG i = 0; NT_SUCCESS(status) && i < key->ValueCount; i++) {
        USHORT chars;
        ULONG type, length;
        PUCHAR data;
        status = CmpReadValue(node->Hive, key, i, name, &chars, &type, &data, &length);
        if (!NT_SUCCESS(status))
            break;
        if (CmpFindValue(node, name, chars)) {
            ExFreePool(data); /* set since boot, that value wins. */
            continue;
        }
        CM_VALUE *v = new_value(name, chars);
        if (!v) {
            ExFreePool(data);
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }
        v->Type = type;
        v->Data = data;
        v->DataLength = length;
        *tail = v;
        tail = &v->Next;
    }
    ExFreePool(key);
    if (!NT_SUCCESS(status))
        KiLogPrintf("registry: cannot read the values of %.*ls: %08x", node->NameLength, node->Name, status);
    else
        node->Flags |= CM_NODE_VALUES_LOADED;
    return status;
}

CM_NODE *CmpFindChild(CM_NODE *node, const WCHAR *name, ULONG chars)
{
    CmpLoadChildren(node);
    for (CM_NODE *c = node->Child; c; c = c->Next)
        if (compare_names(c->Name, c->NameLength, name, chars) == 0)
            return c;
    return nullptr;
}

static NTSTATUS create_child(CM_NODE *node, const WCHAR *name, ULONG chars, ULONG flags, CM_NODE **out)
{
    NTSTATUS status = CmpLoadChildren(node);
    if (!NT_SUCCESS(status))
        return status;
    CM_NODE *n = new_node(node, name, chars);
    if (!n)
        return STATUS_INSUFFICIENT_RESOURCES;
    n->Flags = flags | CM_NODE_CHILDREN_LOADED | CM_NODE_VALUES_LOADED;
    n->LastWrite = now();
    status = CmpJournalAppend(n, CM_JOURNAL_CREATE_KEY, flags & CM_NODE_LINK ? CM_JOURNAL_LINK : 0,
                              nullptr, 0, nullptr, 0, n->LastWrite);
    if (!NT_SUCCESS(status)) {
        ExFreePool(n);
        return status;
    }
    link_child(node, n);
    node->LastWrite = n->LastWrite;
    CmpNotifyChange(node, REG_NOTIFY_CHANGE_NAME);
    *out = n;
    return STATUS_SUCCESS;
}

CM_NODE *CmpCreateChild(CM_NODE *node, const WCHAR *name, ULONG chars, ULONG flags)
{
    CM_NODE *n = nullptr;
    create_child(node, name, chars, flags, &n);
    return n;
}

CM_NODE *CmpMount(CM_NODE *node, PCWSTR name, CM_HIVE *hive)
{
    CM_NODE *n = CmpCreateChild(node, name, (ULONG)wcslen(name), 0);
    if (!n)
        return nullptr;
    CM_HIVE_KEY key;
    if (NT_SUCCESS(CmpReadKey(hive, hive->RootCell, &key)))
        n->LastWrite = key.LastWrite;
    n->Hive = hive;
    n->Cell = hive->RootCell;
    n->Flags &= ~(CM_NODE_CHILDREN_LOADED | CM_NODE_VALUES_LOADED);
    return n;
}

CM_VALUE *CmpFindValue(CM_NODE *node, const WCHAR *name, ULONG chars)
{
    for (CM_VALUE *v = node->Values; v; v = v->Next)
        if (compare_names(v->Name, v->NameLength, name, chars) == 0)
            return v;
    return nullptr;
}

NTSTATUS CmpSetValue(CM_NODE *node, const WCHAR *name, ULONG chars, ULONG type, const void *data, ULONG length)
{
    NTSTATUS status = CmpLoadValues(node);
    if (!NT_SUCCESS(status))
        return status;
    PUCHAR copy = ExAllocatePool2(POOL_FLAG_NON_PAGED, length ? length : 1, KI_TAG('C', 'm', 'V', 'd'));
    if (!copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (length)
        memcpy(copy, data, length);
    CM_VALUE *v = CmpFindValue(node, name, chars);
    /* the same type and data again changes nothing: no record, no new time, no notification. */
    if (v && v->Type == type && v->DataLength == length && (!length || !memcmp(v->Data, data, length))) {
        ExFreePool(copy);
        return STATUS_SUCCESS;
    }
    BOOLEAN created = v == nullptr;
    if (!v) {
        v = new_value(name, chars);
        if (!v) {
            ExFreePool(copy);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    LARGE_INTEGER changed = now();
    status = CmpJournalAppend(node, CM_JOURNAL_SET_VALUE, type, name, chars, data, length, changed);
    if (!NT_SUCCESS(status)) {
        if (created)
            ExFreePool(v);
        ExFreePool(copy);
        return status;
    }
    if (created) {
        CM_VALUE **tail = &node->Values;
        while (*tail)
            tail = &(*tail)->Next;
        *tail = v;
    } else {
        ExFreePool(v->Data);
    }
    v->Type = type;
    v->Data = copy;
    v->DataLength = length;
    v->Written = TRUE;
    node->LastWrite = changed;
    CmpNotifyChange(node, REG_NOTIFY_CHANGE_LAST_SET);
    return STATUS_SUCCESS;
}

NTSTATUS CmpDeleteValue(CM_NODE *node, const WCHAR *name, ULONG chars)
{
    NTSTATUS status = CmpLoadValues(node);
    if (!NT_SUCCESS(status))
        return status;
    for (CM_VALUE **link = &node->Values; *link; link = &(*link)->Next) {
        CM_VALUE *v = *link;
        if (compare_names(v->Name, v->NameLength, name, chars) == 0) {
            LARGE_INTEGER changed = now();
            status = CmpJournalAppend(node, CM_JOURNAL_DELETE_VALUE, 0, name, chars, nullptr, 0, changed);
            if (!NT_SUCCESS(status))
                return status;
            *link = v->Next;
            ExFreePool(v->Data);
            ExFreePool(v);
            node->LastWrite = changed;
            CmpNotifyChange(node, REG_NOTIFY_CHANGE_LAST_SET);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/* the target, a \REGISTRY path, points into the value data. */
static BOOLEAN link_target(CM_NODE *node, PWCHAR *target, ULONG *chars)
{
    static const WCHAR name[] = L"SymbolicLinkValue";
    CmpLoadValues(node);
    CM_VALUE *v = CmpFindValue(node, name, RTL_NUMBER_OF(name) - 1);
    if (!v || v->Type != REG_LINK || v->DataLength < 2 * sizeof(WCHAR))
        return FALSE;
    *chars = v->DataLength / sizeof(WCHAR);
    *target = (PWCHAR)v->Data;
    while (*chars && (*target)[*chars - 1] == 0)
        (*chars)--;
    return TRUE;
}


/* ------------------------------------------------------------------ trace */

static BOOLEAN g_trace;
static ULONG64 g_trace_seen[1 << 16];

static ULONG64 trace_hash(ULONG64 h, const void *data, ULONG bytes)
{
    const UCHAR *p = data;
    for (ULONG i = 0; i < bytes; i++)
        h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

/* logs the first time a program touches a key or value: where it was found, the hive, a journal or nowhere. */
static void trace_registry(char kind, char where, CM_NODE *node, PCWSTR rest, ULONG rest_chars, PCWSTR value, ULONG value_chars)
{
    if (!g_trace)
        return;
    WCHAR path[300];
    ULONG chars = node ? CmpFullPath(node, path, RTL_NUMBER_OF(path) - 1) : 0;
    if (chars > RTL_NUMBER_OF(path) - 1)
        chars = RTL_NUMBER_OF(path) - 1;
    if (rest_chars && chars + 1 + rest_chars < RTL_NUMBER_OF(path)) {
        path[chars++] = '\\';
        memcpy(path + chars, rest, rest_chars * sizeof(WCHAR));
        chars += rest_chars;
    }
    ULONG64 h = 0xCBF29CE484222325ull;
    const char *image = PsCurrentProcess()->ImageFileName;
    h = trace_hash(h, &kind, 1);
    h = trace_hash(h, image, (ULONG)strlen(image));
    h = trace_hash(h, path, chars * sizeof(WCHAR));
    h = trace_hash(h, value, value_chars * sizeof(WCHAR));
    h = trace_hash(h, &where, 1);
    ULONG slot = (ULONG)h & (RTL_NUMBER_OF(g_trace_seen) - 1);
    for (ULONG probe = 0; probe < 64; probe++, slot = (slot + 1) & (RTL_NUMBER_OF(g_trace_seen) - 1)) {
        if (g_trace_seen[slot] == h)
            return;
        if (!g_trace_seen[slot]) {
            g_trace_seen[slot] = h;
            break;
        }
    }
    KiLogPrintf("regtrace %c%c %s %.*ls | %.*ls", kind, where, image, chars, path, value_chars, value ? value : L"");
}

/* ------------------------------------------------------------------ security */

/* logs a refusal with the key's path, its tail if too long. */
static void trace_key(CM_NODE *node, ACCESS_MASK access, NTSTATUS status)
{
    if (!SeTraceAccess)
        return;
    WCHAR path[96];
    ULONG at = RTL_NUMBER_OF(path) - 1;
    path[at] = 0;
    for (CM_NODE *n = node; n && at > 1; n = n->Parent) {
        ULONG take = n->NameLength < at - 1 ? n->NameLength : at - 1;
        at -= take;
        memcpy(path + at, n->Name, take * sizeof(WCHAR));
        path[--at] = '\\';
    }
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, path + at);
    SeTraceDenial("key", &name, access, status);
}

static SD_MAPPING key_mapping(void)
{
    return CmKeyType->Init.GenericMapping;
}

static CM_HIVE *g_user_hive;
static CM_NODE *g_user_key, *g_machine_key;

static BOOLEAN node_is(const CM_NODE *node, PCWSTR name)
{
    ULONG chars = (ULONG)wcslen(name);
    return node && node->NameLength == chars && compare_names(node->Name, chars, name, chars) == 0;
}

/*
 * HKLM\Software\Classes\Installer and below, where the products of the installer are recorded. the user may write
 * there whatever his token's rights, unlike the machine keys that decide who may be elevated, as windows' own
 * installer keeps its bookkeeping under the user's own classes key for a per user install.
 */
static BOOLEAN in_installer_catalog(const CM_NODE *node)
{
    for (; node; node = node->Parent) {
        if (node_is(node, L"Installer") && node->Parent && node_is(node->Parent, L"Classes") && node->Parent->Parent &&
            node_is(node->Parent->Parent, L"SOFTWARE") && g_machine_key && node->Parent->Parent->Parent == g_machine_key)
            return TRUE;
    }
    return FALSE;
}

/* HKCU\Software\AppDataLow, writable by low integrity programs, like the LocalLow folder. */
static BOOLEAN is_low_integrity_key(const CM_NODE *node)
{
    return node_is(node, L"AppDataLow") && node->Parent && node_is(node->Parent, L"Software") &&
           node->Parent->Parent && node->Parent->Parent->Parent == g_user_key && g_user_key;
}

/*
 * a referenced descriptor: the one set on the key, else the hive's, else what the parent hands down. lock held.
 */
static NTSTATUS effective_sd(CM_NODE *node, SD_BLOB **out)
{
    if (node->Sd) {
        *out = SeSdReference(node->Sd);
        return STATUS_SUCCESS;
    }
    if (node->Hive && !(node->Flags & CM_NODE_SD_UNREADABLE)) {
        SD_BLOB *sd;
        if (NT_SUCCESS(CmpReadSecurity(node->Hive, node->Cell, &sd))) {
            /* the user hive was made for nobody in particular: like windows, the profile gives its user full access. */
            if (node->Hive == g_user_hive) {
                UCHAR user[28];
                SD_BLOB *owned;
                MimeUserSid(user);
                if (NT_SUCCESS(SeSdAppendAllowAce(sd, user, 0xF003F, 0x2 /* container inherit */, &owned))) {
                    SeSdDereference(sd);
                    sd = owned;
                }
            }
            node->Sd = sd;
            *out = SeSdReference(sd);
            return STATUS_SUCCESS;
        }
        node->Flags |= CM_NODE_SD_UNREADABLE;
    }
    if (!node->Parent)
        return SeSdSystemDefault(key_mapping(), TRUE, out);
    SD_BLOB *parent;
    NTSTATUS status = effective_sd(node->Parent, &parent);
    if (!NT_SUCCESS(status))
        return status;
    status = SeSdDerive(parent, key_mapping(), TRUE, out);
    SeSdDereference(parent);
    if (NT_SUCCESS(status) && in_installer_catalog(node)) {
        SD_BLOB *open_to_users;
        if (NT_SUCCESS(SeSdAppendAllowAce(*out, SeSidAuthenticatedUsers, 0x3003F, 0, &open_to_users))) {
            SeSdDereference(*out);
            *out = open_to_users;
        }
    }
    if (NT_SUCCESS(status) && is_low_integrity_key(node)) {
        SD_BLOB *labelled;
        if (NT_SUCCESS(SeSdAddInheritedLabel(*out, 0x1000, &labelled))) {
            SeSdDereference(*out);
            *out = labelled;
        }
    }
    return status;
}

/* journaled first. lock held. */
static NTSTATUS store_sd(CM_NODE *node, SD_BLOB *sd)
{
    LARGE_INTEGER changed = now();
    NTSTATUS status = CmpJournalAppend(node, CM_JOURNAL_SET_SECURITY, 0, nullptr, 0, sd->Data, sd->Length, changed);
    if (!NT_SUCCESS(status))
        return status;
    SD_BLOB *old = node->Sd;
    node->Sd = SeSdReference(sd);
    node->LastWrite = changed;
    SeSdDereference(old);
    CmpNotifyChange(node, REG_NOTIFY_CHANGE_ATTRIBUTES | REG_NOTIFY_CHANGE_SECURITY);
    return STATUS_SUCCESS;
}

/*
 * follows links. path is a writable buffer of MAX_PATH_CHARS characters. lock held.
 */
static NTSTATUS walk(CM_NODE *start, PWCHAR path, ULONG len, ULONG attributes, CM_PARSE_CONTEXT *create,
                     CM_NODE **out)
{
    /* creating a key directly under a handle's key depends on the handle's access. */
    BOOLEAN by_handle = create && create->ByHandle;
    ACCESS_MASK handle_access = create ? create->HandleAccess : 0;
    CM_NODE *node = start;
    ULONG pos = 0;
    int reparse = 0;
    for (;;) {
        if (node->Flags & CM_NODE_DELETED)
            return STATUS_KEY_DELETED;
        while (pos < len && path[pos] == '\\')
            pos++;
        if (pos >= len)
            break;
        ULONG begin = pos;
        while (pos < len && path[pos] != '\\')
            pos++;
        ULONG chars = pos - begin;
        BOOLEAN last = true;
        for (ULONG i = pos; i < len; i++)
            if (path[i] != '\\')
                last = false;
        if (chars > 255)
            return STATUS_OBJECT_NAME_INVALID;
        for (ULONG i = begin; i < pos; i++)
            if (!path[i])
                return STATUS_OBJECT_NAME_INVALID;
        NTSTATUS status = CmpLoadChildren(node);
        if (!NT_SUCCESS(status))
            return status;
        CM_NODE *child = CmpFindChild(node, path + begin, chars);
        BOOLEAN view_container = create && create->ViewContainer &&
                                 compare_names(path + begin, chars, L"Wow6432Node", 11) == 0;
        if (view_container)
            create->ViewContainer = FALSE;
        if (!child) {
            if (!create || !create->Create || (!last && !create->Kernel && !view_container))
                return STATUS_OBJECT_NAME_NOT_FOUND;
            ULONG flags = 0;
            if ((create->Options & REG_OPTION_VOLATILE) ||
                (create->Kernel && !create->Replay && (node->Flags & CM_NODE_VOLATILE)))
                flags |= CM_NODE_VOLATILE;
            else if ((node->Flags & CM_NODE_VOLATILE) && node != CmpRoot)
                return STATUS_CHILD_MUST_BE_VOLATILE;
            if (last && (create->Options & REG_OPTION_CREATE_LINK))
                flags |= CM_NODE_LINK;
            /* a program needs KEY_CREATE_SUB_KEY on the parent. */
            if (create->Check && by_handle && node == start && !(handle_access & KEY_CREATE_SUB_KEY_)) {
                if (SeTraceAccess)
                    KiLogPrintf("  the key of a handle opened for %08x", handle_access);
                trace_key(node, KEY_CREATE_SUB_KEY_, STATUS_ACCESS_DENIED);
                return STATUS_ACCESS_DENIED;
            } else if (create->Check && !(by_handle && node == start)) {
                SD_BLOB *parent_sd;
                status = effective_sd(node, &parent_sd);
                if (NT_SUCCESS(status)) {
                    ACCESS_MASK unused;
                    status = SeCheckAccess(parent_sd, key_mapping(), KEY_CREATE_SUB_KEY_, 0,
                                           (create->Options & REG_OPTION_BACKUP_RESTORE_) ? SE_ACCESS_BACKUP_INTENT : 0,
                                           &unused);
                    if (!NT_SUCCESS(status))
                        SeTraceDescriptor(parent_sd);
                    SeSdDereference(parent_sd);
                }
                if (!NT_SUCCESS(status)) {
                    trace_key(node, KEY_CREATE_SUB_KEY_, status);
                    return status;
                }
            }
            /* the new key's descriptor is worked out first, so a refusal creates nothing. */
            SD_BLOB *sd = nullptr;
            if (last && !create->Replay && !create->Kernel && (create->Creator || !SeSdSubjectIsDefault())) {
                SD_BLOB *parent_sd;
                status = effective_sd(node, &parent_sd);
                if (NT_SUCCESS(status)) {
                    status = SeSdAssign(parent_sd, create->Creator, key_mapping(), TRUE, create->Mode, &sd);
                    SeSdDereference(parent_sd);
                }
                if (!NT_SUCCESS(status))
                    return status;
            }
            status = create_child(node, path + begin, chars, flags, &child);
            if (NT_SUCCESS(status) && sd)
                status = store_sd(child, sd);
            if (sd)
                SeSdDereference(sd);
            if (!NT_SUCCESS(status))
                return status;
            if (last)
                create->Disposition = REG_CREATED_NEW_KEY;
            node = child;
            continue;
        }
        PWCHAR target;
        ULONG target_chars;
        if ((child->Flags & CM_NODE_LINK) && !(last && (attributes & OBJ_OPENLINK)) &&
            link_target(child, &target, &target_chars)) {
            static const WCHAR prefix[] = L"\\REGISTRY";
            ULONG p = RTL_NUMBER_OF(prefix) - 1;
            if (++reparse > MAX_REPARSE || target_chars < p || compare_names(target, p, prefix, p) != 0)
                return STATUS_OBJECT_NAME_NOT_FOUND;
            ULONG rest = len - pos;
            if (target_chars - p + rest >= MAX_PATH_CHARS)
                return STATUS_NAME_TOO_LONG;
            memmove(path + target_chars - p, path + pos, rest * sizeof(WCHAR));
            memcpy(path, target + p, (target_chars - p) * sizeof(WCHAR));
            len = target_chars - p + rest;
            pos = 0;
            node = CmpRoot;
            continue;
        }
        node = child;
    }
    if (create && create->Disposition == 0)
        create->Disposition = REG_OPENED_EXISTING_KEY;
    *out = node;
    return STATUS_SUCCESS;
}

CM_NODE *CmpOpenPath(PCWSTR path, BOOLEAN create)
{
    static const WCHAR prefix[] = L"\\REGISTRY";
    ULONG len = (ULONG)wcslen(path), p = RTL_NUMBER_OF(prefix) - 1;
    if (len < p || compare_names(path, p, prefix, p) != 0 || len >= MAX_PATH_CHARS)
        return nullptr;
    PWCHAR buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, MAX_PATH_CHARS * sizeof(WCHAR), KI_TAG('C', 'm', 'P', 't'));
    if (!buffer)
        return nullptr;
    memcpy(buffer, path + p, (len - p) * sizeof(WCHAR));
    CM_PARSE_CONTEXT ctx = {create, TRUE, 0, 0};
    CM_NODE *node = nullptr;
    if (!NT_SUCCESS(walk(CmpRoot, buffer, len - p, 0, &ctx, &node)))
        node = nullptr;
    ExFreePool(buffer);
    return node;
}

/* ------------------------------------------------------------------ objects */

static NTSTATUS new_body(CM_NODE *node, PVOID *out)
{
    CM_KEY_BODY *body;
    NTSTATUS status = ObCreateObject(CmKeyType, nullptr, KernelMode, sizeof(*body), (PVOID *)&body);
    if (NT_SUCCESS(status)) {
        body->Node = node;
        body->Notify = nullptr;
        body->RegistryView = 0;
        body->PhysicalView = FALSE;
        *out = body;
    }
    return status;
}

static NTSTATUS key_parse(PVOID object, PVOID type, ULONG attributes, PUNICODE_STRING complete,
                          PUNICODE_STRING remaining, PVOID context, PVOID *found)
{
    if (type && type != CmKeyType)
        return STATUS_OBJECT_TYPE_MISMATCH;
    ULONG len = remaining->Length / sizeof(WCHAR);
    if (len >= MAX_PATH_CHARS)
        return STATUS_NAME_TOO_LONG;
    PWCHAR path = ExAllocatePool2(POOL_FLAG_NON_PAGED, MAX_PATH_CHARS * sizeof(WCHAR), KI_TAG('C', 'm', 'P', 't'));
    if (!path)
        return STATUS_INSUFFICIENT_RESOURCES;
    memcpy(path, remaining->Buffer, len * sizeof(WCHAR));
    CM_NODE *node;
    CM_PARSE_CONTEXT *ctx = context;
    if (ctx && ctx->Check && PsCurrentThread() && PsCurrentThread()->ParseRootObject == object) {
        ctx->ByHandle = TRUE;
        ctx->HandleAccess = PsCurrentThread()->ParseRootAccess;
    }
    ExAcquireFastMutex(&CmpLock);
    CM_KEY_BODY *root = object;
    CM_NODE *start = root->Node;
    ULONG view = root->RegistryView;
    BOOLEAN physical = root->PhysicalView;
    NTSTATUS status = STATUS_SUCCESS;
    if (ctx && ctx->SelectView) {
        if (ctx->RegistryView)
            view = ctx->RegistryView;
        if (!view)
            view = ctx->Mode == UserMode && PsCurrentProcess()->Machine == IMAGE_FILE_MACHINE_I386 ? 32 : 64;
        status = CmpMapRegistryView(root, view, path, &len, MAX_PATH_CHARS, &start, &physical, &ctx->ViewContainer);
        if (start != root->Node)
            ctx->ByHandle = FALSE;
    }
    PWCHAR requested = nullptr;
    if (g_trace) {
        requested = ExAllocatePool2(POOL_FLAG_NON_PAGED, (len + 1) * sizeof(WCHAR), KI_TAG('C', 'm', 'T', 'r'));
        if (requested)
            memcpy(requested, path, len * sizeof(WCHAR));
    }
    if (NT_SUCCESS(status))
        status = walk(start, path, len, attributes, context, &node);
    if (requested) {
        if (NT_SUCCESS(status))
            trace_registry('O', node->Hive ? 'h' : (node->Flags & CM_NODE_VOLATILE) ? 'v' : 'j', node, nullptr, 0, nullptr, 0);
        else
            trace_registry('O', 'm', start, requested, len, nullptr, 0);
        ExFreePool(requested);
    }
    /* an existing key is checked against its descriptor; a key the program just made is its own. */
    if (NT_SUCCESS(status) && ctx && ctx->Check && ctx->Disposition != REG_CREATED_NEW_KEY) {
        SD_BLOB *sd;
        status = effective_sd(node, &sd);
        if (NT_SUCCESS(status)) {
            /* the wow64 view flags are not rights, the descriptor knows nothing of them. */
            status = SeCheckAccess(sd, key_mapping(), ctx->Access & ~(ACCESS_MASK)(KEY_WOW64_64KEY_ | KEY_WOW64_32KEY_), 0,
                                   (ctx->Options & REG_OPTION_BACKUP_RESTORE_) ? SE_ACCESS_BACKUP_INTENT : 0, &ctx->Granted);
            SeSdDereference(sd);
        }
        if (NT_SUCCESS(status))
            ctx->Checked = TRUE;
        else
            trace_key(node, ctx->Access, status);
        if (!NT_SUCCESS(status) && SeTraceAccess) {
            SD_BLOB *shown;
            if (NT_SUCCESS(effective_sd(node, &shown))) {
                SeTraceDescriptor(shown);
                SeSdDereference(shown);
            }
        }
    }
    CmpUnlock();
    ExFreePool(path);
    if (NT_SUCCESS(status)) {
        status = new_body(node, found);
        if (NT_SUCCESS(status)) {
            ((CM_KEY_BODY *)*found)->RegistryView = (UCHAR)view;
            ((CM_KEY_BODY *)*found)->PhysicalView = physical;
        }
    }
    return status;
}

/* the last handle ends the change notifications waiting on it. */
static void key_close(PEPROCESS process, PVOID object, ULONG handle_count)
{
    if (handle_count <= 1)
        CmpNotifyClose(object);
}

static void key_delete_body(PVOID object)
{
    CmpNotifyDelete(object);
}

NTSTATUS CmpReferenceKeyByName(POBJECT_ATTRIBUTES attributes, KPROCESSOR_MODE mode, ACCESS_MASK access,
                               CM_KEY_BODY **body)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    NTSTATUS status = ObCaptureObjectAttributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status))
        return status;
    oa.Attributes |= OBJ_KERNEL_HANDLE;
    CM_PARSE_CONTEXT ctx = {FALSE, FALSE, 0, 0};
    ctx.Mode = mode;
    ctx.SelectView = mode == UserMode || (access & (KEY_WOW64_32KEY_ | KEY_WOW64_64KEY_));
    ctx.RegistryView = access & KEY_WOW64_32KEY_ ? 32 : access & KEY_WOW64_64KEY_ ? 64 : 0;
    if ((access & (KEY_WOW64_32KEY_ | KEY_WOW64_64KEY_)) == (KEY_WOW64_32KEY_ | KEY_WOW64_64KEY_)) {
        KiFreeCapturedString(&name);
        return STATUS_INVALID_PARAMETER;
    }
    access &= ~(ACCESS_MASK)(KEY_WOW64_32KEY_ | KEY_WOW64_64KEY_);
    HANDLE h;
    status = ObOpenObjectByName(&oa, CmKeyType, mode, access, &ctx, &h);
    KiFreeCapturedString(&name);
    if (!NT_SUCCESS(status))
        return status;
    status = ObReferenceHandle(h, CmKeyType, KernelMode, (PVOID *)body, nullptr);
    ObCloseHandle(h, KernelMode);
    return status;
}

static NTSTATUS key_get_security(PVOID object, PVOID *blob)
{
    CM_NODE *node = ((CM_KEY_BODY *)object)->Node;
    ExAcquireFastMutex(&CmpLock);
    NTSTATUS status = (node->Flags & CM_NODE_DELETED) ? STATUS_KEY_DELETED : effective_sd(node, (SD_BLOB **)blob);
    CmpUnlock();
    return status;
}

static NTSTATUS key_put_security(PVOID object, PVOID blob)
{
    CM_NODE *node = ((CM_KEY_BODY *)object)->Node;
    ExAcquireFastMutex(&CmpLock);
    NTSTATUS status = (node->Flags & CM_NODE_DELETED) ? STATUS_KEY_DELETED : store_sd(node, blob);
    CmpUnlock();
    return status;
}

/* refuses deleted keys. takes the lock on success. */
static NTSTATUS lock_key(HANDLE handle, CM_KEY_BODY **body, CM_NODE **node)
{
    NTSTATUS status = ObReferenceHandle(handle, CmKeyType, KeGetPreviousMode(), (PVOID *)body, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    ExAcquireFastMutex(&CmpLock);
    *node = (*body)->Node;
    if ((*node)->Flags & CM_NODE_DELETED) {
        CmpUnlock();
        ObDereferenceObject(*body);
        return STATUS_KEY_DELETED;
    }
    return STATUS_SUCCESS;
}

static void unlock_key(CM_KEY_BODY *body)
{
    CmpUnlock();
    ObDereferenceObject(body);
}

/* ------------------------------------------------------------------ output */

/* copies a fixed header and variable data out, following the windows partial copy rules. */
typedef struct OUT_BUFFER {
    PUCHAR Buffer;
    ULONG Size;
    ULONG Used;
} OUT_BUFFER;

static BOOLEAN out_init(OUT_BUFFER *o, ULONG size)
{
    o->Buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, size ? size : 1, KI_TAG('C', 'm', 'O', 'u'));
    o->Size = size;
    o->Used = 0;
    return o->Buffer != nullptr;
}

static NTSTATUS out_finish(OUT_BUFFER *o, ULONG fixed, PVOID user, ULONG length, PULONG returned)
{
    NTSTATUS status = STATUS_SUCCESS;
    if (returned)
        status = KiCopyToUser(returned, &o->Used, sizeof(ULONG));
    if (NT_SUCCESS(status)) {
        if (length < fixed)
            status = STATUS_BUFFER_TOO_SMALL;
        else if (length < o->Used)
            status = NT_SUCCESS(KiCopyToUser(user, o->Buffer, length)) ? STATUS_BUFFER_OVERFLOW : STATUS_ACCESS_VIOLATION;
        else
            status = KiCopyToUser(user, o->Buffer, o->Used);
    }
    ExFreePool(o->Buffer);
    return status;
}

#define KeyBasicInformation 0
#define KeyNodeInformation 1
#define KeyFullInformation 2
#define KeyNameInformation 3
#define KeyCachedInformation 4
#define KeyValueBasicInformation 0
#define KeyValueFullInformation 1
#define KeyValuePartialInformation 2
#define KeyValueFullInformationAlign64 3
#define KeyValuePartialInformationAlign64 4

ULONG CmpFullPath(CM_NODE *node, PWCHAR out, ULONG capacity)
{
    ULONG len = 0;
    for (CM_NODE *n = node; n; n = n->Parent)
        len += 1 + n->NameLength;
    if (len > capacity)
        return len;
    ULONG pos = len;
    for (CM_NODE *n = node; n; n = n->Parent) {
        pos -= n->NameLength;
        memcpy(out + pos, n->Name, n->NameLength * sizeof(WCHAR));
        out[--pos] = '\\';
    }
    return len;
}

typedef struct KEY_STATS {
    ULONG SubKeys, MaxNameLen, Values, MaxValueNameLen, MaxValueDataLen;
} KEY_STATS;

static void key_stats(CM_NODE *node, KEY_STATS *s)
{
    memset(s, 0, sizeof(*s));
    CmpLoadChildren(node);
    CmpLoadValues(node);
    for (CM_NODE *c = node->Child; c; c = c->Next) {
        s->SubKeys++;
        if (c->NameLength * sizeof(WCHAR) > s->MaxNameLen)
            s->MaxNameLen = c->NameLength * sizeof(WCHAR);
    }
    for (CM_VALUE *v = node->Values; v; v = v->Next) {
        s->Values++;
        if (v->NameLength * sizeof(WCHAR) > s->MaxValueNameLen)
            s->MaxValueNameLen = v->NameLength * sizeof(WCHAR);
        if (v->DataLength > s->MaxValueDataLen)
            s->MaxValueDataLen = v->DataLength;
    }
}

static void put32(PUCHAR p, ULONG v)
{
    memcpy(p, &v, 4);
}

static NTSTATUS key_info(CM_NODE *node, ULONG klass, OUT_BUFFER *o, ULONG *fixed)
{
    ULONG name_bytes = node->NameLength * sizeof(WCHAR);
    KEY_STATS s;
    switch (klass) {
    case KeyBasicInformation:
        *fixed = 16;
        if (!out_init(o, 16 + name_bytes))
            return STATUS_INSUFFICIENT_RESOURCES;
        memcpy(o->Buffer, &node->LastWrite, 8);
        put32(o->Buffer + 8, 0);
        put32(o->Buffer + 12, name_bytes);
        memcpy(o->Buffer + 16, node->Name, name_bytes);
        o->Used = 16 + name_bytes;
        return STATUS_SUCCESS;
    case KeyNodeInformation:
        *fixed = 24;
        if (!out_init(o, 24 + name_bytes))
            return STATUS_INSUFFICIENT_RESOURCES;
        memcpy(o->Buffer, &node->LastWrite, 8);
        put32(o->Buffer + 8, 0);
        put32(o->Buffer + 12, 0xFFFFFFFF); /* no class */
        put32(o->Buffer + 16, 0);
        put32(o->Buffer + 20, name_bytes);
        memcpy(o->Buffer + 24, node->Name, name_bytes);
        o->Used = 24 + name_bytes;
        return STATUS_SUCCESS;
    case KeyFullInformation:
    case KeyCachedInformation:
        key_stats(node, &s);
        *fixed = klass == KeyFullInformation ? 44 : 40;
        if (!out_init(o, *fixed))
            return STATUS_INSUFFICIENT_RESOURCES;
        memcpy(o->Buffer, &node->LastWrite, 8);
        put32(o->Buffer + 8, 0);
        if (klass == KeyFullInformation) {
            ULONG f[] = {0xFFFFFFFF, 0, s.SubKeys, s.MaxNameLen, 0, s.Values, s.MaxValueNameLen, s.MaxValueDataLen};
            memcpy(o->Buffer + 12, f, sizeof(f));
        } else {
            ULONG f[] = {s.SubKeys, s.MaxNameLen, s.Values, s.MaxValueNameLen, s.MaxValueDataLen, name_bytes};
            memcpy(o->Buffer + 12, f, sizeof(f));
        }
        o->Used = *fixed;
        return STATUS_SUCCESS;
    case KeyNameInformation: {
        *fixed = 4;
        ULONG chars = CmpFullPath(node, nullptr, 0);
        if (!out_init(o, 4 + chars * sizeof(WCHAR)))
            return STATUS_INSUFFICIENT_RESOURCES;
        CmpFullPath(node, (PWCHAR)(o->Buffer + 4), chars);
        put32(o->Buffer, chars * sizeof(WCHAR));
        o->Used = 4 + chars * sizeof(WCHAR);
        return STATUS_SUCCESS;
    }
    default:
        return STATUS_INVALID_PARAMETER;
    }
}

static NTSTATUS value_info(CM_VALUE *v, ULONG klass, OUT_BUFFER *o, ULONG *fixed)
{
    ULONG name_bytes = v->NameLength * sizeof(WCHAR);
    switch (klass) {
    case KeyValueBasicInformation:
        *fixed = 12;
        if (!out_init(o, 12 + name_bytes))
            return STATUS_INSUFFICIENT_RESOURCES;
        put32(o->Buffer, 0);
        put32(o->Buffer + 4, v->Type);
        put32(o->Buffer + 8, name_bytes);
        memcpy(o->Buffer + 12, v->Name, name_bytes);
        o->Used = 12 + name_bytes;
        return STATUS_SUCCESS;
    case KeyValueFullInformation:
    case KeyValueFullInformationAlign64: {
        *fixed = 20;
        ULONG align = klass == KeyValueFullInformation ? 4 : 8;
        ULONG data = (20 + name_bytes + align - 1) & ~(align - 1);
        if (!out_init(o, data + v->DataLength))
            return STATUS_INSUFFICIENT_RESOURCES;
        memset(o->Buffer, 0, data);
        put32(o->Buffer + 4, v->Type);
        put32(o->Buffer + 8, data);
        put32(o->Buffer + 12, v->DataLength);
        put32(o->Buffer + 16, name_bytes);
        memcpy(o->Buffer + 20, v->Name, name_bytes);
        memcpy(o->Buffer + data, v->Data, v->DataLength);
        o->Used = data + v->DataLength;
        return STATUS_SUCCESS;
    }
    case KeyValuePartialInformation:
        *fixed = 12;
        if (!out_init(o, 12 + v->DataLength))
            return STATUS_INSUFFICIENT_RESOURCES;
        put32(o->Buffer, 0);
        put32(o->Buffer + 4, v->Type);
        put32(o->Buffer + 8, v->DataLength);
        memcpy(o->Buffer + 12, v->Data, v->DataLength);
        o->Used = 12 + v->DataLength;
        return STATUS_SUCCESS;
    case KeyValuePartialInformationAlign64:
        *fixed = 8;
        if (!out_init(o, 8 + v->DataLength))
            return STATUS_INSUFFICIENT_RESOURCES;
        put32(o->Buffer, v->Type);
        put32(o->Buffer + 4, v->DataLength);
        memcpy(o->Buffer + 8, v->Data, v->DataLength);
        o->Used = 8 + v->DataLength;
        return STATUS_SUCCESS;
    default:
        return STATUS_INVALID_PARAMETER;
    }
}

/* ------------------------------------------------------------------ system services */

/* the handle gets the access the key's descriptor granted when checked. */
static NTSTATUS open_key(POBJECT_ATTRIBUTES oa, ACCESS_MASK access, CM_PARSE_CONTEXT *ctx, PHANDLE handle)
{
    KPROCESSOR_MODE mode = KeGetPreviousMode();
    if (!oa->ObjectName)
        return STATUS_OBJECT_NAME_INVALID;
    ULONG selectors = access & (KEY_WOW64_32KEY_ | KEY_WOW64_64KEY_);
    if (selectors == (KEY_WOW64_32KEY_ | KEY_WOW64_64KEY_))
        return STATUS_INVALID_PARAMETER;
    ctx->SelectView = mode == UserMode || selectors;
    ctx->RegistryView = selectors & KEY_WOW64_32KEY_ ? 32 : selectors & KEY_WOW64_64KEY_ ? 64 : 0;
    access &= ~(ACCESS_MASK)(KEY_WOW64_32KEY_ | KEY_WOW64_64KEY_);
    ctx->Mode = mode;
    ctx->Check = mode == UserMode && SeAccessCheckRequired();
    ctx->Access = access;
    PVOID object;
    NTSTATUS status = ObLookupObjectName(oa->ObjectName, oa->RootDirectory, oa->Attributes, CmKeyType, mode, ctx, &object);
    if (!NT_SUCCESS(status))
        return status;
    ULONG attrs = oa->Attributes;
    if (mode == UserMode)
        attrs &= ~OBJ_KERNEL_HANDLE;
    return ObInsertObject(object, ctx->Checked ? ctx->Granted : access, attrs, handle);
}

NTSTATUS NTAPI NtCreateKey(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, ULONG index,
                           PUNICODE_STRING klass, ULONG options, PULONG disposition)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    NTSTATUS status = ObCaptureObjectAttributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status))
        return status;
    CM_PARSE_CONTEXT ctx = {TRUE, FALSE, options, 0};
    ctx.Mode = KeGetPreviousMode();
    if (oa.SecurityDescriptor) {
        status = SeSdCapture(oa.SecurityDescriptor, ctx.Mode, &ctx.Creator);
        if (!NT_SUCCESS(status)) {
            KiFreeCapturedString(&name);
            return status;
        }
    }
    HANDLE h;
    status = open_key(&oa, access, &ctx, &h);
    if (ctx.Creator)
        SeSdDereference(ctx.Creator);
    KiFreeCapturedString(&name);
    if (NT_SUCCESS(status)) {
        status = ObStoreHandle(handle, h);
        if (NT_SUCCESS(status) && disposition)
            status = KiCopyToUser(disposition, &ctx.Disposition, sizeof(ULONG));
    }
    return status;
}

NTSTATUS NTAPI NtOpenKeyEx(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, ULONG options)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    NTSTATUS status = ObCaptureObjectAttributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status))
        return status;
    CM_PARSE_CONTEXT ctx = {FALSE, FALSE, options, 0};
    HANDLE h;
    status = open_key(&oa, access, &ctx, &h);
    KiFreeCapturedString(&name);
    if (NT_SUCCESS(status))
        status = ObStoreHandle(handle, h);
    return status;
}

NTSTATUS NTAPI NtOpenKey(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    return NtOpenKeyEx(handle, access, attributes, 0);
}

NTSTATUS NTAPI NtQueryKey(HANDLE key, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    CM_KEY_BODY *body;
    CM_NODE *node;
    NTSTATUS status = lock_key(key, &body, &node);
    if (!NT_SUCCESS(status))
        return status;
    OUT_BUFFER o;
    ULONG fixed;
    status = key_info(node, klass, &o, &fixed);
    unlock_key(body);
    return NT_SUCCESS(status) ? out_finish(&o, fixed, info, length, returned) : status;
}

NTSTATUS NTAPI NtEnumerateKey(HANDLE key, ULONG index, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    CM_KEY_BODY *body;
    CM_NODE *node;
    NTSTATUS status = lock_key(key, &body, &node);
    if (!NT_SUCCESS(status))
        return status;
    CmpLoadChildren(node);
    CM_NODE *child = node->Child;
    for (ULONG i = 0; child && i < index; i++)
        child = child->Next;
    trace_registry('S', node->Hive ? 'h' : 'j', node, nullptr, 0, nullptr, 0);
    OUT_BUFFER o;
    ULONG fixed;
    status = child ? key_info(child, klass, &o, &fixed) : STATUS_NO_MORE_ENTRIES;
    unlock_key(body);
    return NT_SUCCESS(status) ? out_finish(&o, fixed, info, length, returned) : status;
}

NTSTATUS NTAPI NtQueryValueKey(HANDLE key, PUNICODE_STRING name, ULONG klass, PVOID info, ULONG length,
                               PULONG returned)
{
    UNICODE_STRING captured;
    NTSTATUS status = KiCaptureUnicodeString(&captured, name);
    if (!NT_SUCCESS(status))
        return status;
    CM_KEY_BODY *body;
    CM_NODE *node;
    status = lock_key(key, &body, &node);
    if (!NT_SUCCESS(status)) {
        KiFreeCapturedString(&captured);
        return status;
    }
    CmpLoadValues(node);
    CM_VALUE *v = CmpFindValue(node, captured.Buffer, captured.Length / sizeof(WCHAR));
    trace_registry('V', !v ? 'm' : v->Written ? 'w' : 'h', node, nullptr, 0, captured.Buffer, captured.Length / sizeof(WCHAR));
    OUT_BUFFER o;
    ULONG fixed;
    status = v ? value_info(v, klass, &o, &fixed) : STATUS_OBJECT_NAME_NOT_FOUND;
    unlock_key(body);
    KiFreeCapturedString(&captured);
    return NT_SUCCESS(status) ? out_finish(&o, fixed, info, length, returned) : status;
}

NTSTATUS NTAPI NtEnumerateValueKey(HANDLE key, ULONG index, ULONG klass, PVOID info, ULONG length,
                                   PULONG returned)
{
    CM_KEY_BODY *body;
    CM_NODE *node;
    NTSTATUS status = lock_key(key, &body, &node);
    if (!NT_SUCCESS(status))
        return status;
    CmpLoadValues(node);
    CM_VALUE *v = node->Values;
    for (ULONG i = 0; v && i < index; i++)
        v = v->Next;
    trace_registry('E', node->Hive ? 'h' : 'j', node, nullptr, 0, nullptr, 0);
    OUT_BUFFER o;
    ULONG fixed;
    status = v ? value_info(v, klass, &o, &fixed) : STATUS_NO_MORE_ENTRIES;
    unlock_key(body);
    return NT_SUCCESS(status) ? out_finish(&o, fixed, info, length, returned) : status;
}

NTSTATUS NTAPI NtSetValueKey(HANDLE key, PUNICODE_STRING name, ULONG title_index, ULONG type, PVOID data,
                             ULONG size)
{
    if (size > (64u << 20))
        return STATUS_INVALID_PARAMETER;
    UNICODE_STRING captured;
    NTSTATUS status = KiCaptureUnicodeString(&captured, name);
    if (!NT_SUCCESS(status))
        return status;
    PUCHAR copy = ExAllocatePool2(POOL_FLAG_NON_PAGED, size ? size : 1, KI_TAG('C', 'm', 'S', 'v'));
    if (!copy) {
        KiFreeCapturedString(&captured);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    status = KiCopyFromUser(copy, data, size);
    CM_KEY_BODY *body;
    CM_NODE *node;
    if (NT_SUCCESS(status))
        status = lock_key(key, &body, &node);
    if (NT_SUCCESS(status)) {
        status = CmpSetValue(node, captured.Buffer, captured.Length / sizeof(WCHAR), type, copy, size);
        unlock_key(body);
    }
    ExFreePool(copy);
    KiFreeCapturedString(&captured);
    return status;
}

NTSTATUS NTAPI NtDeleteValueKey(HANDLE key, PUNICODE_STRING name)
{
    UNICODE_STRING captured;
    NTSTATUS status = KiCaptureUnicodeString(&captured, name);
    if (!NT_SUCCESS(status))
        return status;
    CM_KEY_BODY *body;
    CM_NODE *node;
    status = lock_key(key, &body, &node);
    if (NT_SUCCESS(status)) {
        status = CmpDeleteValue(node, captured.Buffer, captured.Length / sizeof(WCHAR));
        unlock_key(body);
    }
    KiFreeCapturedString(&captured);
    return status;
}

static NTSTATUS delete_key(CM_NODE *node)
{
    NTSTATUS status = CmpLoadChildren(node);
    if (!NT_SUCCESS(status))
        return status;
    if (node->Child || !node->Parent || (node->Hive && node->Cell == node->Hive->RootCell)) {
        status = STATUS_CANNOT_DELETE;
    } else {
        LARGE_INTEGER changed = now();
        status = CmpJournalAppend(node, CM_JOURNAL_DELETE_KEY, 0, nullptr, 0, nullptr, 0, changed);
        if (!NT_SUCCESS(status))
            return status;
        CM_NODE **link = &node->Parent->Child;
        while (*link != node)
            link = &(*link)->Next;
        *link = node->Next;
        node->Flags |= CM_NODE_DELETED;
        node->Parent->LastWrite = changed;
        CmpNotifyChange(node->Parent, REG_NOTIFY_CHANGE_NAME);
        CmpNotifyKeyDeleted(node);
    }
    return status;
}

/*
 * a journaled deletion takes the keys below with it. a compacted journal no longer holds the deletions of keys
 * below a key deleted later, and those keys may still be in the hive the journal replays onto.
 */
static NTSTATUS delete_tree(CM_NODE *node)
{
    for (;;) {
        CM_NODE *leaf = node;
        NTSTATUS status = CmpLoadChildren(leaf);
        while (NT_SUCCESS(status) && leaf->Child) {
            leaf = leaf->Child;
            status = CmpLoadChildren(leaf);
        }
        if (NT_SUCCESS(status))
            status = delete_key(leaf);
        if (!NT_SUCCESS(status) || leaf == node)
            return status;
    }
}

NTSTATUS CmpRestoreJournal(const CmJournalRecord *record, const UCHAR *payload)
{
    /* the walk edits the path and needs room to follow a link. replay is single threaded at boot. */
    static PWCHAR path;
    if (!path)
        path = ExAllocatePool2(POOL_FLAG_NON_PAGED, MAX_PATH_CHARS * sizeof(WCHAR), KI_TAG('C', 'm', 'J', 'p'));
    if (!path)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG prefix = 9;
    ULONG chars = record->path_bytes / sizeof(WCHAR) - prefix;
    memcpy(path, payload + prefix * sizeof(WCHAR), chars * sizeof(WCHAR));
    CM_PARSE_CONTEXT create = {TRUE, TRUE, 0, 0, TRUE};
    if (record->operation == CM_JOURNAL_CREATE_KEY && (record->type & CM_JOURNAL_LINK))
        create.Options = REG_OPTION_CREATE_LINK;
    BOOLEAN deleting = record->operation == CM_JOURNAL_DELETE_KEY || record->operation == CM_JOURNAL_DELETE_VALUE ||
                       record->operation == CM_JOURNAL_SET_SECURITY;
    CM_NODE *node;
    NTSTATUS status = walk(CmpRoot, path, chars, OBJ_OPENLINK, deleting ? nullptr : &create, &node);
    if (deleting && status == STATUS_OBJECT_NAME_NOT_FOUND)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(status))
        return status;
    if (node->Flags & CM_NODE_VOLATILE)
        return STATUS_REGISTRY_CORRUPT;
    const WCHAR *name = (const WCHAR *)(payload + record->path_bytes);
    const UCHAR *data = payload + record->path_bytes + record->name_bytes;
    switch (record->operation) {
    case CM_JOURNAL_CREATE_KEY:
        if (record->type & CM_JOURNAL_LINK)
            node->Flags |= CM_NODE_LINK;
        break;
    case CM_JOURNAL_SET_VALUE:
        status = CmpSetValue(node, name, record->name_bytes / sizeof(WCHAR), record->type, data, record->data_bytes);
        break;
    case CM_JOURNAL_DELETE_VALUE:
        status = CmpDeleteValue(node, name, record->name_bytes / sizeof(WCHAR));
        if (status == STATUS_OBJECT_NAME_NOT_FOUND)
            status = STATUS_SUCCESS;
        break;
    case CM_JOURNAL_DELETE_KEY:
        status = delete_tree(node);
        break;
    case CM_JOURNAL_SET_SECURITY: {
        SD_BLOB *sd;
        status = SeSdFromBytes(data, record->data_bytes, &sd);
        if (!NT_SUCCESS(status))
            return STATUS_REGISTRY_CORRUPT;
        SeSdDereference(node->Sd);
        node->Sd = sd;
        break;
    }
    default:
        return STATUS_REGISTRY_CORRUPT;
    }
    if (NT_SUCCESS(status)) {
        node->LastWrite.QuadPart = (LONGLONG)record->last_write;
        if (node->Parent && (record->operation == CM_JOURNAL_CREATE_KEY || record->operation == CM_JOURNAL_DELETE_KEY))
            node->Parent->LastWrite = node->LastWrite;
    }
    return status;
}

NTSTATUS NTAPI NtDeleteKey(HANDLE key)
{
    CM_KEY_BODY *body;
    CM_NODE *node;
    NTSTATUS status = lock_key(key, &body, &node);
    if (!NT_SUCCESS(status))
        return status;
    status = delete_key(node);
    unlock_key(body);
    return status;
}

NTSTATUS NTAPI NtFlushKey(HANDLE key)
{
    CM_KEY_BODY *body;
    CM_NODE *node;
    NTSTATUS status = lock_key(key, &body, &node);
    if (!NT_SUCCESS(status))
        return status;
    BOOLEAN is_volatile = (node->Flags & CM_NODE_VOLATILE) != 0;
    unlock_key(body);
    return CmpJournalFlush(is_volatile);
}

#define ZW_WRAP(call)                                                                                             \
    PKTHREAD t_ = KiCurrentThread();                                                                              \
    KPROCESSOR_MODE old_ = t_->PreviousMode;                                                                      \
    t_->PreviousMode = KernelMode;                                                                                \
    NTSTATUS st_ = call;                                                                                          \
    t_->PreviousMode = old_;                                                                                      \
    return st_

NTSTATUS NTAPI ZwCreateKey(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, ULONG index,
                           PUNICODE_STRING klass, ULONG options, PULONG disposition)
{
    ZW_WRAP(NtCreateKey(handle, access, attributes, index, klass, options, disposition));
}

NTSTATUS NTAPI ZwOpenKey(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    ZW_WRAP(NtOpenKey(handle, access, attributes));
}

NTSTATUS NTAPI ZwOpenKeyEx(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, ULONG options)
{
    ZW_WRAP(NtOpenKeyEx(handle, access, attributes, options));
}

NTSTATUS NTAPI ZwQueryKey(HANDLE key, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    ZW_WRAP(NtQueryKey(key, klass, info, length, returned));
}

NTSTATUS NTAPI ZwEnumerateKey(HANDLE key, ULONG index, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    ZW_WRAP(NtEnumerateKey(key, index, klass, info, length, returned));
}

NTSTATUS NTAPI ZwQueryValueKey(HANDLE key, PUNICODE_STRING name, ULONG klass, PVOID info, ULONG length,
                               PULONG returned)
{
    ZW_WRAP(NtQueryValueKey(key, name, klass, info, length, returned));
}

NTSTATUS NTAPI ZwEnumerateValueKey(HANDLE key, ULONG index, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    ZW_WRAP(NtEnumerateValueKey(key, index, klass, info, length, returned));
}

NTSTATUS NTAPI ZwSetValueKey(HANDLE key, PUNICODE_STRING name, ULONG title_index, ULONG type, PVOID data, ULONG size)
{
    ZW_WRAP(NtSetValueKey(key, name, title_index, type, data, size));
}

NTSTATUS NTAPI ZwDeleteKey(HANDLE key)
{
    ZW_WRAP(NtDeleteKey(key));
}

NTSTATUS NTAPI ZwDeleteValueKey(HANDLE key, PUNICODE_STRING name)
{
    ZW_WRAP(NtDeleteValueKey(key, name));
}

NTSTATUS NTAPI ZwFlushKey(HANDLE key)
{
    ZW_WRAP(NtFlushKey(key));
}

/* ------------------------------------------------------------------ queries for the kernel */

NTSTATUS CmQueryValue(PCWSTR key_path, PCWSTR name, PULONG type, PVOID data, PULONG length)
{
    ExAcquireFastMutex(&CmpLock);
    CM_NODE *node = CmpOpenPath(key_path, FALSE);
    CM_VALUE *v = nullptr;
    if (node) {
        CmpLoadValues(node);
        v = CmpFindValue(node, name, (ULONG)wcslen(name));
    }
    NTSTATUS status = STATUS_OBJECT_NAME_NOT_FOUND;
    if (v) {
        if (type)
            *type = v->Type;
        status = v->DataLength <= *length ? STATUS_SUCCESS : STATUS_BUFFER_OVERFLOW;
        if (NT_SUCCESS(status))
            memcpy(data, v->Data, v->DataLength);
        *length = v->DataLength;
    }
    CmpUnlock();
    return status;
}

NTSTATUS CmSetValue(PCWSTR key_path, PCWSTR name, ULONG type, const void *data, ULONG length)
{
    ExAcquireFastMutex(&CmpLock);
    CM_NODE *node = CmpOpenPath(key_path, TRUE);
    NTSTATUS status = node ? CmpSetValue(node, name, (ULONG)wcslen(name), type, data, length) : STATUS_OBJECT_NAME_NOT_FOUND;
    CmpUnlock();
    return status;
}

NTSTATUS CmEnumerateValues(PCWSTR key_path, void (*fn)(PVOID ctx, PCWSTR name, ULONG name_chars, ULONG type,
                                                        const void *data, ULONG length),
                           PVOID ctx)
{
    ExAcquireFastMutex(&CmpLock);
    CM_NODE *node = CmpOpenPath(key_path, FALSE);
    if (node) {
        CmpLoadValues(node);
        for (CM_VALUE *v = node->Values; v; v = v->Next)
            fn(ctx, v->Name, v->NameLength, v->Type, v->Data, v->DataLength);
    }
    CmpUnlock();
    return node ? STATUS_SUCCESS : STATUS_OBJECT_NAME_NOT_FOUND;
}

/* ------------------------------------------------------------------ boot */

static CM_HIVE *mount(CM_NODE *parent, PCWSTR name, PCWSTR file)
{
    CM_HIVE *hive;
    NTSTATUS status = CmpOpenHive(file, &hive);
    if (!NT_SUCCESS(status) || !CmpMount(parent, name, hive)) {
        KiLogPrintf("registry: cannot load the hive %ls: %08x", file, status);
        return nullptr;
    }
    return hive;
}

static void link_key(PCWSTR path, PCWSTR target)
{
    CM_NODE *node = CmpOpenPath(path, TRUE);
    if (!node)
        return;
    node->Flags |= CM_NODE_LINK | CM_NODE_VOLATILE;
    static const WCHAR name[] = L"SymbolicLinkValue";
    CmpSetValue(node, name, RTL_NUMBER_OF(name) - 1, REG_LINK, target, (ULONG)(wcslen(target) * sizeof(WCHAR)));
}

void CmInitSystem1(void)
{
    ExInitializeFastMutex(&CmpLock);
    g_trace = strstr(HalBootInfo->cmdline, "trace-registry") != nullptr;
    OBJECT_TYPE_INITIALIZER init = {};
    init.ValidAccessMask = 0xF003F;
    init.ParseProcedure = key_parse;
    init.CloseProcedure = key_close;
    init.DeleteProcedure = key_delete_body;
    ObSetGenericMapping(&init, 0x20019, 0x20006, 0x20019, 0xF003F);
    init.GetSecurity = key_get_security;
    init.PutSecurity = key_put_security;
    CmKeyType = ObCreateObjectType(L"Key", &init);
    CmpNotifyInitialize();

    static const WCHAR registry[] = L"REGISTRY";
    CmpRoot = new_node(nullptr, registry, RTL_NUMBER_OF(registry) - 1);
    CmpRoot->Flags = CM_NODE_CHILDREN_LOADED | CM_NODE_VALUES_LOADED | CM_NODE_VOLATILE;
    PVOID root;
    if (!CmpRoot || !NT_SUCCESS(new_body(CmpRoot, &root)))
        KeBugCheckEx(0x00000067, 1, 0, 0, 0); /* CONFIG_INITIALIZATION_FAILED */
    OBJECT_TO_HEADER(root)->Flags |= OB_FLAG_PERMANENT;
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"REGISTRY");
    ObInsertIntoDirectory(ObRootDirectory, root, &name);

    ExAcquireFastMutex(&CmpLock);
    CM_NODE *machine = CmpCreateChild(CmpRoot, L"MACHINE", 7, CM_NODE_VOLATILE);
    CM_NODE *user = CmpCreateChild(CmpRoot, L"USER", 4, CM_NODE_VOLATILE);
    CmpCreateChild(CmpRoot, L"A", 1, CM_NODE_VOLATILE);
    CmpCreateChild(CmpRoot, L"WC", 2, CM_NODE_VOLATILE);
    CmpCreateChild(machine, L"HARDWARE", 8, CM_NODE_VOLATILE);
    CmpCreateChild(machine, L"SAM", 3, CM_NODE_VOLATILE);
    CmpCreateChild(machine, L"SECURITY", 8, CM_NODE_VOLATILE);
    mount(machine, L"SYSTEM", L"\\SystemRoot\\System32\\config\\SYSTEM");
    mount(machine, L"SOFTWARE", L"\\SystemRoot\\System32\\config\\SOFTWARE");
    mount(user, L".DEFAULT", L"\\SystemRoot\\System32\\config\\DEFAULT");

    /* the logged on user's hive, which the profile service loads on windows. every profile starts as the default one. */
    WCHAR sid[96] = {};
    _snwprintf(sid, RTL_NUMBER_OF(sid) - 1, L"S-1-5-21-%u-%u-%u-1001", HalBootInfo->user_sid[0], HalBootInfo->user_sid[1],
             HalBootInfo->user_sid[2]);
    g_user_hive = mount(user, sid, L"\\??\\C:\\Users\\Default\\NTUSER.DAT");
    g_user_key = user;
    KiBootStamp("hives");
    CmpJournalInitialize();
    KiBootStamp("journal");

    /* the current control set, from the restored Select key. */
    ULONG current = 1;
    CM_NODE *select = CmpOpenPath(L"\\REGISTRY\\MACHINE\\SYSTEM\\Select", FALSE);
    if (select) {
        CmpLoadValues(select);
        CM_VALUE *v = CmpFindValue(select, L"Current", 7);
        if (v && v->Type == REG_DWORD && v->DataLength == 4)
            memcpy(&current, v->Data, 4);
    }
    WCHAR target[64] = {};
    _snwprintf(target, RTL_NUMBER_OF(target) - 1, L"\\REGISTRY\\MACHINE\\SYSTEM\\ControlSet%03u", current);
    link_key(L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet", target);
    link_key(L"\\REGISTRY\\USER\\S-1-5-18", L"\\REGISTRY\\USER\\.DEFAULT");
    /* the installer catalog exists from the start, so its write rule applies. */
    CmpOpenPath(L"\\REGISTRY\\MACHINE\\SOFTWARE\\Classes\\Installer", TRUE);
    /* and AppDataLow, which every profile has from the start. */
    WCHAR low_path[200] = {};
    _snwprintf(low_path, RTL_NUMBER_OF(low_path) - 1, L"\\REGISTRY\\USER\\%s\\Software\\AppDataLow", sid);
    CmpOpenPath(low_path, TRUE);
    g_machine_key = machine;

    CmpUnlock();

    KiBootStamp("control set");
    CmpSpecialize();
    KiBootStamp("specialized");
    CmpImportHostRoots();
    KiBootStamp("host roots");
    ExAcquireFastMutex(&CmpLock);
    CmpJournalEnable();
    CmpUnlock();
    CmpJournalStart();
}

/*
 * loading and unloading hives needs SeRestorePrivilege, which no token of the
 * session holds, so callers get what windows gives a user without it.
 */
NTSTATUS NTAPI NtLoadKey(POBJECT_ATTRIBUTES target, POBJECT_ATTRIBUTES source)
{
    return STATUS_PRIVILEGE_NOT_HELD;
}

NTSTATUS NTAPI NtLoadKey2(POBJECT_ATTRIBUTES target, POBJECT_ATTRIBUTES source, ULONG flags)
{
    return STATUS_PRIVILEGE_NOT_HELD;
}

NTSTATUS NTAPI NtLoadKeyEx(POBJECT_ATTRIBUTES target, POBJECT_ATTRIBUTES source, ULONG flags, HANDLE trust_key,
                           HANDLE event, ACCESS_MASK access, PHANDLE root, PVOID reserved)
{
    return STATUS_PRIVILEGE_NOT_HELD;
}

NTSTATUS NTAPI NtUnloadKey(POBJECT_ATTRIBUTES target)
{
    return STATUS_PRIVILEGE_NOT_HELD;
}

NTSTATUS NTAPI NtUnloadKey2(POBJECT_ATTRIBUTES target, ULONG flags)
{
    return STATUS_PRIVILEGE_NOT_HELD;
}

NTSTATUS NTAPI NtUnloadKeyEx(POBJECT_ATTRIBUTES target, HANDLE event)
{
    return STATUS_PRIVILEGE_NOT_HELD;
}
