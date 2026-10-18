/*
 * pdh: real time queries over the processor counters, from the per
 * processor times of NtQuerySystemInformation. logs, other machines and the
 * performance registry are not there.
 */
#define PDH_FUNCTION LONG WINAPI
#include <mime/win32/pdh.h>
#include <mime/ntapi.h>

#define QUERY_MAGIC 0x59524551
#define COUNTER_MAGIC 0x544E4F43

/* the compiler references this symbol from code that uses floating point. */
int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

typedef enum { PROCESSOR_TIME, IDLE_TIME, USER_TIME, PRIVILEGED_TIME } COUNTER_KIND;

typedef struct {
    ULONGLONG idle, kernel, user;
    FILETIME when;
} SAMPLE;

typedef struct COUNTER {
    DWORD magic;
    struct QUERY *query;
    struct COUNTER *next;
    COUNTER_KIND kind;
    LONG instance;
    DWORD_PTR user;
    SAMPLE samples[2];
    UINT count;
} COUNTER;

typedef struct QUERY {
    DWORD magic;
    struct QUERY *next;
    DWORD_PTR user;
    COUNTER *counters;
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

PDH_STATUS WINAPI PdhOpenQueryW(LPCWSTR source, DWORD_PTR user, PDH_HQUERY *handle)
{
    if (!handle)
        return PDH_INVALID_ARGUMENT;
    *handle = nullptr;
    if (source) {
        SIZE_T length = wcslen(source);
        if (!length || length > 1024)
            return PDH_INVALID_ARGUMENT;
        /* a log file mime has no reader for. */
        return PDH_FILE_NOT_FOUND;
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

PDH_STATUS WINAPI PdhCloseQuery(PDH_HQUERY handle)
{
    AcquireSRWLockExclusive(&g_lock);
    QUERY *query = find_query(handle);
    if (!query) {
        ReleaseSRWLockExclusive(&g_lock);
        return PDH_INVALID_HANDLE;
    }
    for (COUNTER *counter = query->counters, *next; counter; counter = next) {
        next = counter->next;
        counter->magic = 0;
        LocalFree(counter);
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
    return find_query(handle) != nullptr;
}

/* ------------------------------------------------------------------ counter paths */

static BOOL same(LPCWSTR text, SIZE_T length, LPCWSTR name)
{
    return wcslen(name) == length && !_wcsnicmp(text, name, length);
}

static ULONG processor_count(void)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors;
}

/* "\\machine\object(instance)\counter", the machine optional. */
static PDH_STATUS parse_path(LPCWSTR path, COUNTER_KIND *kind, LONG *instance)
{
    if (!path || path[0] != '\\')
        return PDH_CSTATUS_BAD_COUNTERNAME;
    LPCWSTR p = path;
    if (p[1] == '\\') {
        LPCWSTR machine = p + 2, end = machine;
        while (*end && *end != '\\')
            end++;
        if (!*end)
            return PDH_CSTATUS_BAD_COUNTERNAME;
        WCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
        GetComputerNameW(name, &size);
        if (!same(machine, (SIZE_T)(end - machine), name) && !same(machine, (SIZE_T)(end - machine), L".") &&
            !same(machine, (SIZE_T)(end - machine), L"localhost"))
            return PDH_CSTATUS_NO_MACHINE;
        p = end;
    }
    LPCWSTR object = p + 1, end = object;
    while (*end && *end != '\\' && *end != '(')
        end++;
    SIZE_T object_length = (SIZE_T)(end - object);
    LPCWSTR instance_name = nullptr;
    SIZE_T instance_length = 0;
    if (*end == '(') {
        instance_name = end + 1;
        while (*end && *end != ')')
            end++;
        if (!*end)
            return PDH_CSTATUS_BAD_COUNTERNAME;
        instance_length = (SIZE_T)(end - instance_name);
        end++;
    }
    if (*end != '\\' || !end[1] || !object_length)
        return PDH_CSTATUS_BAD_COUNTERNAME;
    LPCWSTR counter = end + 1;
    if (!same(object, object_length, L"Processor"))
        return PDH_CSTATUS_NO_OBJECT;
    if (!_wcsicmp(counter, L"% Processor Time"))
        *kind = PROCESSOR_TIME;
    else if (!_wcsicmp(counter, L"% Idle Time"))
        *kind = IDLE_TIME;
    else if (!_wcsicmp(counter, L"% User Time"))
        *kind = USER_TIME;
    else if (!_wcsicmp(counter, L"% Privileged Time"))
        *kind = PRIVILEGED_TIME;
    else
        return PDH_CSTATUS_NO_COUNTER;
    if (!instance_name)
        return PDH_CSTATUS_NO_INSTANCE;
    if (same(instance_name, instance_length, L"_Total")) {
        *instance = -1;
        return ERROR_SUCCESS;
    }
    ULONG number = 0;
    if (!instance_length)
        return PDH_CSTATUS_NO_INSTANCE;
    for (SIZE_T i = 0; i < instance_length; i++) {
        if (instance_name[i] < '0' || instance_name[i] > '9')
            return PDH_CSTATUS_NO_INSTANCE;
        number = number * 10 + (ULONG)(instance_name[i] - '0');
    }
    if (number >= processor_count())
        return PDH_CSTATUS_NO_INSTANCE;
    *instance = (LONG)number;
    return ERROR_SUCCESS;
}

PDH_STATUS WINAPI PdhValidatePathW(LPCWSTR path)
{
    COUNTER_KIND kind;
    LONG instance;
    if (!path)
        return PDH_INVALID_ARGUMENT;
    return parse_path(path, &kind, &instance);
}

PDH_STATUS WINAPI PdhValidatePathA(LPCSTR path)
{
    WCHAR wide[2049];
    if (!path || !MultiByteToWideChar(CP_ACP, 0, path, -1, wide, 2049))
        return PDH_INVALID_ARGUMENT;
    return PdhValidatePathW(wide);
}

PDH_STATUS WINAPI PdhAddCounterW(PDH_HQUERY handle, LPCWSTR path, DWORD_PTR user, PDH_HCOUNTER *out)
{
    if (!out || !path)
        return PDH_INVALID_ARGUMENT;
    *out = nullptr;
    if (wcslen(path) + 1 > 0x800)
        return PDH_INVALID_ARGUMENT;
    COUNTER_KIND kind;
    LONG instance;
    PDH_STATUS status = parse_path(path, &kind, &instance);
    if (status)
        return status;
    AcquireSRWLockExclusive(&g_lock);
    QUERY *query = find_query(handle);
    if (!query) {
        ReleaseSRWLockExclusive(&g_lock);
        return PDH_INVALID_HANDLE;
    }
    COUNTER *counter = LocalAlloc(LMEM_ZEROINIT, sizeof(*counter));
    if (!counter) {
        ReleaseSRWLockExclusive(&g_lock);
        return PDH_MEMORY_ALLOCATION_FAILURE;
    }
    counter->magic = COUNTER_MAGIC;
    counter->query = query;
    counter->kind = kind;
    counter->instance = instance;
    counter->user = user;
    counter->next = query->counters;
    query->counters = counter;
    *out = counter;
    ReleaseSRWLockExclusive(&g_lock);
    return ERROR_SUCCESS;
}

/* the english names are the names, mime's counters are not localized. */
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
    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, wide, 0x801))
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
    counter->magic = 0;
    LocalFree(counter);
    ReleaseSRWLockExclusive(&g_lock);
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ samples */

typedef struct {
    LARGE_INTEGER IdleTime, KernelTime, UserTime, DpcTime, InterruptTime;
    ULONG InterruptCount;
} PROCESSOR_TIMES;

static BOOL take_sample(LONG instance, const PROCESSOR_TIMES *times, ULONG count, SAMPLE *sample)
{
    memset(sample, 0, sizeof(*sample));
    for (ULONG i = 0; i < count; i++) {
        if (instance >= 0 && (ULONG)instance != i)
            continue;
        sample->idle += (ULONGLONG)times[i].IdleTime.QuadPart;
        sample->kernel += (ULONGLONG)times[i].KernelTime.QuadPart;
        sample->user += (ULONGLONG)times[i].UserTime.QuadPart;
    }
    GetSystemTimeAsFileTime(&sample->when);
    return TRUE;
}

PDH_STATUS WINAPI PdhCollectQueryData(PDH_HQUERY handle)
{
    PROCESSOR_TIMES times[256];
    ULONG returned = 0;
    NTSTATUS status = NtQuerySystemInformation(SystemProcessorPerformanceInformation, times, sizeof(times), &returned);
    ULONG count = NT_SUCCESS(status) ? returned / sizeof(PROCESSOR_TIMES) : 0;
    AcquireSRWLockExclusive(&g_lock);
    QUERY *query = find_query(handle);
    PDH_STATUS result = query ? (query->counters ? ERROR_SUCCESS : PDH_NO_DATA) : PDH_INVALID_HANDLE;
    for (COUNTER *counter = query ? query->counters : nullptr; counter; counter = counter->next) {
        counter->samples[0] = counter->samples[1];
        take_sample(counter->instance, times, count, &counter->samples[1]);
        if (counter->count < 2)
            counter->count++;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

PDH_STATUS WINAPI PdhCollectQueryDataWithTime(PDH_HQUERY handle, LONGLONG *timestamp)
{
    if (!timestamp)
        return PDH_INVALID_ARGUMENT;
    PDH_STATUS status = PdhCollectQueryData(handle);
    if (!status) {
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        FileTimeToLocalFileTime(&now, &now);
        *timestamp = (LONGLONG)now.dwHighDateTime << 32 | now.dwLowDateTime;
    }
    return status;
}

static DWORD counter_type(COUNTER_KIND kind)
{
    return kind == PROCESSOR_TIME || kind == IDLE_TIME ? PERF_100NSEC_TIMER_INV : PERF_100NSEC_TIMER;
}

/* the percentage over the last two samples. */
static double percent(const COUNTER *counter)
{
    const SAMPLE *a = &counter->samples[0], *b = &counter->samples[1];
    double total = (double)((b->kernel + b->user) - (a->kernel + a->user));
    if (total <= 0)
        return 0;
    double idle = (double)(b->idle - a->idle), user = (double)(b->user - a->user);
    double kernel = (double)(b->kernel - a->kernel);
    switch (counter->kind) {
    case PROCESSOR_TIME: return 100.0 * (1.0 - idle / total);
    case IDLE_TIME: return 100.0 * idle / total;
    case USER_TIME: return 100.0 * user / total;
    case PRIVILEGED_TIME: return 100.0 * (kernel - idle) / total;
    }
    return 0;
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
    DWORD kind = format & (PDH_FMT_LONG | PDH_FMT_DOUBLE | PDH_FMT_LARGE);
    if (kind != PDH_FMT_LONG && kind != PDH_FMT_DOUBLE && kind != PDH_FMT_LARGE)
        return PDH_INVALID_ARGUMENT;
    AcquireSRWLockShared(&g_lock);
    COUNTER *counter = find_counter(handle);
    if (!counter) {
        ReleaseSRWLockShared(&g_lock);
        return PDH_INVALID_HANDLE;
    }
    if (type)
        *type = counter_type(counter->kind);
    if (counter->count < 2) {
        ReleaseSRWLockShared(&g_lock);
        value->CStatus = (DWORD)PDH_CSTATUS_INVALID_DATA;
        return PDH_INVALID_DATA;
    }
    double result = percent(counter);
    ReleaseSRWLockShared(&g_lock);
    if (result < 0)
        result = 0;
    if (result > 100 && !(format & PDH_FMT_NOCAP100))
        result = 100;
    if (format & PDH_FMT_1000)
        result *= 1000;
    value->CStatus = (DWORD)PDH_CSTATUS_VALID_DATA;
    if (kind == PDH_FMT_DOUBLE)
        value->doubleValue = result;
    else if (kind == PDH_FMT_LONG)
        value->longValue = (LONG)result;
    else
        value->largeValue = (LONGLONG)result;
    return ERROR_SUCCESS;
}

/* the counter's time and the elapsed time of the newest sample, in 100 ns. */
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
    if (type)
        *type = counter_type(counter->kind);
    const SAMPLE *sample = &counter->samples[1];
    if (!counter->count) {
        value->CStatus = (DWORD)PDH_CSTATUS_INVALID_DATA;
    } else {
        value->TimeStamp = sample->when;
        switch (counter->kind) {
        case PROCESSOR_TIME: case IDLE_TIME: value->FirstValue = (LONGLONG)sample->idle; break;
        case USER_TIME: value->FirstValue = (LONGLONG)sample->user; break;
        case PRIVILEGED_TIME: value->FirstValue = (LONGLONG)(sample->kernel - sample->idle); break;
        }
        value->SecondValue = (LONGLONG)(sample->kernel + sample->user);
        value->MultiCount = 1;
    }
    ReleaseSRWLockShared(&g_lock);
    return ERROR_SUCCESS;
}

PDH_STATUS WINAPI PdhGetDllVersion(LPDWORD version)
{
    if (!version)
        return PDH_INVALID_ARGUMENT;
    *version = 0x00000503;
    return ERROR_SUCCESS;
}
