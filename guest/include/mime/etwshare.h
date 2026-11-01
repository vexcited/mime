/*
 * the table of event tracing sessions that the processes of the machine share. windows keeps sessions in the kernel,
 * with loggers that fill buffers and provider registrations that the kernel enables. mime keeps them in a named
 * section: advapi32 starts, controls and reads sessions, ntdll checks the table when a provider asks whether it is
 * enabled and writes an event to every session that enables its provider.
 *
 * a provider learns of a session when it registers and each time it asks whether it is enabled or writes an event, not
 * the moment the session enables it. the events of a session wait in a ring of 64 KB, and in the log file of a file
 * session in the same record format, behind a header of 512 bytes. the oldest events are dropped when the ring is full.
 */
#ifndef MIME_ETWSHARE_H
#define MIME_ETWSHARE_H

#include <mime/ntapi.h>

#define ETWS_MAX_SESSIONS 64
#define ETWS_MAX_PROVIDERS 32
#define ETWS_RING 0x10000
#define ETWS_MAGIC 0x53575445
#define ETWS_FILE_HEADER 512
#define ETWS_FIRST_ID 2

#define ETWS_MODE_REAL_TIME 0x100
#define ETWS_MODE_FILE_MASK 0x0000000F

typedef struct ETWS_PROVIDER {
    GUID Guid;
    ULONGLONG MatchAny;
    ULONGLONG MatchAll;
    ULONG Property;
    UCHAR Level;
    UCHAR Used;
    UCHAR Reserved[2];
} ETWS_PROVIDER;

typedef struct ETWS_SESSION {
    LONG Used;
    ULONG Id;
    ULONG ProcessId;
    ULONG BufferSize;
    GUID Guid;
    WCHAR Name[128];
    WCHAR File[260];
    ULONG MinimumBuffers;
    ULONG MaximumBuffers;
    ULONG MaximumFileSize;
    ULONG LogFileMode;
    ULONG FlushTimer;
    ULONG EnableFlags;
    ULONG ClockType;
    LONG AgeLimit;
    ULONG NumberOfBuffers;
    ULONG FreeBuffers;
    ULONG EventsLost;
    ULONG BuffersWritten;
    ULONG LogBuffersLost;
    ULONG RealTimeBuffersLost;
    ULONG LoggerThreadId;
    ULONG FileCounter;
    LONGLONG StartTime;
    ULONGLONG Write;
    ULONGLONG Floor;
    ULONGLONG FileBytes;
    ETWS_PROVIDER Providers[ETWS_MAX_PROVIDERS];
    BYTE Ring[ETWS_RING];
} ETWS_SESSION;

typedef struct ETWS_TABLE {
    ULONG Magic;
    volatile LONG Lock;
    volatile LONG Generation;
    ULONG Reserved;
    ETWS_SESSION Sessions[ETWS_MAX_SESSIONS];
} ETWS_TABLE;

typedef struct ETWS_RECORD {
    ULONG Length;
    ULONG Kind;
    GUID Provider;
    USHORT Id;
    UCHAR Version;
    UCHAR Channel;
    UCHAR Level;
    UCHAR Opcode;
    USHORT Task;
    ULONGLONG Keyword;
    ULONG ProcessId;
    ULONG ThreadId;
    LONGLONG Time;
    GUID Activity;
    GUID Related;
    ULONG PayloadSize;
    ULONG Reserved;
} ETWS_RECORD;

static ETWS_TABLE *g_etws_table;

/* maps the table, creating the section when this is the first process to ask and the caller wants one. */
static inline ETWS_TABLE *etws_table(BOOLEAN create)
{
    if (g_etws_table)
        return g_etws_table;
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES attributes;
    RtlInitUnicodeString(&name, L"\\BaseNamedObjects\\MimeEtwSessions");
    InitializeObjectAttributes(&attributes, &name, OBJ_OPENIF, nullptr, nullptr);
    LARGE_INTEGER size;
    size.QuadPart = sizeof(ETWS_TABLE);
    HANDLE section;
    if (create ? !NT_SUCCESS(NtCreateSection(&section, SECTION_ALL_ACCESS, &attributes, &size, PAGE_READWRITE, SEC_COMMIT, nullptr))
               : !NT_SUCCESS(NtOpenSection(&section, SECTION_ALL_ACCESS, &attributes)))
        return nullptr;
    PVOID base = nullptr;
    SIZE_T view = 0;
    NTSTATUS status = NtMapViewOfSection(section, NtCurrentProcess(), &base, 0, 0, nullptr, &view, ViewShare, 0, PAGE_READWRITE);
    NtClose(section);
    if (!NT_SUCCESS(status))
        return nullptr;
    ETWS_TABLE *table = base;
    __sync_val_compare_and_swap((volatile LONG *)&table->Magic, 0, ETWS_MAGIC);
    PVOID previous = __sync_val_compare_and_swap((PVOID volatile *)&g_etws_table, (PVOID)nullptr, (PVOID)table);
    if (previous) {
        NtUnmapViewOfSection(NtCurrentProcess(), base);
        return previous;
    }
    return table;
}

static inline void etws_lock(ETWS_TABLE *table)
{
    while (__sync_val_compare_and_swap(&table->Lock, 0, 1))
        NtYieldExecution();
}

static inline void etws_unlock(ETWS_TABLE *table)
{
    __sync_lock_release(&table->Lock);
}

static inline void etws_changed(ETWS_TABLE *table)
{
    __sync_fetch_and_add(&table->Generation, 1);
}

/* what the running sessions ask of a provider: enabled at all, the highest level and the union of the keywords. */
static inline BOOLEAN etws_provider_state(ETWS_TABLE *table, const GUID *guid, UCHAR *level, ULONGLONG *any, ULONGLONG *all)
{
    BOOLEAN enabled = FALSE;
    *level = 0;
    *any = 0;
    *all = 0;
    etws_lock(table);
    for (ULONG s = 0; s < ETWS_MAX_SESSIONS; s++) {
        ETWS_SESSION *session = &table->Sessions[s];
        if (!session->Used)
            continue;
        for (ULONG p = 0; p < ETWS_MAX_PROVIDERS; p++) {
            ETWS_PROVIDER *provider = &session->Providers[p];
            if (!provider->Used || memcmp(&provider->Guid, guid, sizeof(GUID)))
                continue;
            if (!enabled || provider->Level > *level)
                *level = provider->Level;
            *any |= provider->MatchAny;
            *all = enabled ? (*all & provider->MatchAll) : provider->MatchAll;
            enabled = TRUE;
        }
    }
    etws_unlock(table);
    return enabled;
}

/* whether an event passes the level and the keywords a provider is enabled with. */
static inline BOOLEAN etws_passes(UCHAR level, ULONGLONG keyword, UCHAR enabled_level, ULONGLONG any, ULONGLONG all)
{
    if (level && enabled_level && level > enabled_level)
        return FALSE;
    if (keyword && ((any && !(keyword & any)) || (keyword & all) != all))
        return FALSE;
    return TRUE;
}

#endif
