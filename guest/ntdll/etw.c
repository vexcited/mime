/*
 * user mode event tracing: providers register and write, but nothing enables them, as the machine has no
 * tracing sessions. advapi32 and kernelbase forward their Event and Trace functions here, as on Windows.
 *
 * a registration handle holds its slot in the low bits and a sequence number in bits 32 to 47, renewed each
 * time the slot is reused, so a stale handle can be told from a live one.
 */
#include "ntdll_private.h"
#include <mime/etwshare.h>

#define ERROR_SUCCESS 0
#define ERROR_INVALID_HANDLE 6
#define ERROR_OUTOFMEMORY 14
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_BAD_LENGTH 24
#define ERROR_NOT_SUPPORTED 50
#define ERROR_INVALID_PARAMETER 87

#define MAX_REGISTRATIONS 0x800
#define MAX_LOGGERS 64
#define TYPE_CLASSIC 2
#define TYPE_EVENT 3

typedef struct ETW_REGISTRATION {
    GUID Provider;
    PVOID Callback;
    PVOID Context;
    ULONG Type;
    USHORT Sequence;
    BOOLEAN Used;
    BOOLEAN DescriptorType;
    PVOID Traits;
    PVOID ClassicContext;
    BOOLEAN Enabled;
    UCHAR Level;
    LONG Seen;
    ULONGLONG MatchAny;
    ULONGLONG MatchAll;
} ETW_REGISTRATION;

static RTL_SRWLOCK g_lock;
static ETW_REGISTRATION *g_table;
static ULONG g_count;
static USHORT g_sequence;

static PVOID etw_alloc(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, size);
}

static void etw_free(PVOID p)
{
    if (p)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, p);
}

static ULONG fail(ULONG error)
{
    RtlSetLastWin32Error(error);
    return error;
}

/* returns a handle's live registration with the lock held, or null. */
static ETW_REGISTRATION *lookup(ULONGLONG handle)
{
    ULONG low = (ULONG)handle;
    ULONG slot = low >> 4;
    if (!(low & 1) || slot >= MAX_REGISTRATIONS || !g_table)
        return nullptr;
    ETW_REGISTRATION *r = &g_table[slot];
    return r->Used && r->Sequence == (USHORT)(handle >> 32) ? r : nullptr;
}

static ULONG register_provider(const GUID *id, ULONG type, PVOID callback, PVOID context, ULONGLONG *handle,
                               PVOID classic_context)
{
    if (!id || !handle)
        return fail(ERROR_INVALID_PARAMETER);
    *handle = 0;
    RtlAcquireSRWLockExclusive(&g_lock);
    if (!g_table) {
        g_table = etw_alloc(sizeof(ETW_REGISTRATION) * MAX_REGISTRATIONS);
        if (!g_table) {
            RtlReleaseSRWLockExclusive(&g_lock);
            return fail(ERROR_OUTOFMEMORY);
        }
    }
    ULONG slot = 0;
    while (slot < MAX_REGISTRATIONS && g_table[slot].Used)
        slot++;
    if (slot == MAX_REGISTRATIONS) {
        RtlReleaseSRWLockExclusive(&g_lock);
        return fail(ERROR_OUTOFMEMORY);
    }
    ETW_REGISTRATION *r = &g_table[slot];
    r->Provider = *id;
    r->Callback = callback;
    r->Context = context;
    r->Type = type;
    r->ClassicContext = classic_context;
    r->Traits = nullptr;
    r->DescriptorType = FALSE;
    r->Seen = -1;
    do
        r->Sequence = ++g_sequence;
    while (!r->Sequence);
    r->Used = TRUE;
    if (slot + 1 > g_count)
        g_count = slot + 1;
    *handle = ((ULONGLONG)r->Sequence << 32) | (slot << 4) | 1;
    RtlReleaseSRWLockExclusive(&g_lock);
    return ERROR_SUCCESS;
}

/* removes a registration; the classic context it carried goes back to the caller. */
static ULONG unregister(ULONGLONG handle, PVOID *classic_context)
{
    RtlAcquireSRWLockExclusive(&g_lock);
    ETW_REGISTRATION *r = lookup(handle);
    if (!r || !(USHORT)(handle >> 32)) {
        RtlReleaseSRWLockExclusive(&g_lock);
        return fail(ERROR_INVALID_HANDLE);
    }
    PVOID traits = r->Traits;
    if (classic_context)
        *classic_context = r->ClassicContext;
    RtlZeroMemory(r, sizeof(*r));
    RtlReleaseSRWLockExclusive(&g_lock);
    etw_free(traits);
    return ERROR_SUCCESS;
}

static BOOLEAN registered(ULONGLONG handle)
{
    RtlAcquireSRWLockShared(&g_lock);
    BOOLEAN found = lookup(handle) != nullptr;
    RtlReleaseSRWLockShared(&g_lock);
    return found;
}

/* ------------------------------------------------------------------ sessions */

typedef void(NTAPI *ENABLE_CALLBACK)(const GUID *source, ULONG enabled, UCHAR level, ULONGLONG any, ULONGLONG all,
                                     PVOID filter, PVOID context);

static LONGLONG g_next_open;

/* the table of sessions when one exists; a process looks again for it a few times a second. */
static ETWS_TABLE *session_table(void)
{
    if (g_etws_table)
        return g_etws_table;
    LARGE_INTEGER now;
    NtQuerySystemTime(&now);
    if (now.QuadPart < g_next_open)
        return nullptr;
    g_next_open = now.QuadPart + 2500000;
    return etws_table(FALSE);
}

/*
 * brings a registration up to date with the sessions that enable its provider, and calls its callback when the state
 * changed. the callback runs in the thread that asked, outside the lock.
 */
static void sync_registration(ULONGLONG handle, BOOLEAN force)
{
    ETWS_TABLE *table = session_table();
    if (!table)
        return;
    LONG generation = table->Generation;
    GUID guid;
    UCHAR level;
    ULONGLONG any, all;
    RtlAcquireSRWLockShared(&g_lock);
    ETW_REGISTRATION *r = lookup(handle);
    if (!r || r->Type != TYPE_EVENT || (r->Seen == generation && !force)) {
        RtlReleaseSRWLockShared(&g_lock);
        return;
    }
    guid = r->Provider;
    RtlReleaseSRWLockShared(&g_lock);
    BOOLEAN enabled = etws_provider_state(table, &guid, &level, &any, &all);
    RtlAcquireSRWLockExclusive(&g_lock);
    r = lookup(handle);
    if (!r) {
        RtlReleaseSRWLockExclusive(&g_lock);
        return;
    }
    BOOLEAN changed = r->Enabled != enabled || (enabled && (r->Level != level || r->MatchAny != any || r->MatchAll != all));
    r->Seen = generation;
    r->Enabled = enabled;
    r->Level = level;
    r->MatchAny = any;
    r->MatchAll = all;
    PVOID callback = r->Callback, context = r->Context;
    RtlReleaseSRWLockExclusive(&g_lock);
    if (changed && callback)
        ((ENABLE_CALLBACK)callback)(&guid, enabled ? 1 : 0, level, any, all, nullptr, context);
}

/* the data of an event is a list of descriptors of memory. */
typedef struct EVENT_DATA {
    ULONGLONG Pointer;
    ULONG Size;
    ULONG Reserved;
} EVENT_DATA;

static ULONG payload_size(ULONG count, const EVENT_DATA *data)
{
    ULONG size = 0;
    for (ULONG i = 0; data && i < count; i++)
        size += data[i].Size;
    return size;
}

static BYTE *build_record(const ETWS_RECORD *record, ULONG count, const EVENT_DATA *data, ULONG length)
{
    BYTE *out = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, length);
    if (!out)
        return nullptr;
    memcpy(out, record, sizeof(*record));
    ((ETWS_RECORD *)out)->Length = length;
    BYTE *payload = out + sizeof(*record);
    for (ULONG i = 0; data && i < count; i++) {
        if (data[i].Size) {
            memcpy(payload, (const void *)(ULONG_PTR)data[i].Pointer, data[i].Size);
            payload += data[i].Size;
        }
    }
    memset(payload, 0, out + length - payload);
    return out;
}

/* drops the oldest events of the ring until the new record fits. */
static void make_room(ETWS_SESSION *session, ULONG length)
{
    while (session->Write + length - session->Floor > ETWS_RING) {
        session->Floor += ((ETWS_RECORD *)&session->Ring[session->Floor % ETWS_RING])->Length;
        session->EventsLost++;
    }
}

static void append_to_file(const ETWS_SESSION *session, const BYTE *bytes, ULONG length)
{
    UNICODE_STRING nt;
    if (!RtlDosPathNameToNtPathName_U(session->File, &nt, nullptr, nullptr))
        return;
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, &nt, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    IO_STATUS_BLOCK io;
    HANDLE file;
    if (NT_SUCCESS(NtCreateFile(&file, FILE_APPEND_DATA | SYNCHRONIZE, &attributes, &io, nullptr, FILE_ATTRIBUTE_NORMAL,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                                FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0))) {
        LARGE_INTEGER end;
        end.QuadPart = -2;
        NtWriteFile(file, nullptr, nullptr, nullptr, &io, (PVOID)bytes, length, &end, nullptr);
        NtClose(file);
    }
    RtlFreeUnicodeString(&nt);
}

/* writes an event to each session that enables the provider and whose level and keywords it passes. */
static void deliver(ULONGLONG handle, const void *descriptor, const GUID *activity, const GUID *related, ULONG count,
                    const void *data)
{
    ETWS_TABLE *table = session_table();
    if (!table)
        return;
    GUID guid;
    RtlAcquireSRWLockShared(&g_lock);
    ETW_REGISTRATION *r = lookup(handle);
    if (!r) {
        RtlReleaseSRWLockShared(&g_lock);
        return;
    }
    guid = r->Provider;
    RtlReleaseSRWLockShared(&g_lock);
    ETWS_RECORD record = {};
    record.Kind = 1;
    record.Provider = guid;
    memcpy(&record.Id, descriptor, 16);
    record.ProcessId = (ULONG)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess;
    record.ThreadId = (ULONG)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueThread;
    LARGE_INTEGER now;
    NtQuerySystemTime(&now);
    record.Time = now.QuadPart;
    record.Activity = activity ? *activity : NtCurrentTeb()->ActivityId;
    if (related)
        record.Related = *related;
    record.PayloadSize = payload_size(count, data);
    ULONG length = (sizeof(record) + record.PayloadSize + 7) & ~7u;
    if (length > ETWS_RING / 2)
        return;
    BYTE *copy = build_record(&record, count, data, length);
    if (!copy)
        return;
    etws_lock(table);
    for (ULONG s = 0; s < ETWS_MAX_SESSIONS; s++) {
        ETWS_SESSION *session = &table->Sessions[s];
        if (!session->Used)
            continue;
        for (ULONG p = 0; p < ETWS_MAX_PROVIDERS; p++) {
            ETWS_PROVIDER *provider = &session->Providers[p];
            if (!provider->Used || memcmp(&provider->Guid, &guid, sizeof(GUID)))
                continue;
            if (!etws_passes(record.Level, record.Keyword, provider->Level, provider->MatchAny, provider->MatchAll))
                break;
            ULONG at = (ULONG)(session->Write % ETWS_RING);
            if (at + length > ETWS_RING) {
                ETWS_RECORD pad = {};
                pad.Length = ETWS_RING - at;
                pad.Kind = 2;
                make_room(session, pad.Length);
                memcpy(&session->Ring[at], &pad, sizeof(pad));
                session->Write += pad.Length;
                at = 0;
            }
            make_room(session, length);
            memcpy(&session->Ring[at], copy, length);
            session->Write += length;
            session->BuffersWritten++;
            if (session->File[0])
                append_to_file(session, copy, length);
            break;
        }
    }
    etws_unlock(table);
    RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, copy);
}

/* whether the provider of a registration is enabled for an event of the level and keyword. */
static BOOLEAN enabled_for(ULONGLONG handle, UCHAR level, ULONGLONG keyword)
{
    sync_registration(handle, FALSE);
    RtlAcquireSRWLockShared(&g_lock);
    ETW_REGISTRATION *r = lookup(handle);
    BOOLEAN enabled = r && r->Enabled && etws_passes(level, keyword, r->Level, r->MatchAny, r->MatchAll);
    RtlReleaseSRWLockShared(&g_lock);
    return enabled;
}

/* ------------------------------------------------------------------ providers */

NTDLL_EXPORT ULONG NTAPI EtwEventRegister(const GUID *id, PVOID callback, PVOID context, ULONGLONG *handle)
{
    /* a context without a callback has nothing to be passed to. */
    if (!callback && context)
        return ERROR_INVALID_PARAMETER;
    ULONG error = register_provider(id, TYPE_EVENT, callback, context, handle, nullptr);
    if (!error)
        sync_registration(*handle, TRUE);
    return error;
}

NTDLL_EXPORT ULONG NTAPI EtwEventUnregister(ULONGLONG handle)
{
    return unregister(handle, nullptr);
}

/* provider traits: a total size, a name, then traits that each carry their own size. */
static BOOLEAN valid_traits(const BYTE *bytes, ULONG size)
{
    if (size < 3 || size > 0xffff || *(const USHORT *)bytes != size)
        return FALSE;
    ULONG offset = 2;
    while (offset < size && bytes[offset])
        offset++;
    if (offset == size)
        return FALSE;
    offset++;
    while (offset < size) {
        USHORT length;
        if (size - offset < 3)
            return FALSE;
        memcpy(&length, bytes + offset, sizeof(length));
        if (length < 3 || length > size - offset)
            return FALSE;
        offset += length;
    }
    return TRUE;
}

NTDLL_EXPORT ULONG NTAPI EtwEventSetInformation(ULONGLONG handle, ULONG kind, const void *data, ULONG size)
{
    PVOID traits = nullptr;
    switch (kind) {
    case 0:
    case 4: /* module tracking, which takes no data. */
        if (size)
            return ERROR_INVALID_PARAMETER;
        break;
    case 2:
        if (!data || size < 3 || size > 0xffff || !valid_traits(data, size))
            return ERROR_INVALID_PARAMETER;
        traits = etw_alloc(size);
        if (!traits)
            return ERROR_NOT_ENOUGH_MEMORY;
        memcpy(traits, data, size);
        break;
    case 3:
        if (!data || size != 1 || *(const BYTE *)data > 1)
            return ERROR_INVALID_PARAMETER;
        break;
    default:
        return ERROR_NOT_SUPPORTED;
    }
    RtlAcquireSRWLockExclusive(&g_lock);
    ETW_REGISTRATION *r = lookup(handle);
    if (r && kind == 2) {
        PVOID old = r->Traits;
        r->Traits = traits;
        r->DescriptorType = TRUE;
        traits = old;
    } else if (r && kind == 3) {
        r->DescriptorType = *(const BYTE *)data;
    }
    RtlReleaseSRWLockExclusive(&g_lock);
    etw_free(traits);
    return r ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER;
}

/* a descriptor starts with its id, version, channel, level and opcode, and its keyword ends it. */
NTDLL_EXPORT BOOLEAN NTAPI EtwEventEnabled(ULONGLONG handle, const void *descriptor)
{
    if (!descriptor)
        return FALSE;
    const BYTE *d = descriptor;
    ULONGLONG keyword;
    memcpy(&keyword, d + 8, sizeof(keyword));
    return enabled_for(handle, d[4], keyword);
}

NTDLL_EXPORT BOOLEAN NTAPI EtwEventProviderEnabled(ULONGLONG handle, UCHAR level, ULONGLONG keyword)
{
    return enabled_for(handle, level, keyword);
}

/* ------------------------------------------------------------------ writing */

/* what every writer checks before it looks at the data: a descriptor, then a live handle. */
static ULONG check_write(ULONGLONG handle, const void *descriptor)
{
    if (!descriptor)
        return ERROR_INVALID_PARAMETER;
    return registered(handle) ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}

/* checks the handle and the descriptor, then delivers the event to the sessions that want it. */
static ULONG write_event(ULONGLONG handle, const void *descriptor, const GUID *activity, const GUID *related, ULONG count,
                         const void *data)
{
    ULONG error = check_write(handle, descriptor);
    if (error)
        return error;
    if (EtwEventEnabled(handle, descriptor))
        deliver(handle, descriptor, activity, related, count, data);
    return ERROR_SUCCESS;
}

NTDLL_EXPORT ULONG NTAPI EtwEventWriteTransfer(ULONGLONG handle, const void *descriptor, const GUID *activity,
                                               const GUID *related, ULONG count, const void *data)
{
    return write_event(handle, descriptor, activity, related, count, data);
}

NTDLL_EXPORT ULONG NTAPI EtwEventWrite(ULONGLONG handle, const void *descriptor, ULONG count, const void *data)
{
    return write_event(handle, descriptor, nullptr, nullptr, count, data);
}

NTDLL_EXPORT ULONG NTAPI EtwEventWriteEx(ULONGLONG handle, const void *descriptor, ULONGLONG filter, ULONG flags,
                                         const GUID *activity, const GUID *related, ULONG count, const void *data)
{
    return write_event(handle, descriptor, activity, related, count, data);
}

NTDLL_EXPORT ULONG NTAPI EtwEventWriteFull(ULONGLONG handle, const void *descriptor, USHORT group,
                                           const GUID *activity, const GUID *related, ULONG count, const void *data)
{
    return write_event(handle, descriptor, activity, related, count, data);
}

/* the handle is looked at before the string, unlike the other writers. */
NTDLL_EXPORT ULONG NTAPI EtwEventWriteString(ULONGLONG handle, UCHAR level, ULONGLONG keyword, const WCHAR *text)
{
    if (!registered(handle))
        return ERROR_INVALID_HANDLE;
    if (!text)
        return ERROR_INVALID_PARAMETER;
    if (enabled_for(handle, level, keyword)) {
        /* the text with its terminator is the data of an event whose descriptor has only the level and keyword. */
        BYTE descriptor[16] = {};
        descriptor[4] = level;
        memcpy(descriptor + 8, &keyword, sizeof(keyword));
        EVENT_DATA data = {(ULONG_PTR)text, (ULONG)((wcslen(text) + 1) * sizeof(WCHAR)), 0};
        deliver(handle, descriptor, nullptr, nullptr, 1, &data);
    }
    return ERROR_SUCCESS;
}

/*
 * a scenario starts and ends with an event that is enabled; one that is not, or a handle that is not live,
 * answers ERROR_INVALID_HANDLE.
 */
NTDLL_EXPORT ULONG NTAPI EtwEventWriteStartScenario(ULONGLONG handle, const void *descriptor, ULONG count,
                                                    const void *data)
{
    return descriptor ? ERROR_INVALID_HANDLE : ERROR_INVALID_PARAMETER;
}

NTDLL_EXPORT ULONG NTAPI EtwEventWriteEndScenario(ULONGLONG handle, const void *descriptor, ULONG count,
                                                  const void *data)
{
    return descriptor ? ERROR_INVALID_HANDLE : ERROR_INVALID_PARAMETER;
}

/* a write to a logger by handle goes to the kernel, which finds no logger. */
NTDLL_EXPORT ULONG NTAPI EtwEventWriteNoRegistration(ULONGLONG logger, const void *descriptor, ULONG count,
                                                     const void *data)
{
    if (!descriptor || !logger)
        return ERROR_INVALID_PARAMETER;
    BYTE block[0x78] = {};
    memcpy(block + 0x28, descriptor, 16);
    *(ULONG *)(block + 0x54) = count;
    *(const void **)(block + 0x58) = data;
    memcpy(block + 0x30, &NtCurrentTeb()->ActivityId, sizeof(GUID));
    NTSTATUS status = NtTraceEvent((HANDLE)logger, 0x700, sizeof(block), block);
    return NT_SUCCESS(status) ? ERROR_SUCCESS : RtlNtStatusToDosError(status);
}

/* ------------------------------------------------------------------ activity ids */

static void create_activity_id(GUID *id)
{
    static LONG counter;
    LARGE_INTEGER now;
    NtQuerySystemTime(&now);
    LONG n = __atomic_add_fetch(&counter, 1, __ATOMIC_RELAXED);
    ULONG seed = (ULONG)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess * 0x9E3779B1u ^ (ULONG)n;
    id->Data1 = now.LowPart;
    id->Data2 = (USHORT)(now.HighPart & 0xffff);
    id->Data3 = (USHORT)(0x1000 | ((now.HighPart >> 16) & 0x0fff));
    id->Data4[0] = 0x80 | (n & 0x3f);
    id->Data4[1] = (UCHAR)(n >> 6);
    for (int i = 2; i < 8; i++) {
        seed = RtlRandomEx(&seed);
        id->Data4[i] = (UCHAR)(seed >> 8);
    }
}

NTDLL_EXPORT ULONG NTAPI EtwEventActivityIdControl(ULONG code, GUID *id)
{
    if (!id)
        return ERROR_INVALID_PARAMETER;
    GUID *current = &NtCurrentTeb()->ActivityId;
    switch (code) {
    case 1: /* EVENT_ACTIVITY_CTRL_GET_ID */
        *id = *current;
        return ERROR_SUCCESS;
    case 2: /* SET_ID */
        *current = *id;
        return ERROR_SUCCESS;
    case 3: /* CREATE_ID */
        create_activity_id(id);
        return ERROR_SUCCESS;
    case 4: { /* GET_SET_ID */
        GUID old = *current;
        *current = *id;
        *id = old;
        return ERROR_SUCCESS;
    }
    case 5: /* CREATE_SET_ID */
        *id = *current;
        create_activity_id(current);
        return ERROR_SUCCESS;
    }
    return fail(ERROR_INVALID_PARAMETER);
}

/* ------------------------------------------------------------------ classic providers */

/*
 * the enable information of a classic provider is its logger handle: the logger in the low 16 bits, the level
 * in the third byte and the flags in the upper half. only the logger numbers a session could have are valid.
 */
static BOOLEAN logger_valid(ULONGLONG handle)
{
    USHORT logger = (USHORT)handle;
    return handle && (logger < MAX_LOGGERS || logger == 0xffff);
}

NTDLL_EXPORT UCHAR NTAPI EtwGetTraceEnableLevel(ULONGLONG logger)
{
    if (!logger_valid(logger)) {
        RtlSetLastWin32Error(ERROR_INVALID_HANDLE);
        return 0;
    }
    return (UCHAR)(logger >> 16);
}

NTDLL_EXPORT ULONG NTAPI EtwGetTraceEnableFlags(ULONGLONG logger)
{
    if (!logger_valid(logger)) {
        RtlSetLastWin32Error(ERROR_INVALID_HANDLE);
        return 0;
    }
    return (ULONG)(logger >> 32);
}

/* the request of an enable notification starts with a wnode header: its size, then the logger in the handle. */
NTDLL_EXPORT ULONGLONG NTAPI EtwGetTraceLoggerHandle(const void *request)
{
    if (!request) {
        RtlSetLastWin32Error(ERROR_INVALID_PARAMETER);
        return (ULONGLONG)-1;
    }
    if (*(const ULONG *)request < 0x30) {
        RtlSetLastWin32Error(ERROR_BAD_LENGTH);
        return (ULONGLONG)-1;
    }
    ULONGLONG handle = *(const ULONGLONG *)((const BYTE *)request + 8);
    if ((USHORT)handle >= MAX_LOGGERS && (USHORT)handle != 0xffff) {
        RtlSetLastWin32Error(ERROR_INVALID_HANDLE);
        return (ULONGLONG)-1;
    }
    return handle;
}

NTDLL_EXPORT ULONG NTAPI EtwRegisterTraceGuidsW(PVOID callback, PVOID context, const GUID *control, ULONG count,
                                                const void *guids, PCWSTR mof_path, PCWSTR mof_resource,
                                                ULONGLONG *handle)
{
    if (!callback || !handle || !control || count > 0x10000)
        return fail(ERROR_INVALID_PARAMETER);
    /* the registrations are the guids of the provider, kept for the unregistration to free. */
    SIZE_T size = (SIZE_T)count * 24 + 16;
    PVOID copy = etw_alloc(size);
    if (!copy)
        return fail(ERROR_NOT_ENOUGH_MEMORY);
    if (count && guids)
        memcpy(copy, guids, (SIZE_T)count * 24);
    ULONG error = register_provider(control, TYPE_CLASSIC, callback, context, handle, copy);
    if (error)
        etw_free(copy);
    return error;
}

NTDLL_EXPORT ULONG NTAPI EtwRegisterTraceGuidsA(PVOID callback, PVOID context, const GUID *control, ULONG count,
                                                const void *guids, PCSTR mof_path, PCSTR mof_resource,
                                                ULONGLONG *handle)
{
    return EtwRegisterTraceGuidsW(callback, context, control, count, guids, nullptr, nullptr, handle);
}

NTDLL_EXPORT ULONG NTAPI EtwUnregisterTraceGuids(ULONGLONG handle)
{
    PVOID context = nullptr;
    ULONG error = unregister(handle, &context);
    if (!error)
        etw_free(context);
    return error;
}

/* a logger with the process bit set is private, run by the process itself. */
#define PRIVATE_LOGGER 0x1000000

NTDLL_EXPORT ULONG NTAPI EtwLogTraceEvent(ULONGLONG logger, PVOID header)
{
    if (!header)
        return ERROR_INVALID_PARAMETER;
    if (logger & PRIVATE_LOGGER)
        return ERROR_INVALID_HANDLE;
    NTSTATUS status = NtTraceEvent((HANDLE)(ULONG_PTR)(USHORT)logger, 0x100, 48, header);
    return NT_SUCCESS(status) ? ERROR_SUCCESS : RtlNtStatusToDosError(status);
}

NTDLL_EXPORT ULONG NTAPI EtwTraceEventInstance(ULONGLONG logger, const void *header, const void *instance,
                                               const void *parent)
{
    if (!header || !instance || *(const USHORT *)header < 0x38 || !*(const ULONG_PTR *)instance)
        return ERROR_INVALID_PARAMETER;
    if (parent && !*(const ULONG_PTR *)parent)
        return ERROR_INVALID_PARAMETER;
    if (logger & PRIVATE_LOGGER)
        return ERROR_INVALID_HANDLE;
    NTSTATUS status = NtTraceEvent((HANDLE)(ULONG_PTR)(USHORT)logger, 0x800, 72, (PVOID)header);
    return NT_SUCCESS(status) ? ERROR_SUCCESS : RtlNtStatusToDosError(status);
}

/* message flags: bit 1 carries a guid, bit 2 a number, in front of the argument pairs. */
NTDLL_EXPORT ULONG NTAPI EtwTraceMessageVa(ULONGLONG logger, ULONG flags, const GUID *guid, USHORT number,
                                           va_list args)
{
    if (logger & PRIVATE_LOGGER)
        return ERROR_INVALID_HANDLE;
    BYTE block[40] = {};
    ULONG pairs = 0;
    va_list scan;
    va_copy(scan, args);
    for (;;) {
        PVOID pointer = va_arg(scan, PVOID);
        if (!pointer || pairs > 0x100)
            break;
        (void)va_arg(scan, SIZE_T);
        pairs++;
    }
    va_end(scan);
    *(USHORT *)(block + 4) = number;
    *(ULONG *)(block + 0x18) = flags;
    if ((flags & 4) && guid)
        *(ULONG *)(block + 6) = *(const ULONG *)guid;
    else if ((flags & 2) && guid)
        memcpy(block + 6, guid, sizeof(GUID));
    *(ULONG *)(block + 0x1c) = pairs * 16 + 8;
    NTSTATUS status = NtTraceEvent((HANDLE)logger, 0x200, sizeof(block), block);
    return NT_SUCCESS(status) ? ERROR_SUCCESS : RtlNtStatusToDosError(status);
}

NTDLL_EXPORT ULONG __cdecl EtwTraceMessage(ULONGLONG logger, ULONG flags, const GUID *guid, ULONG number, ...)
{
    va_list args;
    va_start(args, number);
    ULONG error = EtwTraceMessageVa(logger, flags, guid, number, args);
    va_end(args);
    return error;
}

/* a trace instance id: a counter that belongs to the process that made the instance info. */
NTDLL_EXPORT ULONG NTAPI EtwCreateTraceInstanceId(PVOID registration, PVOID instance)
{
    if (!registration || !instance)
        return fail(ERROR_INVALID_PARAMETER);
    *(PVOID *)instance = registration;
    ULONG *counter = registration;
    if (counter[1] != (ULONG)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess)
        return fail(ERROR_INVALID_PARAMETER);
    ULONG next;
    do
        next = __atomic_add_fetch(&counter[0], 1, __ATOMIC_RELAXED);
    while (!next);
    *(ULONG *)((BYTE *)instance + 8) = next;
    return ERROR_SUCCESS;
}
