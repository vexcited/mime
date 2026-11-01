/*
 * pdh: queries over the performance data of the machine. a counter names an object, an instance and a counter of
 * the global block of the performance registry, and each collection reads the block once for all the counters of its
 * query. logs, other machines and the dialogs are not supported, see logs.c.
 */
#include "pdh_private.h"

#define QUERY_MAGIC 0x59524551
#define COUNTER_MAGIC 0x544E4F43
#define PDH_CVERSION 0x0503

/* the compiler references this symbol from code that uses floating point. */
int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

PDH_FUNCTION PdhFormatFromRawValue(DWORD type, DWORD format, LONGLONG *time_base, PPDH_RAW_COUNTER current,
                                   PPDH_RAW_COUNTER previous, PPDH_FMT_COUNTERVALUE value);

typedef struct ITEM {
    WCHAR Name[128];
    LONG UniqueId;
    PDH_RAW_COUNTER Raw[2];
    DWORD Have;
    BOOL Seen;
} ITEM;

typedef struct COUNTER {
    DWORD magic;
    struct QUERY *query;
    struct COUNTER *next;
    WCHAR *FullPath;
    PDH_PATH *Parsed;
    BOOL Wildcard;
    DWORD ObjectIndex;
    DWORD CounterIndex;
    DWORD Type;
    LONG Scale;
    LONG DefaultScale;
    DWORD Status;
    LONGLONG Frequency;
    DWORD_PTR user;
    ITEM *Items;
    DWORD ItemCount;
    DWORD ItemCapacity;
    DWORD Collections;
    BOOL Base;
} COUNTER;

typedef struct QUERY {
    DWORD magic;
    struct QUERY *next;
    DWORD_PTR user;
    COUNTER *counters;
    FILETIME last;
    HANDLE Thread;
    HANDLE Stop;
    HANDLE Event;
    DWORD Interval;
} QUERY;

/* the open queries, so a stale or made up handle is never dereferenced. */
static SRWLOCK g_lock = SRWLOCK_INIT;
static QUERY *g_queries;

static QUERY *find_query(PDH_HQUERY handle)
{
    for (QUERY *query = g_queries; query; query = query->next)
        if (query == handle)
            return query;
    return nullptr;
}

static COUNTER *find_counter(PDH_HCOUNTER handle)
{
    for (QUERY *query = g_queries; query; query = query->next)
        for (COUNTER *counter = query->counters; counter; counter = counter->next)
            if (counter == handle)
                return counter;
    return nullptr;
}

/* ------------------------------------------------------------------ queries */

PDH_STATUS WINAPI PdhOpenLogW(LPCWSTR file, DWORD access, LPDWORD type, PDH_HQUERY query, DWORD size, LPCWSTR caption,
                              PDH_HLOG *log);

PDH_STATUS WINAPI PdhOpenQueryW(LPCWSTR source, DWORD_PTR user, PDH_HQUERY *handle)
{
    if (!handle)
        return PDH_INVALID_ARGUMENT;
    *handle = nullptr;
    if (source) {
        SIZE_T length = wcslen(source);
        if (!length || length > 1024)
            return PDH_INVALID_ARGUMENT;
        /* there is no reader for log files. */
        DWORD type = 0;
        PDH_HLOG log;
        PDH_STATUS status = PdhOpenLogW(source, 0x10004, &type, nullptr, 0, nullptr, &log);
        return status ? status : PDH_UNKNOWN_LOG_FORMAT;
    }
    QUERY *query = LocalAlloc(LMEM_ZEROINIT, sizeof(*query));
    if (!query)
        return PDH_MEMORY_ALLOCATION_FAILURE;
    query->magic = QUERY_MAGIC;
    query->user = user;
    AcquireSRWLockExclusive(&g_lock);
    query->next = g_queries;
    g_queries = query;
    ReleaseSRWLockExclusive(&g_lock);
    *handle = query;
    return ERROR_SUCCESS;
}

PDH_STATUS WINAPI PdhOpenQueryA(LPCSTR source, DWORD_PTR user, PDH_HQUERY *handle)
{
    if (!source)
        return PdhOpenQueryW(nullptr, user, handle);
    WCHAR wide[1026];
    if (!MultiByteToWideChar(CP_ACP, 0, source, -1, wide, 1026)) {
        if (handle)
            *handle = nullptr;
        return PDH_INVALID_ARGUMENT;
    }
    return PdhOpenQueryW(wide, user, handle);
}

PDH_STATUS WINAPI PdhOpenQueryH(PDH_HLOG source, DWORD_PTR user, PDH_HQUERY *handle)
{
    if (source)
        return PDH_INVALID_HANDLE;
    return PdhOpenQueryW(nullptr, user, handle);
}

static void free_counter(COUNTER *counter)
{
    counter->magic = 0;
    LocalFree(counter->Items);
    LocalFree(counter->Parsed);
    LocalFree(counter->FullPath);
    LocalFree(counter);
}

/* the collection a query does on its own once PdhCollectQueryDataEx has started it. */
static PDH_STATUS collect(PDH_HQUERY handle, FILETIME *stamp);

static DWORD WINAPI collect_thread(LPVOID parameter)
{
    QUERY *query = parameter;
    while (WaitForSingleObject(query->Stop, query->Interval) == WAIT_TIMEOUT) {
        collect(query, nullptr);
        SetEvent(query->Event);
    }
    return 0;
}

static void stop_collecting(HANDLE thread, HANDLE stop)
{
    if (!thread)
        return;
    SetEvent(stop);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    CloseHandle(stop);
}

PDH_STATUS WINAPI PdhCloseQuery(PDH_HQUERY handle)
{
    AcquireSRWLockExclusive(&g_lock);
    QUERY *query = find_query(handle);
    HANDLE thread = query ? query->Thread : nullptr, stop = query ? query->Stop : nullptr;
    if (query)
        query->Thread = nullptr;
    ReleaseSRWLockExclusive(&g_lock);
    stop_collecting(thread, stop);
    AcquireSRWLockExclusive(&g_lock);
    query = find_query(handle);
    if (!query) {
        ReleaseSRWLockExclusive(&g_lock);
        return PDH_INVALID_HANDLE;
    }
    for (COUNTER *counter = query->counters, *next; counter; counter = next) {
        next = counter->next;
        free_counter(counter);
    }
    for (QUERY **link = &g_queries; *link; link = &(*link)->next)
        if (*link == query) {
            *link = query->next;
            break;
        }
    query->magic = 0;
    LocalFree(query);
    ReleaseSRWLockExclusive(&g_lock);
    return ERROR_SUCCESS;
}

BOOL WINAPI PdhIsRealTimeQuery(PDH_HQUERY handle)
{
    AcquireSRWLockShared(&g_lock);
    BOOL found = find_query(handle) != nullptr;
    ReleaseSRWLockShared(&g_lock);
    return found;
}

/* ------------------------------------------------------------------ counters */

/* what a path names in the data: the object, the counter and whether the path holds a wildcard. */
static PDH_STATUS resolve(const PDH_SNAPSHOT *snapshot, const PDH_PATH *path, DWORD *object_index, const PERF_OBJECT_ **object,
                          const PERF_COUNTER_ **counter)
{
    if (!PdhiIsLocalMachine(path->Machine))
        return PDH_CSTATUS_NO_MACHINE;
    DWORD index = PdhiIndexOfName(path->Object);
    *object = index ? PdhiFindObject(snapshot, index) : nullptr;
    if (!*object)
        return PDH_CSTATUS_NO_OBJECT;
    *object_index = index;
    DWORD counter_index = PdhiIndexOfCounterName(*object, path->Counter);
    *counter = counter_index ? PdhiFindCounter(*object, counter_index) : nullptr;
    if (!*counter)
        return PDH_CSTATUS_NO_COUNTER;
    return ERROR_SUCCESS;
}

/* the instance of the path in the object, by name and number of the duplicate; -1 when it is not there. */
static LONG find_instance(const PERF_OBJECT_ *object, const PDH_PATH *path)
{
    if (object->NumInstances == PERF_NO_INSTANCES_)
        return path->Instance ? -1 : 0;
    if (!path->Instance)
        return -1;
    DWORD wanted = path->Index == PDH_INDEX_NONE ? 0 : path->Index;
    DWORD seen = 0;
    for (LONG i = 0; i < object->NumInstances; i++) {
        const BYTE *block;
        const PERF_INSTANCE_ *instance = PdhiInstanceAt(object, i, &block);
        if (_wcsicmp(PdhiInstanceName(instance), path->Instance))
            continue;
        if (seen++ == wanted)
            return i;
    }
    return -1;
}

static BOOL item_for(COUNTER *counter, const WCHAR *name, ITEM **found)
{
    for (DWORD i = 0; i < counter->ItemCount; i++) {
        if (!_wcsicmp(counter->Items[i].Name, name)) {
            *found = &counter->Items[i];
            return TRUE;
        }
    }
    if (counter->ItemCount == counter->ItemCapacity) {
        DWORD capacity = counter->ItemCapacity ? counter->ItemCapacity * 2 : 4;
        ITEM *grown = LocalAlloc(LMEM_ZEROINIT, capacity * sizeof(ITEM));
        if (!grown)
            return FALSE;
        if (counter->Items) {
            memcpy(grown, counter->Items, counter->ItemCount * sizeof(ITEM));
            LocalFree(counter->Items);
        }
        counter->Items = grown;
        counter->ItemCapacity = capacity;
    }
    ITEM *item = &counter->Items[counter->ItemCount++];
    memset(item, 0, sizeof(*item));
    lstrcpynW(item->Name, name, 128);
    *found = item;
    return TRUE;
}

static ULONGLONG value_at(const BYTE *block, const PERF_COUNTER_ *counter)
{
    const BYTE *at = block + counter->CounterOffset;
    return counter->CounterSize == 8 ? *(const ULONGLONG *)at : *(const DWORD *)at;
}

/* the second value of a sample: the base counter behind a fraction, else the time the sample was taken. */
static LONGLONG second_value(const PDH_SNAPSHOT *snapshot, const PERF_OBJECT_ *object, const PERF_COUNTER_ *counter,
                             const BYTE *block)
{
    DWORD type = counter->CounterType, kind = type & PERF_TYPE_MASK_;
    if (kind == 0)
        return 0;
    if (kind == 0x400 && (type & PERF_SUBTYPE_MASK_) == PERF_FRACTION_) {
        for (DWORD i = 0; i < object->NumCounters; i++) {
            if (PdhiCounterAt(object, i) != counter)
                continue;
            const PERF_COUNTER_ *base = PdhiCounterAt(object, i + 1);
            if (base && (base->CounterType & PERF_SUBTYPE_MASK_) == PERF_COUNTER_BASE_)
                return (LONGLONG)value_at(block, base);
        }
        return 0;
    }
    const PERF_BLOCK_ *global = PdhiBlock(snapshot);
    if (type & PERF_TIMER_100NS_)
        return global->PerfTime100nSec.QuadPart;
    if (type & PERF_OBJECT_TIMER_)
        return object->PerfTime.QuadPart;
    return global->PerfTime.QuadPart;
}

static void store_sample(COUNTER *counter, ITEM *item, const PDH_SNAPSHOT *snapshot, const PERF_OBJECT_ *object,
                         const PERF_COUNTER_ *definition, const BYTE *block)
{
    FILETIME stamp;
    SystemTimeToFileTime(&PdhiBlock(snapshot)->SystemTime, &stamp);
    item->Raw[0] = item->Raw[1];
    memset(&item->Raw[1], 0, sizeof(item->Raw[1]));
    item->Raw[1].CStatus = item->Have ? PDH_CSTATUS_VALID_DATA : PDH_CSTATUS_NEW_DATA;
    item->Raw[1].TimeStamp = stamp;
    item->Raw[1].FirstValue = (LONGLONG)value_at(block, definition);
    item->Raw[1].SecondValue = second_value(snapshot, object, definition, block);
    item->Raw[1].MultiCount = 1;
    if (item->Have < 2)
        item->Have++;
    item->Seen = TRUE;
    counter->Type = definition->CounterType;
    counter->CounterIndex = definition->CounterNameTitleIndex;
    counter->DefaultScale = definition->DefaultScale;
    counter->Frequency = (definition->CounterType & PERF_TIMER_100NS_) ? 10000000 : object->PerfFreq.QuadPart;
    if (!counter->Frequency)
        counter->Frequency = PdhiBlock(snapshot)->PerfFreq.QuadPart;
}

/* reads the data of one counter out of the snapshot into its items. */
static void collect_counter(COUNTER *counter, const PDH_SNAPSHOT *snapshot)
{
    const PERF_OBJECT_ *object;
    const PERF_COUNTER_ *definition;
    DWORD object_index;
    counter->Status = resolve(snapshot, counter->Parsed, &object_index, &object, &definition);
    for (DWORD i = 0; i < counter->ItemCount; i++)
        counter->Items[i].Seen = FALSE;
    if (counter->Status)
        return;
    counter->Collections++;
    if (object->NumInstances == PERF_NO_INSTANCES_) {
        const BYTE *block;
        PdhiInstanceAt(object, 0, &block);
        ITEM *item;
        if (!counter->Parsed->Instance && item_for(counter, L"", &item))
            store_sample(counter, item, snapshot, object, definition, block);
        else
            counter->Status = PDH_CSTATUS_NO_INSTANCE;
    } else if (!counter->Wildcard) {
        LONG at = find_instance(object, counter->Parsed);
        if (at < 0) {
            counter->Status = PDH_CSTATUS_NO_INSTANCE;
        } else {
            const BYTE *block;
            WCHAR name[128];
            PdhiInstanceAt(object, at, &block);
            DWORD duplicate;
            PdhiInstanceDisplayName(object, at, name, &duplicate);
            if (duplicate)
                wsprintfW(name + wcslen(name), L"#%lu", duplicate);
            ITEM *item;
            if (item_for(counter, name, &item))
                store_sample(counter, item, snapshot, object, definition, block);
        }
    } else {
        for (LONG i = 0; i < object->NumInstances; i++) {
            const BYTE *block;
            WCHAR name[140];
            DWORD duplicate;
            PdhiInstanceAt(object, i, &block);
            PdhiInstanceDisplayName(object, i, name, &duplicate);
            if (duplicate)
                wsprintfW(name + wcslen(name), L"#%lu", duplicate);
            WCHAR shown[140];
            lstrcpynW(shown, name, 140);
            if (counter->Parsed->Instance && !PdhiWildcardMatch(counter->Parsed->Instance, shown))
                continue;
            ITEM *item;
            if (item_for(counter, name, &item))
                store_sample(counter, item, snapshot, object, definition, block);
        }
    }
    /* the instances that went away are not reported. */
    DWORD kept = 0;
    for (DWORD i = 0; i < counter->ItemCount; i++)
        if (counter->Items[i].Seen)
            counter->Items[kept++] = counter->Items[i];
    counter->ItemCount = kept;
}

PDH_STATUS WINAPI PdhValidatePathExW(PVOID source, LPCWSTR path);

/* the counter the path names, as it is now; a path that cannot be a counter says why. */
static PDH_STATUS validate_path(LPCWSTR path, PDH_PATH **parsed)
{
    *parsed = nullptr;
    PDH_STATUS status = PdhiParsePath(path, TRUE, parsed);
    if (status)
        return status;
    PDH_SNAPSHOT snapshot;
    DWORD object_index;
    const PERF_OBJECT_ *object;
    const PERF_COUNTER_ *counter;
    status = PdhiSnapshot(&snapshot);
    if (!status) {
        status = resolve(&snapshot, *parsed, &object_index, &object, &counter);
        if (!status && object->NumInstances == PERF_NO_INSTANCES_ && (*parsed)->Instance)
            status = PDH_CSTATUS_NO_INSTANCE;
        else if (!status && object->NumInstances != PERF_NO_INSTANCES_) {
            if (!(*parsed)->Instance)
                status = PDH_CSTATUS_NO_INSTANCE;
            else if (!PdhiHasWildcard((*parsed)->Instance) && find_instance(object, *parsed) < 0)
                status = PDH_CSTATUS_NO_INSTANCE;
        }
        PdhiFreeSnapshot(&snapshot);
    }
    if (status && status != PDH_CSTATUS_NO_INSTANCE) {
        LocalFree(*parsed);
        *parsed = nullptr;
    }
    return status;
}

PDH_STATUS WINAPI PdhValidatePathW(LPCWSTR path)
{
    PDH_PATH *parsed = nullptr;
    if (!path)
        return PDH_INVALID_ARGUMENT;
    if (!*path || wcslen(path) > 0x800)
        return PDH_INVALID_ARGUMENT;
    PDH_STATUS status = validate_path(path, &parsed);
    LocalFree(parsed);
    return status;
}

PDH_STATUS WINAPI PdhValidatePathA(LPCSTR path)
{
    WCHAR wide[2049];
    if (!path || PdhiWideOf(path, wide, 2049))
        return PDH_INVALID_ARGUMENT;
    return PdhValidatePathW(wide);
}

PDH_STATUS WINAPI PdhValidatePathExW(PVOID source, LPCWSTR path)
{
    if (source)
        return PDH_INVALID_HANDLE;
    return PdhValidatePathW(path);
}

PDH_STATUS WINAPI PdhValidatePathExA(PVOID source, LPCSTR path)
{
    if (source)
        return PDH_INVALID_HANDLE;
    return PdhValidatePathA(path);
}

PDH_STATUS WINAPI PdhAddCounterW(PDH_HQUERY handle, LPCWSTR path, DWORD_PTR user, PDH_HCOUNTER *out)
{
    if (!out || !path)
        return PDH_INVALID_ARGUMENT;
    *out = nullptr;
    SIZE_T length = wcslen(path);
    if (!length || length > 0x800)
        return PDH_INVALID_ARGUMENT;
    PDH_PATH *parsed = nullptr;
    PDH_STATUS status = validate_path(path, &parsed);
    if (status && status != PDH_CSTATUS_NO_INSTANCE)
        return status;
    COUNTER *counter = LocalAlloc(LMEM_ZEROINIT, sizeof(*counter));
    if (!counter) {
        LocalFree(parsed);
        return PDH_MEMORY_ALLOCATION_FAILURE;
    }
    counter->magic = COUNTER_MAGIC;
    counter->Parsed = parsed;
    counter->FullPath = LocalAlloc(LMEM_ZEROINIT, (length + 1) * sizeof(WCHAR));
    if (counter->FullPath)
        memcpy(counter->FullPath, path, length * sizeof(WCHAR));
    counter->Wildcard = PdhiHasWildcard(parsed->Instance);
    counter->user = user;
    counter->Status = status;
    counter->Scale = INT32_MIN;
    AcquireSRWLockExclusive(&g_lock);
    QUERY *query = find_query(handle);
    if (!query || !counter->FullPath) {
        ReleaseSRWLockExclusive(&g_lock);
        free_counter(counter);
        return query ? PDH_MEMORY_ALLOCATION_FAILURE : PDH_INVALID_HANDLE;
    }
    counter->query = query;
    counter->next = query->counters;
    query->counters = counter;
    *out = counter;
    ReleaseSRWLockExclusive(&g_lock);
    return status;
}

/* the counters are not localized, so the English names are the names. */
PDH_STATUS WINAPI PdhAddEnglishCounterW(PDH_HQUERY handle, LPCWSTR path, DWORD_PTR user, PDH_HCOUNTER *out)
{
    return PdhAddCounterW(handle, path, user, out);
}

static PDH_STATUS add_ansi(PDH_HQUERY handle, LPCSTR path, DWORD_PTR user, PDH_HCOUNTER *out)
{
    WCHAR wide[0x801];
    if (!out || !path)
        return PDH_INVALID_ARGUMENT;
    *out = nullptr;
    if (PdhiWideOf(path, wide, 0x801))
        return PDH_INVALID_ARGUMENT;
    return PdhAddCounterW(handle, wide, user, out);
}

PDH_STATUS WINAPI PdhAddCounterA(PDH_HQUERY handle, LPCSTR path, DWORD_PTR user, PDH_HCOUNTER *out)
{
    return add_ansi(handle, path, user, out);
}

PDH_STATUS WINAPI PdhAddEnglishCounterA(PDH_HQUERY handle, LPCSTR path, DWORD_PTR user, PDH_HCOUNTER *out)
{
    return add_ansi(handle, path, user, out);
}

PDH_STATUS WINAPI PdhRemoveCounter(PDH_HCOUNTER handle)
{
    AcquireSRWLockExclusive(&g_lock);
    COUNTER *counter = find_counter(handle);
    if (!counter) {
        ReleaseSRWLockExclusive(&g_lock);
        return PDH_INVALID_HANDLE;
    }
    for (COUNTER **link = &counter->query->counters; *link; link = &(*link)->next)
        if (*link == counter) {
            *link = counter->next;
            break;
        }
    free_counter(counter);
    ReleaseSRWLockExclusive(&g_lock);
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ collection */

static PDH_STATUS collect(PDH_HQUERY handle, FILETIME *stamp)
{
    PDH_SNAPSHOT snapshot;
    PDH_STATUS status = PdhiSnapshot(&snapshot);
    if (status)
        return status;
    AcquireSRWLockExclusive(&g_lock);
    QUERY *query = find_query(handle);
    status = query ? (query->counters ? ERROR_SUCCESS : PDH_NO_DATA) : PDH_INVALID_HANDLE;
    for (COUNTER *counter = query ? query->counters : nullptr; counter; counter = counter->next)
        collect_counter(counter, &snapshot);
    if (query && stamp)
        SystemTimeToFileTime(&PdhiBlock(&snapshot)->SystemTime, stamp);
    ReleaseSRWLockExclusive(&g_lock);
    PdhiFreeSnapshot(&snapshot);
    return status;
}

PDH_STATUS WINAPI PdhCollectQueryData(PDH_HQUERY handle)
{
    return collect(handle, nullptr);
}

PDH_STATUS WINAPI PdhCollectQueryDataWithTime(PDH_HQUERY handle, LONGLONG *timestamp)
{
    if (!timestamp)
        return PDH_INVALID_ARGUMENT;
    FILETIME stamp;
    PDH_STATUS status = collect(handle, &stamp);
    if (!status) {
        FileTimeToLocalFileTime(&stamp, &stamp);
        *timestamp = (LONGLONG)stamp.dwHighDateTime << 32 | stamp.dwLowDateTime;
    }
    return status;
}

/* collects every interval, in seconds, on a thread of pdh's, and sets the event after each collection. */
PDH_STATUS WINAPI PdhCollectQueryDataEx(PDH_HQUERY handle, DWORD interval, HANDLE event)
{
    if (!interval || !event)
        return PDH_INVALID_ARGUMENT;
    AcquireSRWLockExclusive(&g_lock);
    QUERY *query = find_query(handle);
    if (!query) {
        ReleaseSRWLockExclusive(&g_lock);
        return PDH_INVALID_HANDLE;
    }
    HANDLE old_thread = query->Thread, old_stop = query->Stop;
    query->Thread = nullptr;
    ReleaseSRWLockExclusive(&g_lock);
    stop_collecting(old_thread, old_stop);
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop)
        return PDH_MEMORY_ALLOCATION_FAILURE;
    AcquireSRWLockExclusive(&g_lock);
    query = find_query(handle);
    PDH_STATUS status = query ? ERROR_SUCCESS : PDH_INVALID_HANDLE;
    if (query) {
        query->Stop = stop;
        query->Event = event;
        query->Interval = interval > 4294967 ? 0xFFFFFFFE : interval * 1000;
        query->Thread = CreateThread(nullptr, 0, collect_thread, query, 0, nullptr);
        if (!query->Thread)
            status = PDH_MEMORY_ALLOCATION_FAILURE;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (status)
        CloseHandle(stop);
    return status;
}

PDH_STATUS WINAPI PdhSetQueryTimeRange(PDH_HQUERY handle, PDH_TIME_INFO_ *info)
{
    if (!info)
        return PDH_INVALID_ARGUMENT;
    AcquireSRWLockShared(&g_lock);
    BOOL known = find_query(handle) != nullptr;
    ReleaseSRWLockShared(&g_lock);
    /* the time range selects records of a log, and the queries here are live. */
    return known ? PDH_INVALID_ARGUMENT : PDH_INVALID_HANDLE;
}

/* ------------------------------------------------------------------ values */

static BOOL needs_previous(DWORD type)
{
    return (type & PERF_TYPE_MASK_) != 0 && type != 0x20020400 && type != 0x20020500;
}

static DWORD format_kind(DWORD format)
{
    return format & (PDH_FMT_LONG | PDH_FMT_DOUBLE | PDH_FMT_LARGE);
}

static BOOL valid_kind(DWORD kind)
{
    return kind == PDH_FMT_LONG || kind == PDH_FMT_DOUBLE || kind == PDH_FMT_LARGE;
}

static void apply_scale(COUNTER *counter, DWORD format, PDH_FMT_COUNTERVALUE *value)
{
    LONG scale = counter->Scale == INT32_MIN ? counter->DefaultScale : counter->Scale;
    if ((format & PDH_FMT_NOSCALE) || !scale)
        return;
    double factor = 1;
    for (LONG i = 0; i < (scale < 0 ? -scale : scale); i++)
        factor *= 10;
    if (scale < 0)
        factor = 1 / factor;
    DWORD kind = format_kind(format);
    if (kind == PDH_FMT_DOUBLE)
        value->doubleValue *= factor;
    else if (kind == PDH_FMT_LARGE)
        value->largeValue = (LONGLONG)(value->largeValue * factor);
    else
        value->longValue = (LONG)(value->longValue * factor);
}

static PDH_STATUS format_item(COUNTER *counter, const ITEM *item, DWORD format, PDH_FMT_COUNTERVALUE *value)
{
    memset(value, 0, sizeof(*value));
    if (item->Have == 0 || (item->Have < 2 && needs_previous(counter->Type))) {
        value->CStatus = (DWORD)PDH_CSTATUS_INVALID_DATA;
        return PDH_INVALID_DATA;
    }
    PDH_RAW_COUNTER previous = item->Have < 2 ? item->Raw[1] : item->Raw[0];
    LONGLONG frequency = counter->Frequency;
    PDH_RAW_COUNTER current = item->Raw[1];
    PDH_STATUS status = PdhFormatFromRawValue(counter->Type, format, &frequency, &current, &previous, value);
    if (status == ERROR_SUCCESS)
        apply_scale(counter, format, value);
    return status;
}

PDH_STATUS WINAPI PdhGetFormattedCounterValue(PDH_HCOUNTER handle, DWORD format, LPDWORD type,
                                              PPDH_FMT_COUNTERVALUE value)
{
    if (!value)
        return PDH_INVALID_ARGUMENT;
    value->CStatus = (DWORD)-1;
    value->largeValue = 0;
    if (type)
        *type = 0;
    if (!valid_kind(format_kind(format)))
        return PDH_INVALID_ARGUMENT;
    AcquireSRWLockShared(&g_lock);
    COUNTER *counter = find_counter(handle);
    if (!counter) {
        ReleaseSRWLockShared(&g_lock);
        return PDH_INVALID_HANDLE;
    }
    PDH_STATUS status;
    if (counter->Wildcard) {
        status = PDH_INVALID_ARGUMENT;
    } else {
        if (type)
            *type = counter->Type;
        if (counter->ItemCount) {
            status = format_item(counter, &counter->Items[0], format, value);
        } else {
            value->CStatus = counter->Collections || counter->Status ? counter->Status : (DWORD)PDH_CSTATUS_INVALID_DATA;
            status = PDH_INVALID_DATA;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return status;
}

PDH_STATUS WINAPI PdhGetRawCounterValue(PDH_HCOUNTER handle, LPDWORD type, PPDH_RAW_COUNTER value)
{
    if (!value)
        return PDH_INVALID_ARGUMENT;
    memset(value, 0, sizeof(*value));
    AcquireSRWLockShared(&g_lock);
    COUNTER *counter = find_counter(handle);
    if (!counter) {
        ReleaseSRWLockShared(&g_lock);
        return PDH_INVALID_HANDLE;
    }
    PDH_STATUS status = ERROR_SUCCESS;
    if (counter->Wildcard) {
        status = PDH_INVALID_ARGUMENT;
    } else {
        if (type)
            *type = counter->Type;
        if (counter->ItemCount)
            *value = counter->Items[0].Raw[1];
        else
            value->CStatus = counter->Status ? counter->Status : (DWORD)PDH_CSTATUS_INVALID_DATA;
    }
    ReleaseSRWLockShared(&g_lock);
    return status;
}

/* the names of the items go behind the array, in the buffer of the caller. */
static SIZE_T name_bytes(const WCHAR *text, BOOL ansi)
{
    if (!ansi)
        return (wcslen(text) + 1) * sizeof(WCHAR);
    return (SIZE_T)WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
}

static PDH_STATUS array_names(COUNTER *counter, BOOL ansi, SIZE_T item_size, DWORD *size, DWORD *count, BYTE *buffer,
                              void (*fill)(BYTE *item, const ITEM *source, COUNTER *counter, BYTE *name, BOOL ansi, DWORD room))
{
    DWORD needed = (DWORD)(item_size * counter->ItemCount);
    for (DWORD i = 0; i < counter->ItemCount; i++)
        needed += (DWORD)name_bytes(counter->Items[i].Name, ansi);
    DWORD room = *size;
    *count = counter->ItemCount;
    *size = needed;
    if (!buffer || room < needed)
        return PDH_MORE_DATA;
    BYTE *name = buffer + item_size * counter->ItemCount;
    for (DWORD i = 0; i < counter->ItemCount; i++) {
        BYTE *item = buffer + item_size * i;
        fill(item, &counter->Items[i], counter, name, ansi, 0);
        name += name_bytes(counter->Items[i].Name, ansi);
    }
    return ERROR_SUCCESS;
}

static void put_name(BYTE *name, const WCHAR *text, BOOL ansi)
{
    if (ansi)
        WideCharToMultiByte(CP_ACP, 0, text, -1, (char *)name, 1024, nullptr, nullptr);
    else
        memcpy(name, text, (wcslen(text) + 1) * sizeof(WCHAR));
}

static void fill_raw(BYTE *item, const ITEM *source, COUNTER *counter, BYTE *name, BOOL ansi, DWORD room)
{
    (void)counter;
    (void)room;
    PDH_RAW_COUNTER_ITEM_W *w = (PDH_RAW_COUNTER_ITEM_W *)item;
    put_name(name, source->Name, ansi);
    w->szName = (LPWSTR)name;
    w->RawValue = source->Raw[1];
}

static void fill_formatted(BYTE *item, const ITEM *source, COUNTER *counter, BYTE *name, BOOL ansi, DWORD room)
{
    (void)room;
    (void)counter;
    PDH_FMT_COUNTERVALUE_ITEM_W *w = (PDH_FMT_COUNTERVALUE_ITEM_W *)item;
    put_name(name, source->Name, ansi);
    w->szName = (LPWSTR)name;
}

static PDH_STATUS counter_array(PDH_HCOUNTER handle, DWORD format, BOOL formatted, BOOL ansi, DWORD *size, DWORD *count,
                                BYTE *buffer)
{
    if (!size || !count)
        return PDH_INVALID_ARGUMENT;
    if (formatted && (format & 0x4070))
        return PDH_INVALID_ARGUMENT;
    if (formatted && !valid_kind(format_kind(format)))
        return PDH_INVALID_ARGUMENT;
    if (*size && !buffer)
        return PDH_INVALID_ARGUMENT;
    *count = 0;
    AcquireSRWLockShared(&g_lock);
    COUNTER *counter = find_counter(handle);
    if (!counter) {
        ReleaseSRWLockShared(&g_lock);
        return PDH_INVALID_HANDLE;
    }
    PDH_STATUS status;
    if (!counter->Collections && !counter->ItemCount) {
        status = PDH_CSTATUS_INVALID_DATA;
    } else {
        SIZE_T item_size = formatted ? sizeof(PDH_FMT_COUNTERVALUE_ITEM_W) : sizeof(PDH_RAW_COUNTER_ITEM_W);
        status = array_names(counter, ansi, item_size, size, count, buffer, formatted ? fill_formatted : fill_raw);
        if (!status && formatted) {
            for (DWORD i = 0; i < counter->ItemCount; i++) {
                PDH_FMT_COUNTERVALUE_ITEM_W *item = (PDH_FMT_COUNTERVALUE_ITEM_W *)(buffer + item_size * i);
                format_item(counter, &counter->Items[i], format, &item->FmtValue);
            }
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return status;
}

PDH_STATUS WINAPI PdhGetFormattedCounterArrayW(PDH_HCOUNTER counter, DWORD format, LPDWORD size, LPDWORD count,
                                               PPDH_FMT_COUNTERVALUE_ITEM_W buffer)
{
    return counter_array(counter, format, TRUE, FALSE, size, count, (BYTE *)buffer);
}

PDH_STATUS WINAPI PdhGetFormattedCounterArrayA(PDH_HCOUNTER counter, DWORD format, LPDWORD size, LPDWORD count,
                                               PPDH_FMT_COUNTERVALUE_ITEM_A buffer)
{
    return counter_array(counter, format, TRUE, TRUE, size, count, (BYTE *)buffer);
}

PDH_STATUS WINAPI PdhGetRawCounterArrayW(PDH_HCOUNTER counter, LPDWORD size, LPDWORD count, PPDH_RAW_COUNTER_ITEM_W buffer)
{
    return counter_array(counter, 0, FALSE, FALSE, size, count, (BYTE *)buffer);
}

PDH_STATUS WINAPI PdhGetRawCounterArrayA(PDH_HCOUNTER counter, LPDWORD size, LPDWORD count, PPDH_RAW_COUNTER_ITEM_A buffer)
{
    return counter_array(counter, 0, FALSE, TRUE, size, count, (BYTE *)buffer);
}

PDH_STATUS WINAPI PdhGetDllVersion(LPDWORD version)
{
    if (!version)
        return PDH_INVALID_ARGUMENT;
    *version = PDH_CVERSION;
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ counter attributes */

PDH_STATUS WINAPI PdhSetCounterScaleFactor(PDH_HCOUNTER handle, LONG factor)
{
    AcquireSRWLockExclusive(&g_lock);
    COUNTER *counter = find_counter(handle);
    PDH_STATUS status = counter ? ERROR_SUCCESS : PDH_INVALID_HANDLE;
    if (counter) {
        if (factor < -7 || factor > 7)
            status = PDH_INVALID_ARGUMENT;
        else
            counter->Scale = factor;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return status;
}

PDH_STATUS WINAPI PdhGetCounterTimeBase(PDH_HCOUNTER handle, LONGLONG *frequency)
{
    if (!frequency)
        return PDH_INVALID_ARGUMENT;
    AcquireSRWLockShared(&g_lock);
    COUNTER *counter = find_counter(handle);
    PDH_STATUS status = counter ? ERROR_SUCCESS : PDH_INVALID_HANDLE;
    if (counter)
        *frequency = counter->Frequency;
    ReleaseSRWLockShared(&g_lock);
    return status;
}

static void machine_path_name(WCHAR *out, DWORD chars)
{
    WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameW(computer, &size))
        computer[0] = 0;
    wsprintfW(out, L"\\\\%s", computer);
    (void)chars;
}

/* the counter's strings go behind the structure, in the order of the elements. */
static PDH_STATUS counter_info(PDH_HCOUNTER handle, BOOLEAN explain, BOOL ansi, DWORD *size, BYTE *buffer)
{
    if (!size)
        return PDH_INVALID_ARGUMENT;
    if (*size && !buffer)
        return PDH_INVALID_ARGUMENT;
    AcquireSRWLockShared(&g_lock);
    COUNTER *counter = find_counter(handle);
    if (!counter) {
        ReleaseSRWLockShared(&g_lock);
        return PDH_INVALID_HANDLE;
    }
    WCHAR machine[2 + MAX_COMPUTERNAME_LENGTH + 1], full[2200];
    machine_path_name(machine, 40);
    PDH_PATH_ELEMENTS_W elements = {machine, counter->Parsed->Object, counter->Parsed->Instance,
                                    counter->Parsed->Parent, counter->Parsed->Index, counter->Parsed->Counter};
    DWORD full_chars = 2200;
    PdhiMakePath(&elements, full, &full_chars);
    const WCHAR *help = explain ? PdhiHelpOf(counter->CounterIndex) : nullptr;
    PDH_COUNTER_INFO_W info = {};
    PDH_COUNTER_INFO_A info_a = {};
    const WCHAR *texts[7] = {full, machine, counter->Parsed->Object, counter->Parsed->Instance, counter->Parsed->Parent,
                             counter->Parsed->Counter, help};
    SIZE_T unit = ansi ? 1 : sizeof(WCHAR), total = ansi ? sizeof(info_a) : sizeof(info);
    total -= sizeof(DWORD);
    for (int i = 0; i < 7; i++)
        if (texts[i])
            total += (wcslen(texts[i]) + 1) * unit * (ansi ? 2 : 1);
    total = (total + 7) & ~(SIZE_T)7;
    DWORD room = *size;
    *size = (DWORD)total;
    PDH_STATUS status = ERROR_SUCCESS;
    if (room < total) {
        status = PDH_MORE_DATA;
    } else {
        memset(buffer, 0, total);
        BYTE *at = buffer + (ansi ? sizeof(info_a) - sizeof(DWORD) : sizeof(info) - sizeof(DWORD));
        void *slots[7] = {};
        for (int i = 0; i < 7; i++) {
            if (!texts[i])
                continue;
            slots[i] = at;
            if (ansi) {
                int n = WideCharToMultiByte(CP_ACP, 0, texts[i], -1, (char *)at, 4096, nullptr, nullptr);
                at += n;
            } else {
                SIZE_T bytes = (wcslen(texts[i]) + 1) * sizeof(WCHAR);
                memcpy(at, texts[i], bytes);
                at += bytes;
            }
        }
        if (ansi) {
            PDH_COUNTER_INFO_A *out = (PDH_COUNTER_INFO_A *)buffer;
            out->dwLength = (DWORD)total;
            out->dwType = counter->Type;
            out->CVersion = PDH_CVERSION;
            out->CStatus = counter->Status;
            out->lScale = counter->Scale == INT32_MIN ? 0 : counter->Scale;
            out->lDefaultScale = counter->DefaultScale;
            out->dwUserData = counter->user;
            out->dwQueryUserData = counter->query->user;
            out->szFullPath = slots[0];
            out->CounterPath.szMachineName = slots[1];
            out->CounterPath.szObjectName = slots[2];
            out->CounterPath.szInstanceName = slots[3];
            out->CounterPath.szParentInstance = slots[4];
            out->CounterPath.dwInstanceIndex = counter->Parsed->Index;
            out->CounterPath.szCounterName = slots[5];
            out->szExplainText = slots[6];
        } else {
            PDH_COUNTER_INFO_W *out = (PDH_COUNTER_INFO_W *)buffer;
            out->dwLength = (DWORD)total;
            out->dwType = counter->Type;
            out->CVersion = PDH_CVERSION;
            out->CStatus = counter->Status;
            out->lScale = counter->Scale == INT32_MIN ? 0 : counter->Scale;
            out->lDefaultScale = counter->DefaultScale;
            out->dwUserData = counter->user;
            out->dwQueryUserData = counter->query->user;
            out->szFullPath = slots[0];
            out->CounterPath.szMachineName = slots[1];
            out->CounterPath.szObjectName = slots[2];
            out->CounterPath.szInstanceName = slots[3];
            out->CounterPath.szParentInstance = slots[4];
            out->CounterPath.dwInstanceIndex = counter->Parsed->Index;
            out->CounterPath.szCounterName = slots[5];
            out->szExplainText = slots[6];
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return status;
}

PDH_STATUS WINAPI PdhGetCounterInfoW(PDH_HCOUNTER counter, BOOLEAN explain, LPDWORD size, PPDH_COUNTER_INFO_W buffer)
{
    return counter_info(counter, explain, FALSE, size, (BYTE *)buffer);
}

PDH_STATUS WINAPI PdhGetCounterInfoA(PDH_HCOUNTER counter, BOOLEAN explain, LPDWORD size, PPDH_COUNTER_INFO_A buffer)
{
    return counter_info(counter, explain, TRUE, size, (BYTE *)buffer);
}

/* ------------------------------------------------------------------ calculations on raw values */

PDH_STATUS WINAPI PdhCalculateCounterFromRawValue(PDH_HCOUNTER handle, DWORD format, PPDH_RAW_COUNTER current,
                                                  PPDH_RAW_COUNTER previous, PPDH_FMT_COUNTERVALUE value)
{
    if (!value || !current || !previous || !valid_kind(format_kind(format)))
        return PDH_INVALID_ARGUMENT;
    AcquireSRWLockShared(&g_lock);
    COUNTER *counter = find_counter(handle);
    if (!counter) {
        ReleaseSRWLockShared(&g_lock);
        return PDH_INVALID_HANDLE;
    }
    LONGLONG frequency = counter->Frequency;
    DWORD type = counter->Type;
    LONG scale = counter->Scale == INT32_MIN ? counter->DefaultScale : counter->Scale;
    ReleaseSRWLockShared(&g_lock);
    memset(value, 0, sizeof(*value));
    PDH_STATUS status = PdhFormatFromRawValue(type, format, &frequency, current, previous, value);
    if (status == ERROR_SUCCESS && scale && !(format & PDH_FMT_NOSCALE)) {
        COUNTER shadow = {};
        shadow.Scale = scale;
        apply_scale(&shadow, format, value);
    }
    return status;
}

/* the least, greatest and mean of the values between two raw samples of a counter. */
PDH_STATUS WINAPI PdhComputeCounterStatistics(PDH_HCOUNTER handle, DWORD format, DWORD first, DWORD count,
                                              PPDH_RAW_COUNTER samples, PDH_STATISTICS_ *statistics)
{
    if (!statistics || !samples || !valid_kind(format_kind(format)) || count < 2)
        return PDH_INVALID_ARGUMENT;
    AcquireSRWLockShared(&g_lock);
    COUNTER *counter = find_counter(handle);
    if (!counter) {
        ReleaseSRWLockShared(&g_lock);
        return PDH_INVALID_HANDLE;
    }
    LONGLONG frequency = counter->Frequency;
    DWORD type = counter->Type;
    ReleaseSRWLockShared(&g_lock);
    memset(statistics, 0, sizeof(*statistics));
    statistics->dwFormat = format;
    double minimum = 0, maximum = 0, sum = 0;
    DWORD used = 0;
    for (DWORD i = first + 1; i < first + count; i++) {
        PDH_FMT_COUNTERVALUE value = {};
        double as_double;
        if (PdhFormatFromRawValue(type, (format & ~(PDH_FMT_LONG | PDH_FMT_LARGE)) | PDH_FMT_DOUBLE, &frequency,
                                  &samples[i], &samples[i - 1], &value))
            continue;
        as_double = value.doubleValue;
        if (!used || as_double < minimum)
            minimum = as_double;
        if (!used || as_double > maximum)
            maximum = as_double;
        sum += as_double;
        used++;
    }
    if (!used)
        return PDH_CALC_NEGATIVE_VALUE;
    statistics->count = used;
    PDH_FMT_COUNTERVALUE *outs[3] = {&statistics->min, &statistics->max, &statistics->mean};
    double results[3] = {minimum, maximum, sum / used};
    DWORD kind = format_kind(format);
    for (int i = 0; i < 3; i++) {
        outs[i]->CStatus = PDH_CSTATUS_VALID_DATA;
        if (kind == PDH_FMT_DOUBLE)
            outs[i]->doubleValue = results[i];
        else if (kind == PDH_FMT_LARGE)
            outs[i]->largeValue = (LONGLONG)results[i];
        else
            outs[i]->longValue = (LONG)results[i];
    }
    return ERROR_SUCCESS;
}
