/*
 * the network store interface, \Device\Nsi: the device nsi.dll asks for every table of the network stack. on windows
 * nsiproxy.sys takes the requests and netio.sys hands them to the module that owns the table; here both are this
 * file, and the tables are nsinet.c's.
 *
 * a request is one structure in the caller's memory, read and written in place, with pointers to the caller's
 * buffers for keys and for the three structures of an object. a 32-bit caller sends the layout of its own width,
 * which the device converts itself as nsiproxy does. checks and their statuses are netio's: a key of another size
 * than the table's is an invalid parameter, a structure too small a buffer too small, a size without a buffer an
 * invalid buffer size.
 */
#include "nsi_p.h"

#define STATUS_MORE_ENTRIES ((NTSTATUS)0x00000105L)
#define STATUS_NSI_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#define STATUS_NSI_INVALID_BUFFER_SIZE ((NTSTATUS)0xC0000206L)
#define STATUS_NSI_CANCELLED ((NTSTATUS)0xC0000120L)
#define STATUS_NSI_INTEGER_OVERFLOW ((NTSTATUS)0xC0000095L)

#define NSI_MAX_BYTES (16u << 20)
/* the state the network list manager publishes its connectivity under. */
#define WNF_NLM_GLOBAL_CONNECTIVITY_STATE 0x418B0223A3BC2075ull

/* a request in one shape for both widths of caller. */
typedef struct NSI_CALL {
    ULONG64 Reserved;
    ULONG64 Module;
    ULONG Table;
    ULONG Store;
    ULONG Action;
    ULONG64 Key;
    ULONG KeySize;
    ULONG64 Rw;
    ULONG RwSize;
    ULONG64 Dynamic;
    ULONG DynamicSize;
    ULONG64 Static;
    ULONG StaticSize;
    ULONG Count;
    ULONG Type;
    ULONG64 Data;
    ULONG DataSize;
    ULONG DataOffset;
} NSI_CALL;

/* a thread waiting for a table to change. */
typedef struct NSI_WAITER {
    LIST_ENTRY Links;
    IO_USER_REQUEST *Request;
    ULONG Module;
    ULONG Table;
    ULONG64 Signature;
} NSI_WAITER;

static PDEVICE_OBJECT g_device;
static KMUTEX g_lock;
static LIST_ENTRY g_waiters;
static KEVENT g_wake;
static ULONG64 g_connectivity = ~0ull;

static void lock(void)
{
    KeWaitForSingleObject(&g_lock, Executive, KernelMode, FALSE, nullptr);
}

static void unlock(void)
{
    KeReleaseMutex(&g_lock, FALSE);
}

static BOOLEAN narrow(void)
{
    return PspIsWow64Process(PsCurrentProcess());
}

static ULONG64 field(const UCHAR *request, ULONG at64, ULONG at32, BOOLEAN pointer)
{
    if (narrow()) {
        ULONG v;
        memcpy(&v, request + at32, 4);
        return v;
    }
    ULONG64 v = 0;
    memcpy(&v, request + at64, pointer ? 8 : 4);
    return v;
}

/* the fields every request starts with, up to the key. */
static void common(NSI_CALL *c, const UCHAR *r)
{
    memset(c, 0, sizeof(*c));
    c->Reserved = field(r, 0, 0, TRUE);
    c->Module = field(r, 0x10, 0x08, TRUE);
    c->Table = (ULONG)field(r, 0x18, 0x0C, FALSE);
    c->Store = (ULONG)field(r, 0x20, 0x10, FALSE);
    c->Action = (ULONG)field(r, 0x24, 0x14, FALSE);
    c->Key = field(r, 0x28, 0x18, TRUE);
    c->KeySize = (ULONG)field(r, 0x30, 0x1C, FALSE);
}

static void structures(NSI_CALL *c, const UCHAR *r)
{
    c->Rw = field(r, 0x38, 0x20, TRUE);
    c->RwSize = (ULONG)field(r, 0x40, 0x24, FALSE);
    c->Dynamic = field(r, 0x48, 0x28, TRUE);
    c->DynamicSize = (ULONG)field(r, 0x50, 0x2C, FALSE);
    c->Static = field(r, 0x58, 0x30, TRUE);
    c->StaticSize = (ULONG)field(r, 0x60, 0x34, FALSE);
}

static void parameter(NSI_CALL *c, const UCHAR *r)
{
    c->Type = (ULONG)field(r, 0x38, 0x20, FALSE);
    c->Data = field(r, 0x40, 0x24, TRUE);
    c->DataSize = (ULONG)field(r, 0x48, 0x28, FALSE);
    c->DataOffset = (ULONG)field(r, 0x4C, 0x2C, FALSE);
}

static NTSTATUS write32(PVOID request, ULONG at64, ULONG at32, ULONG value)
{
    return KiCopyToUser((PUCHAR)request + (narrow() ? at32 : at64), &value, sizeof(value));
}

/* the module a request names, by the byte that tells the modules' guids apart. */
static NTSTATUS module_of(ULONG64 pointer, ULONG *module)
{
    static const GUID stem = {0xEB004A00, 0x9B1A, 0x11D4, {0x91, 0x23, 0x00, 0x50, 0x04, 0x77, 0x59, 0xBC}};
    NSI_MODULEID id;
    if (!pointer)
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = KiCopyFromUser(&id, (PVOID)(ULONG_PTR)pointer, sizeof(id));
    if (!NT_SUCCESS(status))
        return status;
    GUID guid = id.Guid;
    ULONG low = guid.Data1 & 0xFF;
    guid.Data1 &= ~0xFFu;
    ULONG tables;
    if (id.Type != 1 || memcmp(&guid, &stem, sizeof(guid)) || (!NsiNetTable(low, 0, &tables) && !tables))
        return STATUS_NOT_SUPPORTED;
    *module = low;
    return STATUS_SUCCESS;
}

static void *zeroed(SIZE_T size)
{
    void *p = ExAllocatePool2(POOL_FLAG_NON_PAGED, size ? size : 1, KI_TAG('N', 's', 'i', 'B'));
    if (p)
        memset(p, 0, size ? size : 1);
    return p;
}

/* one object's structures, each of the table's size. */
typedef struct NSI_SCRATCH {
    PUCHAR Key, Rw, Dynamic, Static;
} NSI_SCRATCH;

static BOOLEAN scratch_new(NSI_SCRATCH *s, const NSI_TABLE *t)
{
    s->Key = zeroed((SIZE_T)t->Key + t->Rw + t->Dynamic + t->Static);
    if (!s->Key)
        return FALSE;
    s->Rw = s->Key + t->Key;
    s->Dynamic = s->Rw + t->Rw;
    s->Static = s->Dynamic + t->Dynamic;
    return TRUE;
}

static void scratch_clear(NSI_SCRATCH *s, const NSI_TABLE *t)
{
    memset(s->Key, 0, (SIZE_T)t->Key + t->Rw + t->Dynamic + t->Static);
}

/* the object of a key: computed by the table, or found among its objects. */
static NTSTATUS find(const NSI_NET *net, const NSI_TABLE *t, ULONG module, ULONG table, const UCHAR *key,
                     NSI_SCRATCH *s)
{
    if (t->Get)
        return t->Get(net, module, table, key, s->Rw, s->Dynamic, s->Static);
    if (!t->Count)
        return STATUS_NOT_SUPPORTED;
    ULONG count = t->Count(net, module, table);
    for (ULONG i = 0; i < count; i++) {
        scratch_clear(s, t);
        t->Object(net, module, table, i, s->Key, s->Rw, s->Dynamic, s->Static);
        if (!memcmp(s->Key, key, t->Key))
            return STATUS_SUCCESS;
    }
    return module == NSI_MODULE_NDIS ? STATUS_NSI_NO_OBJECT : STATUS_NSI_NOT_FOUND;
}

/* a hash of what an object holds apart from its key, without what is no change: counters, ages and lifetimes. */
static ULONG64 object_hash(const NSI_TABLE *t, ULONG module, ULONG table, const NSI_SCRATCH *s)
{
    ULONG64 h = 0xCBF29CE484222325ull;
    BOOLEAN ip = module == NSI_MODULE_IPV4 || module == NSI_MODULE_IPV6;
    BOOLEAN route = ip && table == NSI_IP_FORWARD, address = ip && table == NSI_IP_UNICAST;
    for (const UCHAR *p = s->Rw; p < s->Static + t->Static; p++) {
        if ((t->Flags & NSI_T_COUNTERS) && p >= s->Dynamic && p < s->Static)
            continue;
        if (route && p >= s->Dynamic && p < s->Dynamic + 4)
            continue;
        /* the lifetimes of an address run down every second. */
        if (address && p < s->Rw + 8)
            continue;
        h = (h ^ *p) * 0x100000001B3ull;
    }
    return h;
}

/* a hash of everything a table holds that a change notification is about. */
static ULONG64 signature(const NSI_NET *net, ULONG module, ULONG table)
{
    ULONG tables;
    const NSI_TABLE *t = NsiNetTable(module, table, &tables);
    ULONG64 h = 0xCBF29CE484222325ull;
    NSI_SCRATCH s;
    if (!net || !t || !t->Count || !scratch_new(&s, t))
        return h;
    ULONG count = t->Count(net, module, table);
    for (ULONG i = 0; i < count; i++) {
        scratch_clear(&s, t);
        t->Object(net, module, table, i, s.Key, s.Rw, s.Dynamic, s.Static);
        for (ULONG k = 0; k < t->Key; k++)
            h = (h ^ s.Key[k]) * 0x100000001B3ull;
        h = (h ^ object_hash(t, module, table, &s)) * 0x100000001B3ull;
    }
    ExFreePool(s.Key);
    return h;
}

/* mime's request for its nsi service: the keys of a table with a hash of each object. */
static NTSTATUS list_objects(PVOID input, ULONG in_len, PVOID output, ULONG out_len, ULONG_PTR *information)
{
    NSI_MIME_OBJECTS_QUERY query;
    if (!input || in_len < sizeof(query) || !output || out_len < sizeof(NSI_MIME_OBJECTS))
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = KiCopyFromUser(&query, input, sizeof(query));
    if (!NT_SUCCESS(status))
        return status;
    static const GUID stem = {0xEB004A00, 0x9B1A, 0x11D4, {0x91, 0x23, 0x00, 0x50, 0x04, 0x77, 0x59, 0xBC}};
    GUID guid = query.Module.Guid;
    ULONG module = guid.Data1 & 0xFF, tables;
    guid.Data1 &= ~0xFFu;
    const NSI_TABLE *t = memcmp(&guid, &stem, sizeof(guid)) ? nullptr : NsiNetTable(module, query.Table, &tables);
    if (!t || !t->Count)
        return STATUS_NOT_SUPPORTED;
    NSI_SCRATCH s;
    if (!scratch_new(&s, t))
        return STATUS_INSUFFICIENT_RESOURCES;
    lock();
    const NSI_NET *net = NsiNetCurrent(FALSE, module);
    ULONG count = net ? t->Count(net, module, query.Table) : 0;
    SIZE_T each = (SIZE_T)t->Key + sizeof(ULONG64);
    SIZE_T size = sizeof(NSI_MIME_OBJECTS) + count * each;
    NSI_MIME_OBJECTS *list = zeroed(size);
    if (list) {
        list->KeySize = t->Key;
        list->Count = count;
        for (ULONG i = 0; i < count; i++) {
            scratch_clear(&s, t);
            t->Object(net, module, query.Table, i, s.Key, s.Rw, s.Dynamic, s.Static);
            ULONG64 h = object_hash(t, module, query.Table, &s);
            memcpy(list->Objects + i * each, s.Key, t->Key);
            memcpy(list->Objects + i * each + t->Key, &h, sizeof(h));
        }
    }
    unlock();
    ExFreePool(s.Key);
    if (!list)
        return STATUS_INSUFFICIENT_RESOURCES;
    status = KiCopyToUser(output, list, size <= out_len ? size : sizeof(NSI_MIME_OBJECTS));
    if (NT_SUCCESS(status)) {
        *information = size;
        if (size > out_len)
            status = (NTSTATUS)0x80000005L; /* STATUS_BUFFER_OVERFLOW */
    }
    ExFreePool(list);
    return status;
}

/* ------------------------------------------------------------------ requests */

/* netio's checks of a structure's buffer against the size the table has for it. */
static NTSTATUS check_structure(ULONG64 *pointer, ULONG size, ULONG wanted)
{
    if (!*pointer)
        return size ? STATUS_NSI_INVALID_BUFFER_SIZE : STATUS_SUCCESS;
    if (size < wanted)
        return STATUS_NSI_BUFFER_TOO_SMALL;
    if (!size)
        *pointer = 0;
    return STATUS_SUCCESS;
}

static NTSTATUS check_store(const NSI_CALL *c)
{
    if (c->Reserved)
        return STATUS_NOT_IMPLEMENTED;
    /* the persistent store is the registry of a machine set up by hand, which this one is not. */
    if (c->Store != NSI_STORE_ACTIVE)
        return c->Store > 3 || c->Store == 2 ? STATUS_INVALID_PARAMETER : STATUS_NSI_NOT_FOUND;
    return STATUS_SUCCESS;
}

static NTSTATUS enumerate(PVOID request, ULONG in_len, ULONG_PTR *information)
{
    UCHAR r[sizeof(NSI_ENUMERATE_ALL)];
    ULONG length = narrow() ? 0x3C : sizeof(r);
    if (!request || in_len < length)
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = KiCopyFromUser(r, request, length);
    if (!NT_SUCCESS(status))
        return status;
    NSI_CALL c;
    common(&c, r);
    structures(&c, r);
    c.Count = (ULONG)field(r, 0x68, 0x38, FALSE);
    /* with no room for an object the sizes are not looked at. */
    if (!c.Count)
        c.KeySize = c.RwSize = c.DynamicSize = c.StaticSize = 0;
    if ((c.Store & ~3u) || c.Store == 2 || c.Action - 1 > 1)
        return STATUS_INVALID_PARAMETER;
    status = check_store(&c);
    if (!NT_SUCCESS(status))
        return status;
    ULONG module, tables;
    status = module_of(c.Module, &module);
    if (!NT_SUCCESS(status))
        return status;
    const NSI_TABLE *t = NsiNetTable(module, c.Table, &tables);
    if (!t)
        return STATUS_INVALID_PARAMETER;
    if (c.KeySize) {
        if (c.KeySize != t->Key || !c.Key)
            return STATUS_INVALID_PARAMETER;
    } else if (c.Key) {
        c.Key = 0;
    } else if (c.Count) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!NT_SUCCESS(status = check_structure(&c.Rw, c.RwSize, t->Rw)) ||
        !NT_SUCCESS(status = check_structure(&c.Dynamic, c.DynamicSize, t->Dynamic)) ||
        !NT_SUCCESS(status = check_structure(&c.Static, c.StaticSize, t->Static)))
        return status;
    ULONG64 each = (ULONG64)c.KeySize + c.RwSize + c.DynamicSize + c.StaticSize;
    if (each * c.Count > 0xFFFFFFFFull)
        return STATUS_NSI_INTEGER_OVERFLOW;
    if (!t->Count)
        return (t->Flags & NSI_T_UNSUPPORTED) ? STATUS_NOT_SUPPORTED : STATUS_NOT_IMPLEMENTED;
    if (each * c.Count > NSI_MAX_BYTES)
        return STATUS_INSUFFICIENT_RESOURCES;

    lock();
    const NSI_NET *net = NsiNetCurrent(FALSE, module);
    NSI_SCRATCH s = {};
    PUCHAR keys = nullptr, rw = nullptr, dynamic = nullptr, fixed = nullptr, all_keys = nullptr;
    ULONG *order = nullptr;
    ULONG total = 0, taken = 0, count = 0;
    if (!net || !scratch_new(&s, t)) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }
    total = t->Count(net, module, c.Table);
    taken = total < c.Count ? total : c.Count;
    if (c.Key) {
        keys = zeroed((SIZE_T)c.Count * c.KeySize);
        rw = c.Rw ? zeroed((SIZE_T)c.Count * c.RwSize) : nullptr;
        dynamic = c.Dynamic ? zeroed((SIZE_T)c.Count * c.DynamicSize) : nullptr;
        fixed = c.Static ? zeroed((SIZE_T)c.Count * c.StaticSize) : nullptr;
        order = zeroed((SIZE_T)(total + 1) * sizeof(ULONG));
        all_keys = zeroed((SIZE_T)(total + 1) * (t->Key ? t->Key : 1));
        if (!keys || (c.Rw && !rw) || (c.Dynamic && !dynamic) || (c.Static && !fixed) || !order || !all_keys) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto done;
        }
        for (ULONG i = 0; i < total; i++) {
            scratch_clear(&s, t);
            t->Object(net, module, c.Table, i, s.Key, s.Rw, s.Dynamic, s.Static);
            memcpy(all_keys + (SIZE_T)i * t->Key, s.Key, t->Key);
            ULONG at = i;
            if (t->Flags & NSI_T_SORT)
                for (; at && memcmp(all_keys + (SIZE_T)order[at - 1] * t->Key, s.Key, t->Key) > 0; at--)
                    order[at] = order[at - 1];
            order[at] = i;
        }
        for (ULONG i = 0; i < taken; i++) {
            scratch_clear(&s, t);
            t->Object(net, module, c.Table, order[i], s.Key, s.Rw, s.Dynamic, s.Static);
            memcpy(keys + (SIZE_T)i * c.KeySize, s.Key, t->Key);
            if (rw)
                memcpy(rw + (SIZE_T)i * c.RwSize, s.Rw, t->Rw);
            if (dynamic)
                memcpy(dynamic + (SIZE_T)i * c.DynamicSize, s.Dynamic, t->Dynamic);
            if (fixed)
                memcpy(fixed + (SIZE_T)i * c.StaticSize, s.Static, t->Static);
        }
    }
    /* a table too long for the buffers is told by the status. its count is the room that was there, or all its
       objects for the tables whose module counts them. */
    if (total > c.Count && (c.Key || (t->Flags & NSI_T_COUNT_MORE)))
        status = STATUS_MORE_ENTRIES;
    else
        status = STATUS_SUCCESS;
    count = !c.Key || (t->Flags & NSI_T_COUNT_MORE) ? total : taken;
done:
    unlock();
    if (NT_SUCCESS(status)) {
        NTSTATUS copied = write32(request, 0x68, 0x38, count);
        if (NT_SUCCESS(copied) && keys)
            copied = KiCopyToUser((PVOID)(ULONG_PTR)c.Key, keys, (SIZE_T)c.Count * c.KeySize);
        if (NT_SUCCESS(copied) && rw)
            copied = KiCopyToUser((PVOID)(ULONG_PTR)c.Rw, rw, (SIZE_T)c.Count * c.RwSize);
        if (NT_SUCCESS(copied) && dynamic)
            copied = KiCopyToUser((PVOID)(ULONG_PTR)c.Dynamic, dynamic, (SIZE_T)c.Count * c.DynamicSize);
        if (NT_SUCCESS(copied) && fixed)
            copied = KiCopyToUser((PVOID)(ULONG_PTR)c.Static, fixed, (SIZE_T)c.Count * c.StaticSize);
        if (!NT_SUCCESS(copied))
            status = copied;
        else
            *information = sizeof(NSI_ENUMERATE_ALL);
    }
    void *blocks[] = {s.Key, keys, rw, dynamic, fixed, order, all_keys};
    for (ULONG i = 0; i < RTL_NUMBER_OF(blocks); i++)
        if (blocks[i])
            ExFreePool(blocks[i]);
    return status;
}

/* the key of a request in a pool block, checked against the table's. */
static NTSTATUS take_key(const NSI_CALL *c, const NSI_TABLE *t, PUCHAR *key)
{
    *key = nullptr;
    if (c->KeySize != t->Key || (c->KeySize && !c->Key))
        return STATUS_INVALID_PARAMETER;
    *key = zeroed(t->Key);
    if (!*key)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = t->Key ? KiCopyFromUser(*key, (PVOID)(ULONG_PTR)c->Key, t->Key) : STATUS_SUCCESS;
    if (!NT_SUCCESS(status)) {
        ExFreePool(*key);
        *key = nullptr;
    }
    return status;
}

static NTSTATUS get_all(PVOID request, ULONG in_len, ULONG_PTR *information)
{
    UCHAR r[sizeof(NSI_GET_ALL_PARAMETERS)];
    ULONG length = narrow() ? 0x38 : sizeof(r);
    if (!request || in_len < length)
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = KiCopyFromUser(r, request, length);
    if (!NT_SUCCESS(status))
        return status;
    NSI_CALL c;
    common(&c, r);
    structures(&c, r);
    if (c.Store > 4 || c.Action > 2 || c.Store == 2)
        return STATUS_INVALID_PARAMETER;
    status = check_store(&c);
    if (!NT_SUCCESS(status))
        return status;
    ULONG module, tables;
    status = module_of(c.Module, &module);
    if (!NT_SUCCESS(status))
        return status;
    const NSI_TABLE *t = NsiNetTable(module, c.Table, &tables);
    if (!t)
        return STATUS_INVALID_PARAMETER;
    /* a table that has objects only by enumeration, or none at all. */
    if (!t->Get && !t->Count)
        return STATUS_NOT_SUPPORTED;
    PUCHAR key;
    status = take_key(&c, t, &key);
    if (!NT_SUCCESS(status))
        return status;
    if (!NT_SUCCESS(status = check_structure(&c.Rw, c.RwSize, t->Rw)) ||
        !NT_SUCCESS(status = check_structure(&c.Dynamic, c.DynamicSize, t->Dynamic)) ||
        !NT_SUCCESS(status = check_structure(&c.Static, c.StaticSize, t->Static))) {
        ExFreePool(key);
        return status;
    }
    NSI_SCRATCH s;
    if (!scratch_new(&s, t)) {
        ExFreePool(key);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    lock();
    const NSI_NET *net = NsiNetCurrent(FALSE, module);
    status = net ? find(net, t, module, c.Table, key, &s) : STATUS_INSUFFICIENT_RESOURCES;
    unlock();
    if (NT_SUCCESS(status)) {
        /* a larger buffer takes the table's size, which the request then tells. */
        NTSTATUS copied = STATUS_SUCCESS;
        if (c.Rw)
            copied = KiCopyToUser((PVOID)(ULONG_PTR)c.Rw, s.Rw, t->Rw);
        if (NT_SUCCESS(copied) && c.Dynamic)
            copied = KiCopyToUser((PVOID)(ULONG_PTR)c.Dynamic, s.Dynamic, t->Dynamic);
        if (NT_SUCCESS(copied) && c.Static)
            copied = KiCopyToUser((PVOID)(ULONG_PTR)c.Static, s.Static, t->Static);
        if (NT_SUCCESS(copied) && c.Rw && c.RwSize != t->Rw)
            copied = write32(request, 0x40, 0x24, t->Rw);
        if (NT_SUCCESS(copied) && c.Dynamic && c.DynamicSize != t->Dynamic)
            copied = write32(request, 0x50, 0x2C, t->Dynamic);
        if (NT_SUCCESS(copied) && c.Static && c.StaticSize != t->Static)
            copied = write32(request, 0x60, 0x34, t->Static);
        if (!NT_SUCCESS(copied))
            status = copied;
        else
            *information = sizeof(NSI_GET_ALL_PARAMETERS);
    }
    ExFreePool(s.Key);
    ExFreePool(key);
    return status;
}

static NTSTATUS get_parameter(PVOID request, ULONG in_len, ULONG_PTR *information)
{
    UCHAR r[sizeof(NSI_GET_PARAMETER)];
    ULONG length = narrow() ? 0x30 : sizeof(r);
    if (!request || in_len < length)
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = KiCopyFromUser(r, request, length);
    if (!NT_SUCCESS(status))
        return status;
    NSI_CALL c;
    common(&c, r);
    parameter(&c, r);
    if (c.Store > 3 || c.Action > 2 || c.Store == 2 || c.Type > NSI_PARAM_STATIC)
        return STATUS_INVALID_PARAMETER;
    if ((ULONG64)c.DataSize + c.DataOffset > 0xFFFFFFFFull)
        return STATUS_INVALID_PARAMETER;
    status = check_store(&c);
    if (!NT_SUCCESS(status))
        return status;
    ULONG module, tables;
    status = module_of(c.Module, &module);
    if (!NT_SUCCESS(status))
        return status;
    const NSI_TABLE *t = NsiNetTable(module, c.Table, &tables);
    if (!t)
        return STATUS_INVALID_PARAMETER;
    PUCHAR key;
    status = take_key(&c, t, &key);
    if (!NT_SUCCESS(status))
        return status;
    if (c.Data && !c.DataSize)
        c.Data = 0;
    ULONG size = c.Type == NSI_PARAM_RW ? t->Rw : c.Type == NSI_PARAM_DYNAMIC ? t->Dynamic : t->Static;
    if (!size || (!t->Get && !t->Count))
        status = STATUS_NOT_SUPPORTED;
    else if (c.DataSize + c.DataOffset > size)
        status = STATUS_INVALID_PARAMETER;
    NSI_SCRATCH s = {};
    if (NT_SUCCESS(status) && !scratch_new(&s, t))
        status = STATUS_INSUFFICIENT_RESOURCES;
    if (NT_SUCCESS(status)) {
        lock();
        const NSI_NET *net = NsiNetCurrent(FALSE, module);
        status = net ? find(net, t, module, c.Table, key, &s) : STATUS_INSUFFICIENT_RESOURCES;
        unlock();
    }
    if (NT_SUCCESS(status) && c.Data) {
        const UCHAR *from = c.Type == NSI_PARAM_RW ? s.Rw : c.Type == NSI_PARAM_DYNAMIC ? s.Dynamic : s.Static;
        status = KiCopyToUser((PVOID)(ULONG_PTR)c.Data, from + c.DataOffset, c.DataSize);
    }
    if (NT_SUCCESS(status))
        *information = sizeof(NSI_GET_PARAMETER);
    if (s.Key)
        ExFreePool(s.Key);
    ExFreePool(key);
    return status;
}

/*
 * a parameter or the parameters of an object set. the stack is the host's, which no program of the machine
 * configures: only the tables that keep a state of their own take a value, the rest refuse like windows refuses a
 * caller that is no network operator.
 */
static NTSTATUS set(PVOID request, ULONG in_len, BOOLEAN all)
{
    UCHAR r[sizeof(NSI_GET_PARAMETER)] = {};
    ULONG length = narrow() ? (all ? 0x28 : 0x30) : (all ? 0x48 : 0x50);
    if (!request || in_len < length)
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = KiCopyFromUser(r, request, length);
    if (!NT_SUCCESS(status))
        return status;
    NSI_CALL c;
    common(&c, r);
    if (all) {
        c.Type = NSI_PARAM_RW;
        c.Data = field(r, 0x38, 0x20, TRUE);
        c.DataSize = (ULONG)field(r, 0x40, 0x24, FALSE);
    } else {
        parameter(&c, r);
    }
    if (c.Reserved)
        return STATUS_NOT_IMPLEMENTED;
    if (c.Store > 3 || c.Store == 2)
        return STATUS_INVALID_PARAMETER;
    ULONG module, tables;
    status = module_of(c.Module, &module);
    if (!NT_SUCCESS(status))
        return status;
    const NSI_TABLE *t = NsiNetTable(module, c.Table, &tables);
    if (!t)
        return STATUS_INVALID_PARAMETER;
    if (c.KeySize > t->Key || (c.KeySize && !c.Key))
        return STATUS_INVALID_PARAMETER;
    if (!t->Set)
        return STATUS_ACCESS_DENIED;
    UCHAR data[64] = {}, key[80] = {};
    if (c.DataSize > sizeof(data) || (c.DataSize && !c.Data) || c.KeySize > sizeof(key))
        return STATUS_INVALID_PARAMETER;
    status = c.DataSize ? KiCopyFromUser(data, (PVOID)(ULONG_PTR)c.Data, c.DataSize) : STATUS_SUCCESS;
    if (NT_SUCCESS(status) && c.KeySize)
        status = KiCopyFromUser(key, (PVOID)(ULONG_PTR)c.Key, c.KeySize);
    if (!NT_SUCCESS(status))
        return status;
    lock();
    const NSI_NET *net = NsiNetCurrent(FALSE, module);
    status = net ? t->Set(net, module, c.Table, c.Action, key, c.KeySize, c.Type, data, c.DataSize, c.DataOffset)
                 : STATUS_INSUFFICIENT_RESOURCES;
    unlock();
    return status;
}

/* ------------------------------------------------------------------ change notifications */

/*
 * a request that stays pending until the table changes, then completes with success and nothing else: the caller
 * reads the table again. NotifyAddrChange and NotifyRouteChange are this on the unicast and the route table.
 */
static NTSTATUS request_change(PFILE_OBJECT file, HANDLE event, PVOID apc, PVOID context, PIO_STATUS_BLOCK iosb,
                               PVOID request, ULONG in_len)
{
    UCHAR r[sizeof(NSI_REQUEST_CHANGE)];
    ULONG length = narrow() ? 0x14 : sizeof(r);
    if (!request || in_len < length)
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = KiCopyFromUser(r, request, length);
    if (!NT_SUCCESS(status))
        return status;
    ULONG module;
    status = module_of(field(r, 0x08, 0x04, TRUE), &module);
    if (!NT_SUCCESS(status))
        return status;
    ULONG table = (ULONG)field(r, 0x10, 0x08, FALSE), tables;
    if (!NsiNetTable(module, table, &tables))
        return STATUS_INVALID_PARAMETER;
    NSI_WAITER *w = zeroed(sizeof(*w));
    if (!w)
        return STATUS_INSUFFICIENT_RESOURCES;
    IO_USER_REQUEST *u = IopNewUserRequest(file, event, apc, context, iosb, nullptr, 0, &status);
    if (!u) {
        ExFreePool(w);
        return status;
    }
    w->Request = u;
    w->Module = module;
    w->Table = table;
    lock();
    w->Signature = signature(NsiNetCurrent(FALSE, module), module, table);
    InsertTailList(&g_waiters, &w->Links);
    unlock();
    KeSetEvent(&g_wake, IO_NO_INCREMENT, FALSE);
    return IopPendUserRequest(u);
}

/* completes the waiters a predicate picks; the lock is held. */
static ULONG complete_waiters(PFILE_OBJECT file, PETHREAD thread, PVOID iosb, PVOID context, BOOLEAN by_context,
                              NTSTATUS status)
{
    ULONG count = 0;
    for (PLIST_ENTRY e = g_waiters.Flink; e != &g_waiters;) {
        NSI_WAITER *w = CONTAINING_RECORD(e, NSI_WAITER, Links);
        e = e->Flink;
        IO_USER_REQUEST *u = w->Request;
        if ((file && u->File != file) || (thread && u->Thread != thread) || (iosb && u->Iosb != iosb) ||
            (by_context && u->ApcContext != context))
            continue;
        RemoveEntryList(&w->Links);
        IopCompleteUserRequest(u, status, 0);
        ExFreePool(w);
        count++;
    }
    return count;
}

/* the request is named by the address of its OVERLAPPED, the apc context it was made with. */
static NTSTATUS cancel_change(PVOID request, ULONG in_len)
{
    ULONG64 value = 0;
    ULONG length = narrow() ? 4 : 8;
    if (!request || in_len < length)
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = KiCopyFromUser(&value, request, length);
    if (!NT_SUCCESS(status))
        return status;
    if (!value)
        return STATUS_NSI_NOT_FOUND;
    lock();
    ULONG count = complete_waiters(nullptr, nullptr, nullptr, (PVOID)(ULONG_PTR)value, TRUE, STATUS_NSI_CANCELLED);
    unlock();
    return count ? STATUS_SUCCESS : STATUS_NSI_NOT_FOUND;
}

NTSTATUS NsiCancel(PFILE_OBJECT file, PETHREAD thread, PVOID iosb)
{
    lock();
    ULONG count = complete_waiters(file, thread, iosb, nullptr, FALSE, STATUS_NSI_CANCELLED);
    unlock();
    return count ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

/* the connectivity of the machine, published like the network list manager's when it changes. */
static void publish_connectivity(const NSI_NET *net)
{
    ULONG64 state = NsiNetConnectivity(net);
    if (state == g_connectivity)
        return;
    g_connectivity = state;
    WNF_STATE_NAME name;
    ULONG64 value = WNF_NLM_GLOBAL_CONNECTIVITY_STATE;
    memcpy(&name, &value, sizeof(name));
    NtUpdateWnfStateData(&name, &state, sizeof(state), nullptr, nullptr, 0, 0);
}

/* reads the host's network again every second while a thread waits for a change, else now and then. */
static void NTAPI worker(PVOID context)
{
    for (;;) {
        lock();
        BOOLEAN waiting = !IsListEmpty(&g_waiters);
        const NSI_NET *net = NsiNetCurrent(TRUE, NSI_MODULE_NDIS);
        if (net) {
            publish_connectivity(net);
            for (PLIST_ENTRY e = g_waiters.Flink; e != &g_waiters;) {
                NSI_WAITER *w = CONTAINING_RECORD(e, NSI_WAITER, Links);
                e = e->Flink;
                if (signature(NsiNetCurrent(FALSE, w->Module), w->Module, w->Table) == w->Signature)
                    continue;
                RemoveEntryList(&w->Links);
                IopCompleteUserRequest(w->Request, STATUS_SUCCESS, 0);
                ExFreePool(w);
            }
        }
        unlock();
        LARGE_INTEGER timeout;
        timeout.QuadPart = waiting ? -10000000ll : -50000000ll;
        KeWaitForSingleObject(&g_wake, Executive, KernelMode, FALSE, &timeout);
    }
}

/* ------------------------------------------------------------------ the device */

BOOLEAN IopIsNsi(PFILE_OBJECT file)
{
    return g_device && file->DeviceObject == g_device;
}

NTSTATUS NsiDeviceControl(PFILE_OBJECT file, HANDLE event, PVOID apc, PVOID context, PIO_STATUS_BLOCK iosb,
                          ULONG code, PVOID input, ULONG in_len, PVOID output, ULONG out_len)
{
    if (code == IOCTL_NSI_REQUEST_CHANGE)
        return request_change(file, event, apc, context, iosb, input, in_len);
    NTSTATUS status;
    ULONG_PTR information = 0;
    switch (code) {
    case IOCTL_NSI_ENUMERATE_ALL:
        status = enumerate(input, in_len, &information);
        break;
    case IOCTL_NSI_GET_ALL_PARAMETERS:
        status = get_all(input, in_len, &information);
        break;
    case IOCTL_NSI_GET_PARAMETER:
        status = get_parameter(input, in_len, &information);
        break;
    case IOCTL_NSI_SET_PARAMETER:
        status = set(input, in_len, FALSE);
        break;
    case IOCTL_NSI_SET_ALL_PARAMETERS:
        status = set(input, in_len, TRUE);
        break;
    case IOCTL_NSI_CANCEL_CHANGE:
        status = cancel_change(input, in_len);
        break;
    case IOCTL_NSI_MIME_OBJECTS:
        status = list_objects(input, in_len, output, out_len, &information);
        break;
    case IOCTL_NSI_REGISTER_CHANGE:
    case IOCTL_NSI_DEREGISTER_CHANGE:
        /* the registrations of the nsi service of windows, which netio calls back by rpc. */
        status = STATUS_ACCESS_DENIED;
        break;
    case IOCTL_NSI_ENUMERATE_PERSISTENT:
    case IOCTL_NSI_GET_PERSISTENT:
        status = STATUS_NSI_NOT_FOUND;
        break;
    case IOCTL_NSI_SET_PERSISTENT:
        status = STATUS_ACCESS_DENIED;
        break;
    default:
        status = STATUS_NOT_IMPLEMENTED;
        break;
    }
    NTSTATUS created;
    IO_USER_REQUEST *r = IopNewUserRequest(file, event, apc, context, iosb, nullptr, 0, &created);
    if (!r)
        return created;
    return IopImmediateUserRequest(r, status, information);
}

static NTSTATUS complete_irp(PIRP irp, NTSTATUS status)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = 0;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS NTAPI nsi_create(PDEVICE_OBJECT device, PIRP irp)
{
    return complete_irp(irp, STATUS_SUCCESS);
}

/* the last handle of a file closed: its waits end. */
static NTSTATUS NTAPI nsi_cleanup(PDEVICE_OBJECT device, PIRP irp)
{
    lock();
    complete_waiters(IoGetCurrentIrpStackLocation(irp)->FileObject, nullptr, nullptr, nullptr, FALSE,
                     STATUS_NSI_CANCELLED);
    unlock();
    return complete_irp(irp, STATUS_SUCCESS);
}

static NTSTATUS NTAPI NsiDriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    driver->MajorFunction[IRP_MJ_CREATE] = nsi_create;
    driver->MajorFunction[IRP_MJ_CLEANUP] = nsi_cleanup;
    driver->MajorFunction[IRP_MJ_CLOSE] = nsi_create;
    UNICODE_STRING name = RTL_CONSTANT_STRING(MIME_NSI_DEVICE);
    return IoCreateDevice(driver, 0, &name, FILE_DEVICE_NETWORK, 0, FALSE, &g_device);
}

void IopInitializeNsi(void)
{
    KeInitializeMutex(&g_lock, 0);
    KeInitializeEvent(&g_wake, SynchronizationEvent, FALSE);
    InitializeListHead(&g_waiters);
    NsiNetInitialize();
    NTSTATUS status = IoCreateDriver(L"\\Driver\\nsiproxy", NsiDriverEntry, nullptr);
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("nsi failed to start: %08x", status);
        return;
    }
    status = PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, worker, nullptr);
    if (!NT_SUCCESS(status))
        KiLogPrintf("nsi has no worker: %08x", status);
}
