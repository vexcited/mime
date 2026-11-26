#include "../ntos.h"

typedef struct API_SET_RULE {
    PCWSTR Prefix;
    PCWSTR Host;
    ULONG Version;
} API_SET_RULE;

static const API_SET_RULE g_api_sets[] = {
#include "../../ntdll/apiset.h"
};

typedef struct API_SET_NAMESPACE {
    ULONG Version, Size, Flags, Count, EntryOffset, HashOffset, HashFactor;
} API_SET_NAMESPACE;

typedef struct API_SET_NAMESPACE_ENTRY {
    ULONG Flags, NameOffset, NameLength, HashedLength, ValueOffset, ValueCount;
} API_SET_NAMESPACE_ENTRY;

typedef struct API_SET_VALUE_ENTRY {
    ULONG Flags, NameOffset, NameLength, ValueOffset, ValueLength;
} API_SET_VALUE_ENTRY;

typedef struct API_SET_HASH_ENTRY {
    ULONG Hash, Index;
} API_SET_HASH_ENTRY;

#define API_SET_HASH_FACTOR 31
#define API_SET_COUNT RTL_NUMBER_OF(g_api_sets)

static FAST_MUTEX g_lock;
static PVOID g_api_set_section, g_leap_section, g_csr_section[2];
static SIZE_T g_api_set_size;
static ULONG64 g_csr_server_base[2];
static PUCHAR g_csr_base[2];

void PspInitializePebBlocks(void)
{
    ExInitializeFastMutex(&g_lock);
}

static ULONG chars_of(PCWSTR text)
{
    ULONG n = 0;
    while (text[n])
        n++;
    return n;
}

/* the contract with its highest version, as the schema names it: api-ms-win-core-heap-l1-2 becomes ...-l1-2-0. */
static ULONG contract_name(const API_SET_RULE *rule, PWCHAR out)
{
    ULONG n = chars_of(rule->Prefix);
    memcpy(out, rule->Prefix, n * sizeof(WCHAR));
    out[n++] = '-';
    ULONG version = rule->Version, digits = version >= 10 ? 2 : 1;
    if (digits == 2)
        out[n++] = (WCHAR)('0' + version / 10 % 10);
    out[n++] = (WCHAR)('0' + version % 10);
    return n;
}

/*
 * the contracts whose host depends on who imports them, read from the schema of windows 11 26300: kernel32 forwards
 * these exports to the contract and gets kernelbase, everyone else gets kernel32. with one host for all, the
 * forwarder of kernel32 comes back to kernel32 and the loader gives up with c000007b.
 */
static const struct {
    PCWSTR Prefix, Importer, Host;
} g_api_set_importers[] = {
    {L"api-ms-win-core-appinit-l1-1", L"kernel32.dll", L"kernelbase.dll"},
    {L"api-ms-win-core-io-l1-1", L"kernel32.dll", L"kernelbase.dll"},
    {L"api-ms-win-core-processsecurity-l1-1", L"kernel32.dll", L"kernelbase.dll"},
    {L"api-ms-win-core-processthreads-l1-1", L"kernel32.dll", L"kernelbase.dll"},
    {L"api-ms-win-core-util-l1-1", L"kernel32.dll", L"kernelbase.dll"},
};

static BOOLEAN same_text(PCWSTR a, PCWSTR b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static LONG importer_of(const API_SET_RULE *rule)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_api_set_importers); i++)
        if (same_text(rule->Prefix, g_api_set_importers[i].Prefix))
            return (LONG)i;
    return -1;
}

static ULONG put_text(PUCHAR b, PULONG text, PCWSTR string)
{
    ULONG at = *text, bytes = chars_of(string) * sizeof(WCHAR);
    memcpy(b + at, string, bytes);
    *text += (bytes + 3) & ~3u;
    return at;
}

/*
 * a version 6 namespace: the entries sorted by name, their values, the default first and then the one of an
 * importer, then the hashes sorted, then the strings.
 */
static NTSTATUS build_api_set(void)
{
    SIZE_T strings = 0;
    for (ULONG i = 0; i < API_SET_COUNT; i++)
        strings += (chars_of(g_api_sets[i].Prefix) + 3 + chars_of(g_api_sets[i].Host)) * sizeof(WCHAR) + 4;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_api_set_importers); i++)
        strings += (chars_of(g_api_set_importers[i].Importer) + chars_of(g_api_set_importers[i].Host)) * sizeof(WCHAR) + 8;
    ULONG value_count = API_SET_COUNT + RTL_NUMBER_OF(g_api_set_importers);
    ULONG entries = sizeof(API_SET_NAMESPACE);
    ULONG values = entries + API_SET_COUNT * sizeof(API_SET_NAMESPACE_ENTRY);
    ULONG hashes = values + value_count * sizeof(API_SET_VALUE_ENTRY);
    ULONG text = hashes + API_SET_COUNT * sizeof(API_SET_HASH_ENTRY);
    SIZE_T size = ROUND_TO_PAGES(text + strings);
    PVOID base, section;
    NTSTATUS status = MmCreateSharedSection(size, &base, &section);
    if (!NT_SUCCESS(status))
        return status;
    PUCHAR b = base;
    memset(b, 0, size);
    API_SET_NAMESPACE *ns = base;
    API_SET_NAMESPACE_ENTRY *e = (PVOID)(b + entries);
    API_SET_VALUE_ENTRY *v = (PVOID)(b + values);
    API_SET_HASH_ENTRY *h = (PVOID)(b + hashes);
    ULONG value = 0;
    for (ULONG i = 0; i < API_SET_COUNT; i++) {
        ULONG name = contract_name(&g_api_sets[i], (PWCHAR)(b + text));
        ULONG hashed = chars_of(g_api_sets[i].Prefix), hash = 0;
        for (ULONG k = 0; k < hashed; k++)
            hash = hash * API_SET_HASH_FACTOR + g_api_sets[i].Prefix[k];
        LONG importer = importer_of(&g_api_sets[i]);
        e[i] = (API_SET_NAMESPACE_ENTRY){1, text, name * sizeof(WCHAR), hashed * sizeof(WCHAR),
                                         values + value * sizeof(API_SET_VALUE_ENTRY), importer < 0 ? 1u : 2u};
        text += (name * sizeof(WCHAR) + 3) & ~3u;
        ULONG host = put_text(b, &text, g_api_sets[i].Host);
        v[value++] = (API_SET_VALUE_ENTRY){0, 0, 0, host, chars_of(g_api_sets[i].Host) * sizeof(WCHAR)};
        if (importer >= 0) {
            PCWSTR who = g_api_set_importers[importer].Importer, where = g_api_set_importers[importer].Host;
            ULONG who_at = put_text(b, &text, who);
            ULONG where_at = put_text(b, &text, where);
            v[value++] = (API_SET_VALUE_ENTRY){0, who_at, chars_of(who) * sizeof(WCHAR), where_at,
                                               chars_of(where) * sizeof(WCHAR)};
        }
        /* an insertion sort keeps the hashes in order as they come. */
        ULONG at = i;
        while (at && h[at - 1].Hash > hash) {
            h[at] = h[at - 1];
            at--;
        }
        h[at] = (API_SET_HASH_ENTRY){hash, i};
    }
    *ns = (API_SET_NAMESPACE){6, text, 0, API_SET_COUNT, entries, hashes, API_SET_HASH_FACTOR};
    g_api_set_size = size;
    g_api_set_section = section;
    return STATUS_SUCCESS;
}

/*
 * the heap csrss shares with its session, as a process of windows 11 26300 reads its committed head, qword by qword,
 * in session 0 and in session 1. a pointer is an offset from the base the heap has in csrss: every process reads the
 * addresses of the server there and moves them itself, the base server's data by the address it holds of itself.
 * the heap holds the static data of the base server at 0x790: the windows and system directories, the directories of
 * the named objects, the time of the boot, the locale of the user and the tree of the ini files kept in the registry.
 */
typedef struct CSR_HEAP_CELL {
    USHORT Offset;
    BOOLEAN Pointer;
    ULONG64 Value;
} CSR_HEAP_CELL;

#include "csrheap.inc"

#define CSR_RESERVED 0x100000 /* the size of the view a client maps, of which csrss has committed the head. */
#define CSR_COMMITTED 0x5000
#define CSR_STATIC_ARRAY 0x750 /* ReadOnlyStaticServerData: one pointer for each server dll, the base server second. */
#define CSR_CSD_VERSION 0x7ca
#define CSR_TIME_OF_DAY 0x8d0
#define CSR_LOCALE_NAME 0x908
#define CSR_USER_SID 0xf3c /* session 1 only: the sid of the user whose locale the heap holds. */
#define CSR_NLS_USER_INFO CSR_LOCALE_NAME
#define CSR_NLS_USER_INFO_SIZE 0x67c

static NTSTATUS build_csr(ULONG slot, PVOID *out)
{
    PVOID base, section;
    NTSTATUS status = MmCreateReservedSection(CSR_RESERVED, CSR_COMMITTED, &base, &section);
    if (!NT_SUCCESS(status))
        return status;
    /* where the two servers of the reference had their heaps, moved by a draw. */
    g_csr_server_base[slot] = (slot ? 0x7df400000000ull : 0x7ff500000000ull) + ((MiRandom() & 0x7fff) << 16);
    ULONG64 server = g_csr_server_base[slot];
    PUCHAR b = base;
    memset(b, 0, CSR_COMMITTED);
    const CSR_HEAP_CELL *cells = slot ? g_csr_heap_1 : g_csr_heap_0;
    ULONG count = slot ? RTL_NUMBER_OF(g_csr_heap_1) : RTL_NUMBER_OF(g_csr_heap_0);
    for (ULONG i = 0; i < count; i++)
        *(PULONG64)(b + cells[i].Offset) = cells[i].Value + (cells[i].Pointer ? server : 0);
    struct {
        LARGE_INTEGER BootTime, CurrentTime, TimeZoneBias;
        ULONG TimeZoneId;
    } *time = (PVOID)(b + CSR_TIME_OF_DAY);
    time->BootTime.QuadPart = (LONGLONG)KeBootFiletime;
    KeQuerySystemTime(&time->CurrentTime);
    time->TimeZoneBias.QuadPart = (LONGLONG)SharedUserData->TimeZoneBias.LowPart |
                                  (LONGLONG)SharedUserData->TimeZoneBias.High1Time << 32;
    time->TimeZoneId = SharedUserData->TimeZoneId;
    if (slot) {
        PULONG sid = (PULONG)(b + CSR_USER_SID);
        sid[3] = HalBootInfo->user_sid[0];
        sid[4] = HalBootInfo->user_sid[1];
        sid[5] = HalBootInfo->user_sid[2];
        sid[6] = 1001;
        if (HalBootInfo->locale_lcid && HalBootInfo->locale_lcid != 0x0409)
            memset(b + CSR_LOCALE_NAME, 0, 6 * sizeof(WCHAR));
    }
    g_csr_base[slot] = b;
    *out = section;
    return STATUS_SUCCESS;
}

/* the section of a session, which csrss names SharedSection, and where its heap and its static data sit in the server. */
NTSTATUS PspCsrSharedSection(ULONG session, PVOID *section, ULONG64 *server, ULONG64 *static_data)
{
    ULONG slot = session ? 1 : 0;
    NTSTATUS status = STATUS_SUCCESS;
    ExAcquireFastMutex(&g_lock);
    if (!g_csr_section[slot])
        status = build_csr(slot, &g_csr_section[slot]);
    ExReleaseFastMutex(&g_lock);
    if (!NT_SUCCESS(status))
        return status;
    *section = g_csr_section[slot];
    *server = g_csr_server_base[slot];
    *static_data = g_csr_server_base[slot] + CSR_STATIC_ARRAY;
    return STATUS_SUCCESS;
}

/*
 * the locale settings of the user, the NLS_USER_INFO kernelbase asks csrss for with request 0x1001b when the count
 * at its end differs from the one in the shared heap. the answer is the copy the heap holds, so the counts agree and
 * the process keeps its cache.
 */
NTSTATUS PspCsrNlsUserInfo(ULONG session, PVOID buffer, ULONG length)
{
    PVOID section;
    ULONG64 server, static_data;
    ULONG slot = session ? 1 : 0;
    if (length != CSR_NLS_USER_INFO_SIZE)
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = PspCsrSharedSection(session, &section, &server, &static_data);
    if (NT_SUCCESS(status))
        memcpy(buffer, g_csr_base[slot] + CSR_NLS_USER_INFO, length);
    return status;
}

static NTSTATUS build_leap(void)
{
    PVOID base;
    NTSTATUS status = MmCreateSharedSection(PAGE_SIZE, &base, &g_leap_section);
    if (!NT_SUCCESS(status))
        return status;
    memset(base, 0, PAGE_SIZE);
    *(PULONG)base = 1; /* enabled, and no leap second in the list. */
    return STATUS_SUCCESS;
}

static NTSTATUS map_read_only(PVOID section, PEPROCESS process, SIZE_T size, ULONG type, PVOID *base)
{
    *base = nullptr;
    return MiMapViewOfSection(section, process, base, 0, &size, PAGE_READONLY, type);
}

/*
 * what the peb of a 64-bit process points at on windows 11 26300 besides its heap and its modules, read from a
 * process there: the api set map and the heap csrss shares with its session, both read-only views, the leap second
 * page, a read-only view too, and the shim data, a private block of 0x2000 bytes. called in the address space of
 * the new process, before its first thread.
 */
NTSTATUS PspMapPebBlocks(PEPROCESS process, PPEB peb)
{
    ULONG session = process->SessionId ? 1 : 0;
    NTSTATUS status = STATUS_SUCCESS;
    ExAcquireFastMutex(&g_lock);
    if (!g_api_set_section)
        status = build_api_set();
    if (NT_SUCCESS(status) && !g_leap_section)
        status = build_leap();
    if (NT_SUCCESS(status) && !g_csr_section[session])
        status = build_csr(session, &g_csr_section[session]);
    ExReleaseFastMutex(&g_lock);
    if (!NT_SUCCESS(status))
        return status;

    /*
     * a 64-bit process maps the heap of csrss itself when it connects to the api port, see ex/csr.c. a 32-bit one
     * does not connect yet, so it still finds the view made.
     */
    PVOID api_set, csr = nullptr, leap, shim = nullptr;
    status = map_read_only(g_api_set_section, process, g_api_set_size, 0, &api_set);
    if (NT_SUCCESS(status) && PspIsWow64Process(process))
        status = map_read_only(g_csr_section[session], process, CSR_COMMITTED, MEM_TOP_DOWN, &csr);
    if (NT_SUCCESS(status))
        status = map_read_only(g_leap_section, process, PAGE_SIZE, MEM_TOP_DOWN, &leap);
    SIZE_T shim_size = 0x2000;
    if (NT_SUCCESS(status))
        status = MiAllocateVirtual(process, &shim, &shim_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!NT_SUCCESS(status))
        return status;

    peb->ApiSetMap = api_set;
    if (csr) {
        peb->ReadOnlySharedMemoryBase = csr;
        peb->ReadOnlyStaticServerData = (PVOID *)((PUCHAR)csr + CSR_STATIC_ARRAY);
        peb->CsrServerReadOnlySharedMemoryBase = g_csr_server_base[session];
        peb->CSDVersion.Length = 0;
        peb->CSDVersion.MaximumLength = sizeof(WCHAR);
        peb->CSDVersion.Buffer = (PWSTR)((PUCHAR)csr + CSR_CSD_VERSION);
    }
    peb->LeapSecondData = leap;
    /*
     * the header of the shim data the creator leaves: its size, its magic, its version and the machine. measured on
     * windows 11, a wow64 child gets version 0 and the x86 machine.
     */
    PULONG header = (PULONG)((PUCHAR)shim + 0x208);
    header[0] = 0x11c0;
    header[1] = 0xac0dedab;
    header[2] = PspIsWow64Process(process) ? 0 : 1;
    header[3] = PspIsWow64Process(process) ? IMAGE_FILE_MACHINE_I386 : MIME_NATIVE_MACHINE;
    peb->pShimData = shim;
    return STATUS_SUCCESS;
}
