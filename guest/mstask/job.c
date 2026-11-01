/*
 * the task: a job file with the settings, the triggers and the account of a program that the scheduler runs. The file
 * follows the layout of the task scheduler 1.0, a fixed part and then the application, parameters, working directory,
 * creator and comment, the user data, the reserved data and the triggers. There is no service here to run the task, so
 * Run says that the service is not running.
 */
#include "mstask_private.h"

DECLSPEC_IMPORT BOOL WINAPI GetUserNameW(LPWSTR buffer, LPDWORD size);

#define PRODUCT_VERSION 0x0A00
#define FILE_VERSION 1
#define FIXED_SIZE 68

/* the flags of the task kept apart from the ones the callers set. */
#define FLAG_USER_MASK 0x7FFF
#define FLAG_BASE 0x20000000
#define FLAG_FILE_MASK 0x3FFDFFFF
#define TRIGGER_NOT_SET 0x10000

enum { STRING_APPLICATION, STRING_PARAMETERS, STRING_DIRECTORY, STRING_CREATOR, STRING_COMMENT, STRING_COUNT };

typedef struct JOB {
    ITask task;
    IPersistFile persist;
    LONG refs;
    GUID uuid;
    WORD retry_count, retry_interval, idle_deadline, idle_wait, running;
    DWORD priority, max_run, exit_code, status, flags, task_flags;
    HRESULT exit_result;
    SYSTEMTIME last_run;
    LPWSTR strings[STRING_COUNT];
    WORD data_size;
    BYTE *data;
    WORD trigger_count;
    TASK_TRIGGER *triggers;
    /* the file of the task, and whether it exists yet. */
    LPWSTR path;
    BOOL on_disk, dirty;
    /* the account set and not yet saved. */
    LPWSTR account;
    BOOL account_set;
} JOB;

typedef struct TRIGGER {
    ITaskTrigger trigger;
    LONG refs;
    JOB *job;
    WORD index;
} TRIGGER;

static JOB *job_of_task(ITask *task) { return (JOB *)task; }
static JOB *job_of_persist(IPersistFile *persist) { return (JOB *)((char *)persist - offsetof(JOB, persist)); }

static void *allocate(SIZE_T size) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1); }
static void release_memory(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

HRESULT MstaskFailure(void)
{
    DWORD error = GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
}

LPWSTR MstaskDuplicate(LPCWSTR text)
{
    SIZE_T length = wcslen(text) + 1;
    LPWSTR copy = CoTaskMemAlloc(length * sizeof(WCHAR));
    if (copy)
        memcpy(copy, text, length * sizeof(WCHAR));
    return copy;
}

static LPWSTR local_duplicate(LPCWSTR text)
{
    SIZE_T length = wcslen(text) + 1;
    LPWSTR copy = allocate(length * sizeof(WCHAR));
    if (copy)
        memcpy(copy, text, length * sizeof(WCHAR));
    return copy;
}

/* ------------------------------------------------------------------ dates */

static BOOL leap_year(WORD year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

static const BYTE g_month_days[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static BOOL valid_date(WORD month, WORD day, WORD year)
{
    if (month < 1 || month > 12 || day == 0)
        return FALSE;
    return day <= (month == 2 && leap_year(year) ? 29 : g_month_days[month]);
}

static BOOL valid_monthly_date(const TASK_TRIGGER *trigger)
{
    DWORD days = trigger->Type.MonthlyDate.rgfDays;
    DWORD months = trigger->Type.MonthlyDate.rgfMonths;
    if (days == 0 || days > 0x7FFFFFFF || months == 0 || months > 0xFFF)
        return FALSE;
    /* day 31 never comes in a month of 30 days, and day 30 never in february. */
    if (!(months & ~0x52Au) && (days & 0x40000000) && (months & 0x52A))
        return FALSE;
    if ((months & 2) && !(months & ~2u) && (days & 0x20000000))
        return FALSE;
    return TRUE;
}

/* ------------------------------------------------------------------ the status of the task */

static HRESULT job_status(const JOB *job)
{
    if (job->running)
        return SCHED_S_TASK_RUNNING;
    if (job->flags & TASK_FLAG_DISABLED)
        return SCHED_S_TASK_DISABLED;
    if (!job->trigger_count)
        return SCHED_S_TASK_NOT_SCHEDULED;
    for (WORD i = 0; i < job->trigger_count; i++)
        if (!(job->triggers[i].rgFlags & TASK_TRIGGER_FLAG_DISABLED) && !(job->triggers[i].rgFlags & TRIGGER_NOT_SET))
            return SCHED_S_TASK_READY;
    return SCHED_S_TASK_NO_VALID_TRIGGERS;
}

/* ------------------------------------------------------------------ the run times */

#define TICKS_PER_MINUTE 600000000ULL
#define TICKS_PER_DAY (1440 * TICKS_PER_MINUTE)

static ULONGLONG ticks_of(const SYSTEMTIME *time)
{
    FILETIME file;
    if (!SystemTimeToFileTime(time, &file))
        return 0;
    return ((ULONGLONG)file.dwHighDateTime << 32) | file.dwLowDateTime;
}

static void time_of(ULONGLONG ticks, SYSTEMTIME *time)
{
    FILETIME file = {(DWORD)ticks, (DWORD)(ticks >> 32)};
    FileTimeToSystemTime(&file, time);
}

static ULONGLONG day_of(WORD year, WORD month, WORD day)
{
    SYSTEMTIME time = {.wYear = year, .wMonth = month, .wDay = day};
    return ticks_of(&time);
}

/* whether the trigger runs on the day that starts at the given tick. */
static BOOL runs_on_day(const TASK_TRIGGER *trigger, ULONGLONG day)
{
    SYSTEMTIME date;
    time_of(day, &date);
    ULONGLONG first = day_of(trigger->wBeginYear, trigger->wBeginMonth, trigger->wBeginDay);
    if (day < first)
        return FALSE;
    if (trigger->rgFlags & TASK_TRIGGER_FLAG_HAS_END_DATE) {
        ULONGLONG end = day_of(trigger->wEndYear, trigger->wEndMonth, trigger->wEndDay);
        if (day > end)
            return FALSE;
    }
    ULONGLONG since = (day - first) / TICKS_PER_DAY;
    switch (trigger->TriggerType) {
    case TASK_TIME_TRIGGER_ONCE:
        return since == 0;
    case TASK_TIME_TRIGGER_DAILY:
        return trigger->Type.Daily.DaysInterval && since % trigger->Type.Daily.DaysInterval == 0;
    case TASK_TIME_TRIGGER_WEEKLY: {
        /* the weeks count from the sunday of the week of the first day. */
        SYSTEMTIME begin;
        time_of(first, &begin);
        ULONGLONG weeks = (since + begin.wDayOfWeek) / 7;
        return trigger->Type.Weekly.WeeksInterval && weeks % trigger->Type.Weekly.WeeksInterval == 0 &&
               (trigger->Type.Weekly.rgfDaysOfTheWeek & (1u << date.wDayOfWeek));
    }
    case TASK_TIME_TRIGGER_MONTHLYDATE:
        return (trigger->Type.MonthlyDate.rgfMonths & (1u << (date.wMonth - 1))) &&
               (trigger->Type.MonthlyDate.rgfDays & (1u << (date.wDay - 1)));
    case TASK_TIME_TRIGGER_MONTHLYDOW: {
        WORD length = date.wMonth == 2 && leap_year(date.wYear) ? 29 : g_month_days[date.wMonth];
        WORD week = (date.wDay - 1) / 7 + 1;
        BOOL last = date.wDay + 7 > length;
        WORD wanted = trigger->Type.MonthlyDOW.wWhichWeek;
        return (trigger->Type.MonthlyDOW.rgfMonths & (1u << (date.wMonth - 1))) &&
               (trigger->Type.MonthlyDOW.rgfDaysOfTheWeek & (1u << date.wDayOfWeek)) &&
               (wanted == TASK_LAST_WEEK ? last : wanted == week);
    }
    default:
        return FALSE;
    }
}

/* the first run of the trigger at or after a tick, or 0 when it has none in the next ten years. */
static ULONGLONG next_run(const TASK_TRIGGER *trigger, ULONGLONG from)
{
    if (trigger->TriggerType > TASK_TIME_TRIGGER_MONTHLYDOW || (trigger->rgFlags & (TASK_TRIGGER_FLAG_DISABLED | TRIGGER_NOT_SET)))
        return 0;
    ULONGLONG day = from - from % TICKS_PER_DAY;
    ULONGLONG first = day_of(trigger->wBeginYear, trigger->wBeginMonth, trigger->wBeginDay);
    if (day < first)
        day = first;
    ULONGLONG start = (ULONGLONG)trigger->wStartHour * 60 + trigger->wStartMinute;
    for (int i = 0; i < 3660; i++, day += TICKS_PER_DAY) {
        if (!runs_on_day(trigger, day))
            continue;
        for (ULONGLONG offset = 0;; offset += trigger->MinutesInterval) {
            ULONGLONG run = day + (start + offset) * TICKS_PER_MINUTE;
            if (run >= from)
                return run;
            if (!trigger->MinutesInterval || offset + trigger->MinutesInterval >= trigger->MinutesDuration)
                break;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ the triggers */

static HRESULT STDMETHODCALLTYPE trigger_query_interface(ITaskTrigger *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_ITaskTrigger))
        return E_NOINTERFACE;
    *object = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE trigger_add_ref(ITaskTrigger *This)
{
    return InterlockedIncrement(&((TRIGGER *)This)->refs);
}

static ULONG job_release(JOB *job);

static ULONG STDMETHODCALLTYPE trigger_release(ITaskTrigger *This)
{
    TRIGGER *trigger = (TRIGGER *)This;
    LONG refs = InterlockedDecrement(&trigger->refs);
    if (!refs) {
        job_release(trigger->job);
        release_memory(trigger);
        InterlockedDecrement(&g_mstask_objects);
    }
    return refs;
}

static TASK_TRIGGER *find_trigger(JOB *job, WORD index)
{
    for (WORD i = 0; i < job->trigger_count; i++)
        if (job->triggers[i].Reserved1 == index)
            return &job->triggers[i];
    return nullptr;
}

static HRESULT job_get_trigger(JOB *job, WORD index, TASK_TRIGGER *out)
{
    TASK_TRIGGER *trigger = find_trigger(job, index);
    if (!trigger)
        return E_FAIL;
    *out = *trigger;
    out->Reserved1 = 0;
    if (trigger->rgFlags & TRIGGER_NOT_SET)
        out->rgFlags = (trigger->rgFlags & 0x7FFB) | TASK_TRIGGER_FLAG_DISABLED;
    else
        out->rgFlags = trigger->rgFlags & FLAG_USER_MASK;
    return S_OK;
}

static HRESULT job_set_trigger(JOB *job, WORD index, const TASK_TRIGGER *in)
{
    TASK_TRIGGER *trigger = find_trigger(job, index);
    if (!trigger)
        return E_FAIL;
    if (in->cbTriggerSize != sizeof(TASK_TRIGGER) || in->wStartHour > 23 || in->wStartMinute > 59 ||
        !valid_date(in->wBeginMonth, in->wBeginDay, in->wBeginYear) ||
        ((in->rgFlags & TASK_TRIGGER_FLAG_HAS_END_DATE) && !valid_date(in->wEndMonth, in->wEndDay, in->wEndYear)))
        return E_INVALIDARG;
    if ((in->MinutesDuration || in->MinutesInterval) && in->MinutesInterval >= in->MinutesDuration)
        return E_INVALIDARG;
    TASK_TRIGGER next = *trigger;
    switch (in->TriggerType) {
    case TASK_TIME_TRIGGER_ONCE:
    case TASK_EVENT_TRIGGER_ON_IDLE:
    case TASK_EVENT_TRIGGER_AT_SYSTEMSTART:
    case TASK_EVENT_TRIGGER_AT_LOGON:
        break;
    case TASK_TIME_TRIGGER_DAILY:
        if (!in->Type.Daily.DaysInterval)
            return E_INVALIDARG;
        next.Type.Daily.DaysInterval = in->Type.Daily.DaysInterval;
        break;
    case TASK_TIME_TRIGGER_WEEKLY:
        if (!in->Type.Weekly.WeeksInterval || !in->Type.Weekly.rgfDaysOfTheWeek || in->Type.Weekly.rgfDaysOfTheWeek > 0x7F)
            return E_INVALIDARG;
        next.Type.Weekly = in->Type.Weekly;
        break;
    case TASK_TIME_TRIGGER_MONTHLYDATE:
        if (!valid_monthly_date(in))
            return E_INVALIDARG;
        next.Type.MonthlyDate = in->Type.MonthlyDate;
        break;
    case TASK_TIME_TRIGGER_MONTHLYDOW:
        if (in->Type.MonthlyDOW.wWhichWeek < 1 || in->Type.MonthlyDOW.wWhichWeek > 5 || !in->Type.MonthlyDOW.rgfDaysOfTheWeek ||
            in->Type.MonthlyDOW.rgfDaysOfTheWeek > 0x7F || !in->Type.MonthlyDOW.rgfMonths || in->Type.MonthlyDOW.rgfMonths > 0xFFF)
            return E_INVALIDARG;
        next.Type.MonthlyDOW = in->Type.MonthlyDOW;
        break;
    default:
        return E_INVALIDARG;
    }
    WORD kept = trigger->Reserved1;
    next.TriggerType = in->TriggerType;
    next.cbTriggerSize = in->cbTriggerSize;
    next.wBeginYear = in->wBeginYear;
    next.wBeginMonth = in->wBeginMonth;
    next.wBeginDay = in->wBeginDay;
    next.wEndYear = in->wEndYear;
    next.wEndMonth = in->wEndMonth;
    next.wEndDay = in->wEndDay;
    next.wStartHour = in->wStartHour;
    next.wStartMinute = in->wStartMinute;
    next.MinutesDuration = in->MinutesDuration;
    next.MinutesInterval = in->MinutesInterval;
    next.rgFlags = in->rgFlags & FLAG_USER_MASK;
    next.Reserved1 = kept;
    *trigger = next;
    job->dirty = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE trigger_set(ITaskTrigger *This, const TASK_TRIGGER *in)
{
    TRIGGER *trigger = (TRIGGER *)This;
    return in ? job_set_trigger(trigger->job, trigger->index, in) : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE trigger_get(ITaskTrigger *This, TASK_TRIGGER *out)
{
    TRIGGER *trigger = (TRIGGER *)This;
    return out ? job_get_trigger(trigger->job, trigger->index, out) : E_INVALIDARG;
}

/* appends text when it fits. */
static void append(WCHAR *text, SIZE_T size, LPCWSTR more)
{
    SIZE_T used = wcslen(text);
    if (used + wcslen(more) + 1 <= size)
        wcscpy(text + used, more);
}

static void describe(const TASK_TRIGGER *trigger, WCHAR *text, SIZE_T size)
{
    SYSTEMTIME begin = {.wYear = trigger->wBeginYear, .wMonth = trigger->wBeginMonth, .wDay = trigger->wBeginDay,
                        .wHour = trigger->wStartHour, .wMinute = trigger->wStartMinute};
    WCHAR time[64] = L"", date[64] = L"", list[256] = L"";
    GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &begin, nullptr, time, RTL_NUMBER_OF(time));
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &begin, nullptr, date, RTL_NUMBER_OF(date));
    static const LPCWSTR days[] = {L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
    static const LPCWSTR months[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
    switch (trigger->TriggerType) {
    case TASK_TIME_TRIGGER_ONCE:
        _snwprintf(text, size, L"At %s on %s", time, date);
        break;
    case TASK_TIME_TRIGGER_DAILY:
        if (trigger->Type.Daily.DaysInterval == 1)
            _snwprintf(text, size, L"At %s every day, starting %s", time, date);
        else
            _snwprintf(text, size, L"At %s every %u days, starting %s", time, trigger->Type.Daily.DaysInterval, date);
        break;
    case TASK_TIME_TRIGGER_WEEKLY:
        for (int i = 0; i < 7; i++)
            if (trigger->Type.Weekly.rgfDaysOfTheWeek & (1u << i)) {
                if (*list)
                    append(list, RTL_NUMBER_OF(list), L", ");
                append(list, RTL_NUMBER_OF(list), days[i]);
            }
        if (trigger->Type.Weekly.WeeksInterval == 1)
            _snwprintf(text, size, L"At %s every %s of every week, starting %s", time, list, date);
        else
            _snwprintf(text, size, L"At %s every %s of every %u weeks, starting %s", time, list, trigger->Type.Weekly.WeeksInterval, date);
        break;
    case TASK_TIME_TRIGGER_MONTHLYDATE:
    case TASK_TIME_TRIGGER_MONTHLYDOW: {
        WCHAR number[16];
        WORD month_flags = trigger->TriggerType == TASK_TIME_TRIGGER_MONTHLYDATE ? trigger->Type.MonthlyDate.rgfMonths
                                                                                  : trigger->Type.MonthlyDOW.rgfMonths;
        if (month_flags == 0xFFF)
            append(list, RTL_NUMBER_OF(list), L"every month");
        else
            for (int i = 0; i < 12; i++)
                if (month_flags & (1u << i)) {
                    if (*list)
                        append(list, RTL_NUMBER_OF(list), L", ");
                    append(list, RTL_NUMBER_OF(list), months[i]);
                }
        if (trigger->TriggerType == TASK_TIME_TRIGGER_MONTHLYDATE) {
            WCHAR which[160] = L"";
            for (int i = 0; i < 31; i++)
                if (trigger->Type.MonthlyDate.rgfDays & (1u << i)) {
                    if (*which)
                        append(which, RTL_NUMBER_OF(which), L",");
                    _snwprintf(number, RTL_NUMBER_OF(number), L"%d", i + 1);
                    append(which, RTL_NUMBER_OF(which), number);
                }
            _snwprintf(text, size, L"At %s on day %s of %s, starting %s", time, which, list, date);
        } else {
            static const LPCWSTR weeks[] = {L"", L"first", L"second", L"third", L"fourth", L"last"};
            WCHAR which[64] = L"";
            for (int i = 0; i < 7; i++)
                if (trigger->Type.MonthlyDOW.rgfDaysOfTheWeek & (1u << i)) {
                    if (*which)
                        append(which, RTL_NUMBER_OF(which), L", ");
                    append(which, RTL_NUMBER_OF(which), days[i]);
                }
            _snwprintf(text, size, L"At %s on the %s %s of %s, starting %s", time, weeks[trigger->Type.MonthlyDOW.wWhichWeek], which, list, date);
        }
        break;
    }
    case TASK_EVENT_TRIGGER_ON_IDLE:
        _snwprintf(text, size, L"When the computer is idle");
        break;
    case TASK_EVENT_TRIGGER_AT_SYSTEMSTART:
        _snwprintf(text, size, L"At system startup");
        break;
    case TASK_EVENT_TRIGGER_AT_LOGON:
        _snwprintf(text, size, L"At log on");
        break;
    default:
        text[0] = 0;
    }
}

static HRESULT job_trigger_string(JOB *job, WORD index, LPWSTR *out)
{
    TASK_TRIGGER trigger;
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    HRESULT result = job_get_trigger(job, index, &trigger);
    if (FAILED(result))
        return SCHED_E_TRIGGER_NOT_FOUND;
    WCHAR text[512];
    describe(&trigger, text, RTL_NUMBER_OF(text));
    *out = MstaskDuplicate(text);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE trigger_get_string(ITaskTrigger *This, LPWSTR *text)
{
    TRIGGER *trigger = (TRIGGER *)This;
    return job_trigger_string(trigger->job, trigger->index, text);
}

static const ITaskTriggerVtbl g_trigger_vtbl = {
    trigger_query_interface, trigger_add_ref, trigger_release, trigger_set, trigger_get, trigger_get_string,
};

static void job_add_ref(JOB *job);

static HRESULT trigger_create(JOB *job, WORD index, ITaskTrigger **out)
{
    TRIGGER *trigger = allocate(sizeof(*trigger));
    if (!trigger)
        return E_OUTOFMEMORY;
    trigger->trigger.lpVtbl = &g_trigger_vtbl;
    trigger->refs = 1;
    trigger->job = job;
    trigger->index = index;
    job_add_ref(job);
    InterlockedIncrement(&g_mstask_objects);
    *out = &trigger->trigger;
    return S_OK;
}

/* ------------------------------------------------------------------ the account store */

/* the accounts of the tasks live in a file of the tasks folder, one name and account after the other. */
static BOOL account_file(WCHAR path[MAX_PATH])
{
    if (FAILED(MstaskTasksFolder(path)) || wcslen(path) + 8 > MAX_PATH)
        return FALSE;
    wcscat(path, L"\\SA.DAT");
    return TRUE;
}

static const WCHAR *job_name(LPCWSTR path)
{
    const WCHAR *name = wcsrchr(path, L'\\');
    return name ? name + 1 : path;
}

typedef struct ACCOUNT_ENTRY {
    LPWSTR name, account;
} ACCOUNT_ENTRY;

static void account_entries_free(ACCOUNT_ENTRY *entries, ULONG count)
{
    for (ULONG i = 0; i < count; i++) {
        release_memory(entries[i].name);
        release_memory(entries[i].account);
    }
    release_memory(entries);
}

static BOOL account_entries_read(ACCOUNT_ENTRY **entries, ULONG *count)
{
    WCHAR path[MAX_PATH];
    *entries = nullptr;
    *count = 0;
    if (!account_file(path))
        return FALSE;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return TRUE;
    DWORD size = GetFileSize(file, nullptr), read = 0;
    BYTE *data = size && size < 0x100000 ? allocate(size) : nullptr;
    BOOL ok = TRUE;
    if (data && ReadFile(file, data, size, &read, nullptr) && read == size) {
        ACCOUNT_ENTRY *list = allocate(sizeof(*list) * 1024);
        DWORD at = 0;
        ULONG n = 0;
        while (ok && list && at + 2 <= size && n < 1024) {
            WORD lengths[2];
            LPWSTR parts[2];
            for (int i = 0; i < 2 && ok; i++) {
                if (at + 2 > size) { ok = FALSE; break; }
                lengths[i] = *(WORD *)(data + at);
                at += 2;
                if (at + lengths[i] * 2u > size) { ok = FALSE; break; }
                parts[i] = allocate((lengths[i] + 1) * sizeof(WCHAR));
                if (!parts[i]) { ok = FALSE; break; }
                memcpy(parts[i], data + at, lengths[i] * 2u);
                at += lengths[i] * 2u;
            }
            if (!ok)
                break;
            list[n].name = parts[0];
            list[n].account = parts[1];
            n++;
        }
        *entries = list;
        *count = n;
    }
    release_memory(data);
    CloseHandle(file);
    return ok;
}

static LPWSTR account_lookup(LPCWSTR path)
{
    ACCOUNT_ENTRY *entries;
    ULONG count;
    LPWSTR found = nullptr;
    if (!account_entries_read(&entries, &count))
        return nullptr;
    for (ULONG i = 0; i < count; i++)
        if (!_wcsicmp(entries[i].name, job_name(path))) {
            found = MstaskDuplicate(entries[i].account);
            break;
        }
    account_entries_free(entries, count);
    return found;
}

/* sets the account of the task, or takes it out when it is null. */
static void account_update(LPCWSTR path, LPCWSTR account)
{
    ACCOUNT_ENTRY *entries;
    ULONG count;
    WCHAR file_path[MAX_PATH];
    if (!account_file(file_path) || !account_entries_read(&entries, &count))
        return;
    DWORD size = 0;
    for (ULONG i = 0; i < count; i++)
        size += 4 + (DWORD)(wcslen(entries[i].name) + wcslen(entries[i].account)) * 2;
    size += account ? 4 + (DWORD)(wcslen(job_name(path)) + wcslen(account)) * 2 : 0;
    BYTE *data = allocate(size);
    if (data) {
        DWORD at = 0;
        for (ULONG i = 0; i <= count; i++) {
            LPCWSTR name, value;
            if (i < count) {
                name = entries[i].name;
                value = entries[i].account;
                if (!_wcsicmp(name, job_name(path)))
                    continue;
            } else if (account) {
                name = job_name(path);
                value = account;
            } else {
                break;
            }
            WORD name_length = (WORD)wcslen(name), value_length = (WORD)wcslen(value);
            *(WORD *)(data + at) = name_length;
            memcpy(data + at + 2, name, name_length * 2u);
            at += 2 + name_length * 2u;
            *(WORD *)(data + at) = value_length;
            memcpy(data + at + 2, value, value_length * 2u);
            at += 2 + value_length * 2u;
        }
        HANDLE file = CreateFileW(file_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(file, data, at, &written, nullptr);
            CloseHandle(file);
        }
        release_memory(data);
    }
    account_entries_free(entries, count);
}

void JobForgetAccount(LPCWSTR path) { account_update(path, nullptr); }

/* ------------------------------------------------------------------ the file */

typedef struct CURSOR {
    const BYTE *data;
    DWORD size, at;
    BOOL bad;
} CURSOR;

static const BYTE *cursor_take(CURSOR *cursor, DWORD count)
{
    if (cursor->bad || cursor->size - cursor->at < count) {
        cursor->bad = TRUE;
        return nullptr;
    }
    const BYTE *p = cursor->data + cursor->at;
    cursor->at += count;
    return p;
}

static WORD cursor_word(CURSOR *cursor)
{
    const BYTE *p = cursor_take(cursor, 2);
    return p ? *(const WORD *)p : 0;
}

static void job_clear(JOB *job)
{
    for (int i = 0; i < STRING_COUNT; i++) {
        release_memory(job->strings[i]);
        job->strings[i] = nullptr;
    }
    release_memory(job->data);
    release_memory(job->triggers);
    job->data = nullptr;
    job->triggers = nullptr;
    job->data_size = job->trigger_count = 0;
}

static HRESULT job_parse(JOB *job, const BYTE *data, DWORD size)
{
    CURSOR cursor = {data, size, 0, FALSE};
    if (size < FIXED_SIZE)
        return SCHED_E_INVALID_TASK;
    const BYTE *fixed = cursor_take(&cursor, FIXED_SIZE);
    if (*(const WORD *)(fixed + 2) != FILE_VERSION)
        return SCHED_E_UNKNOWN_OBJECT_VERSION;
    WORD name_offset = *(const WORD *)(fixed + 20), trigger_offset = *(const WORD *)(fixed + 22);
    if (name_offset < FIXED_SIZE + 2 || trigger_offset < name_offset || trigger_offset > size)
        return SCHED_E_INVALID_TASK;
    JOB loaded = {};
    memcpy(&loaded.uuid, fixed + 4, 16);
    loaded.retry_count = *(const WORD *)(fixed + 24);
    loaded.retry_interval = *(const WORD *)(fixed + 26);
    loaded.idle_deadline = *(const WORD *)(fixed + 28);
    loaded.idle_wait = *(const WORD *)(fixed + 30);
    loaded.priority = *(const DWORD *)(fixed + 32);
    loaded.max_run = *(const DWORD *)(fixed + 36);
    loaded.exit_code = *(const DWORD *)(fixed + 40);
    loaded.status = *(const DWORD *)(fixed + 44);
    loaded.flags = *(const DWORD *)(fixed + 48);
    memcpy(&loaded.last_run, fixed + 52, 16);
    loaded.running = cursor_word(&cursor);
    for (int i = 0; i < STRING_COUNT && !cursor.bad; i++) {
        WORD length = cursor_word(&cursor);
        const BYTE *text = cursor_take(&cursor, length * 2u);
        if (cursor.bad)
            break;
        if (length) {
            loaded.strings[i] = allocate(length * 2u + 2);
            if (!loaded.strings[i]) {
                job_clear(&loaded);
                return E_OUTOFMEMORY;
            }
            memcpy(loaded.strings[i], text, length * 2u);
        }
    }
    loaded.data_size = cursor_word(&cursor);
    const BYTE *user = cursor_take(&cursor, loaded.data_size);
    WORD reserved_size = cursor_word(&cursor);
    const BYTE *reserved = cursor_take(&cursor, reserved_size);
    WORD triggers = cursor_word(&cursor);
    const BYTE *list = cursor_take(&cursor, triggers * 48u);
    if (cursor.bad) {
        job_clear(&loaded);
        return SCHED_E_INVALID_TASK;
    }
    if (loaded.data_size) {
        loaded.data = allocate(loaded.data_size);
        if (loaded.data)
            memcpy(loaded.data, user, loaded.data_size);
    }
    if (reserved_size >= 8) {
        loaded.exit_result = *(const HRESULT *)reserved;
        loaded.task_flags = *(const DWORD *)(reserved + 4);
    }
    if (triggers) {
        loaded.triggers = allocate(triggers * 48u);
        if (loaded.triggers) {
            memcpy(loaded.triggers, list, triggers * 48u);
            for (WORD i = 0; i < triggers; i++)
                loaded.triggers[i].Reserved1 = i;
            loaded.trigger_count = triggers;
        }
    }
    job_clear(job);
    job->uuid = loaded.uuid;
    job->retry_count = loaded.retry_count;
    job->retry_interval = loaded.retry_interval;
    job->idle_deadline = loaded.idle_deadline;
    job->idle_wait = loaded.idle_wait;
    job->priority = loaded.priority;
    job->max_run = loaded.max_run;
    job->exit_code = loaded.exit_code;
    job->status = loaded.status;
    job->flags = loaded.flags;
    job->last_run = loaded.last_run;
    job->running = loaded.running;
    job->exit_result = loaded.exit_result;
    job->task_flags = loaded.task_flags;
    memcpy(job->strings, loaded.strings, sizeof(job->strings));
    job->data = loaded.data;
    job->data_size = loaded.data_size;
    job->triggers = loaded.triggers;
    job->trigger_count = loaded.trigger_count;
    return S_OK;
}

static HRESULT job_load_file(JOB *job, LPCWSTR path)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return MstaskFailure();
    DWORD size = GetFileSize(file, nullptr), read = 0;
    BYTE *data = size != INVALID_FILE_SIZE && size < 0x400000 ? allocate(size) : nullptr;
    HRESULT result = data ? S_OK : SCHED_E_INVALID_TASK;
    if (data && (!ReadFile(file, data, size, &read, nullptr) || read != size))
        result = MstaskFailure();
    CloseHandle(file);
    if (SUCCEEDED(result))
        result = job_parse(job, data, size);
    release_memory(data);
    if (SUCCEEDED(result)) {
        LPWSTR copy = local_duplicate(path);
        if (!copy)
            return E_OUTOFMEMORY;
        release_memory(job->path);
        job->path = copy;
        job->on_disk = TRUE;
        job->dirty = FALSE;
    }
    return result;
}

static SIZE_T job_serialize(JOB *job, BYTE **out)
{
    SIZE_T names = 0;
    WORD lengths[STRING_COUNT];
    for (int i = 0; i < STRING_COUNT; i++) {
        lengths[i] = job->strings[i] && *job->strings[i] ? (WORD)(wcslen(job->strings[i]) + 1) : 0;
        names += 2 + lengths[i] * 2u;
    }
    SIZE_T trigger_at = FIXED_SIZE + 2 + names + 2 + job->data_size + 2 + 8;
    SIZE_T total = trigger_at + 2 + job->trigger_count * 48u;
    BYTE *data = allocate(total);
    if (!data)
        return 0;
    WORD product = PRODUCT_VERSION, version = FILE_VERSION;
    memcpy(data, &product, 2);
    memcpy(data + 2, &version, 2);
    memcpy(data + 4, &job->uuid, 16);
    WORD name_offset = FIXED_SIZE + 2, trigger_offset = (WORD)trigger_at;
    memcpy(data + 20, &name_offset, 2);
    memcpy(data + 22, &trigger_offset, 2);
    memcpy(data + 24, &job->retry_count, 2);
    memcpy(data + 26, &job->retry_interval, 2);
    memcpy(data + 28, &job->idle_deadline, 2);
    memcpy(data + 30, &job->idle_wait, 2);
    memcpy(data + 32, &job->priority, 4);
    memcpy(data + 36, &job->max_run, 4);
    memcpy(data + 40, &job->exit_code, 4);
    DWORD status = (DWORD)job_status(job), flags = job->flags & FLAG_FILE_MASK;
    memcpy(data + 44, &status, 4);
    memcpy(data + 48, &flags, 4);
    memcpy(data + 52, &job->last_run, 16);
    memcpy(data + FIXED_SIZE, &job->running, 2);
    SIZE_T at = FIXED_SIZE + 2;
    for (int i = 0; i < STRING_COUNT; i++) {
        memcpy(data + at, &lengths[i], 2);
        memcpy(data + at + 2, job->strings[i], lengths[i] * 2u);
        at += 2 + lengths[i] * 2u;
    }
    memcpy(data + at, &job->data_size, 2);
    if (job->data_size)
        memcpy(data + at + 2, job->data, job->data_size);
    at += 2 + job->data_size;
    WORD reserved_size = 8;
    memcpy(data + at, &reserved_size, 2);
    memcpy(data + at + 2, &job->exit_result, 4);
    memcpy(data + at + 6, &job->task_flags, 4);
    at += 2 + 8;
    memcpy(data + at, &job->trigger_count, 2);
    for (WORD i = 0; i < job->trigger_count; i++) {
        TASK_TRIGGER trigger = job->triggers[i];
        trigger.Reserved1 = 0;
        memcpy(data + at + 2 + i * 48u, &trigger, 48);
    }
    *out = data;
    return total;
}

static void job_new_id(JOB *job)
{
    CoCreateGuid(&job->uuid);
}

/* saves the task to its own file or to another one. A file that is not the own file must not exist. */
static HRESULT job_save(JOB *job, LPCWSTR file, BOOL remember)
{
    LPCWSTR target = file ? file : job->path;
    if (!target || !*target)
        return E_INVALIDARG;
    BOOL same = job->path && !_wcsicmp(target, job->path);
    BOOL create = !(same && job->on_disk);
    if (!job->strings[STRING_CREATOR]) {
        WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1], user[257], name[MAX_COMPUTERNAME_LENGTH + 260];
        DWORD computer_size = RTL_NUMBER_OF(computer), user_size = RTL_NUMBER_OF(user);
        if (GetComputerNameW(computer, &computer_size) && GetUserNameW(user, &user_size)) {
            _snwprintf(name, RTL_NUMBER_OF(name), L"%s\\%s", computer, user);
            job->strings[STRING_CREATOR] = local_duplicate(name);
        }
    }
    if (job->uuid.Data1 == 0 && job->uuid.Data2 == 0)
        job_new_id(job);
    BYTE *data;
    SIZE_T size = job_serialize(job, &data);
    if (!size)
        return E_OUTOFMEMORY;
    DWORD attributes = (job->flags & TASK_FLAG_HIDDEN) ? FILE_ATTRIBUTE_HIDDEN : FILE_ATTRIBUTE_NORMAL;
    HANDLE handle = CreateFileW(target, GENERIC_WRITE, FILE_SHARE_READ, nullptr, create ? CREATE_NEW : TRUNCATE_EXISTING, attributes, nullptr);
    HRESULT result = S_OK;
    if (handle == INVALID_HANDLE_VALUE) {
        result = MstaskFailure();
    } else {
        DWORD written;
        if (!WriteFile(handle, data, (DWORD)size, &written, nullptr) || written != size)
            result = MstaskFailure();
        CloseHandle(handle);
        if (FAILED(result))
            DeleteFileW(target);
    }
    release_memory(data);
    if (FAILED(result))
        return result;
    if (job->account_set) {
        account_update(target, job->account);
        job->account_set = FALSE;
    }
    if (remember || same) {
        LPWSTR copy = same ? job->path : local_duplicate(target);
        if (!copy)
            return E_OUTOFMEMORY;
        if (!same) {
            release_memory(job->path);
            job->path = copy;
        }
        job->on_disk = TRUE;
        job->dirty = FALSE;
    } else if (!file) {
        job->dirty = FALSE;
    }
    job->status = (DWORD)job_status(job);
    return S_OK;
}

/* ------------------------------------------------------------------ the task */

static void job_add_ref(JOB *job) { InterlockedIncrement(&job->refs); }

static ULONG job_release(JOB *job)
{
    LONG refs = InterlockedDecrement(&job->refs);
    if (!refs) {
        job_clear(job);
        release_memory(job->path);
        release_memory(job->account);
        release_memory(job);
        InterlockedDecrement(&g_mstask_objects);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE task_query_interface(ITask *This, REFIID riid, void **object)
{
    JOB *job = job_of_task(This);
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITask) || IsEqualIID(riid, &IID_IScheduledWorkItem))
        *object = &job->task;
    else if (IsEqualIID(riid, &IID_IPersist) || IsEqualIID(riid, &IID_IPersistFile))
        *object = &job->persist;
    else
        return E_NOINTERFACE;
    job_add_ref(job);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE task_add_ref(ITask *This)
{
    return InterlockedIncrement(&job_of_task(This)->refs);
}

static ULONG STDMETHODCALLTYPE task_release(ITask *This)
{
    return job_release(job_of_task(This));
}

static HRESULT STDMETHODCALLTYPE task_create_trigger(ITask *This, WORD *index, ITaskTrigger **out)
{
    JOB *job = job_of_task(This);
    if (!index || !out)
        return E_INVALIDARG;
    *out = nullptr;
    if (job->trigger_count == 0xFFFF)
        return E_OUTOFMEMORY;
    TASK_TRIGGER *list = allocate((job->trigger_count + 1u) * sizeof(TASK_TRIGGER));
    if (!list)
        return E_OUTOFMEMORY;
    if (job->trigger_count)
        memcpy(list, job->triggers, job->trigger_count * sizeof(TASK_TRIGGER));
    SYSTEMTIME now;
    GetLocalTime(&now);
    TASK_TRIGGER *fresh = &list[job->trigger_count];
    fresh->cbTriggerSize = sizeof(TASK_TRIGGER);
    fresh->Reserved1 = job->trigger_count;
    fresh->wBeginYear = now.wYear;
    fresh->wBeginMonth = now.wMonth;
    fresh->wBeginDay = now.wDay;
    fresh->wStartHour = now.wHour;
    fresh->wStartMinute = now.wMinute;
    fresh->rgFlags = TRIGGER_NOT_SET;
    fresh->TriggerType = TASK_TIME_TRIGGER_DAILY;
    fresh->Type.Daily.DaysInterval = 1;
    ITaskTrigger *trigger;
    HRESULT result = trigger_create(job, job->trigger_count, &trigger);
    if (FAILED(result)) {
        release_memory(list);
        return result;
    }
    release_memory(job->triggers);
    job->triggers = list;
    *index = job->trigger_count++;
    job->dirty = TRUE;
    *out = trigger;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE task_delete_trigger(ITask *This, WORD index)
{
    JOB *job = job_of_task(This);
    if (!find_trigger(job, index))
        return SCHED_E_TRIGGER_NOT_FOUND;
    WORD count = --job->trigger_count;
    if (index != count)
        memmove(&job->triggers[index], &job->triggers[index + 1], (count - index) * sizeof(TASK_TRIGGER));
    for (WORD i = index; i < count; i++)
        job->triggers[i].Reserved1--;
    job->dirty = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE task_get_trigger_count(ITask *This, WORD *count)
{
    if (!count)
        return E_INVALIDARG;
    *count = job_of_task(This)->trigger_count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE task_get_trigger(ITask *This, WORD index, ITaskTrigger **out)
{
    JOB *job = job_of_task(This);
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!find_trigger(job, index))
        return SCHED_E_TRIGGER_NOT_FOUND;
    return trigger_create(job, index, out);
}

static HRESULT STDMETHODCALLTYPE task_get_trigger_string(ITask *This, WORD index, LPWSTR *text)
{
    return job_trigger_string(job_of_task(This), index, text);
}

/* the earliest run of any trigger at or after a tick, and whether any trigger has runs at all. */
static ULONGLONG job_next_run(const JOB *job, ULONGLONG from, BOOL *events)
{
    ULONGLONG best = 0;
    for (WORD i = 0; i < job->trigger_count; i++) {
        const TASK_TRIGGER *trigger = &job->triggers[i];
        if (trigger->TriggerType >= TASK_EVENT_TRIGGER_ON_IDLE && !(trigger->rgFlags & TASK_TRIGGER_FLAG_DISABLED))
            *events = TRUE;
        ULONGLONG run = next_run(trigger, from);
        if (run && (!best || run < best))
            best = run;
    }
    return best;
}

static HRESULT STDMETHODCALLTYPE task_get_run_times(ITask *This, const SYSTEMTIME *begin, const SYSTEMTIME *end, WORD *count,
                                                    SYSTEMTIME **times)
{
    JOB *job = job_of_task(This);
    if (!begin || !count || !times)
        return E_INVALIDARG;
    WORD wanted = *count;
    *count = 0;
    *times = nullptr;
    if (!end && !wanted)
        return E_INVALIDARG;
    ULONGLONG from = ticks_of(begin), until = end ? ticks_of(end) : ~0ULL;
    if (end && until < from)
        return E_INVALIDARG;
    HRESULT status = job_status(job);
    if (status == SCHED_S_TASK_DISABLED || status == SCHED_S_TASK_NOT_SCHEDULED || status == SCHED_S_TASK_NO_VALID_TRIGGERS)
        return status;
    SYSTEMTIME *list = CoTaskMemAlloc((wanted ? wanted : 1) * sizeof(SYSTEMTIME));
    if (!list)
        return E_OUTOFMEMORY;
    WORD found = 0;
    BOOL events = FALSE;
    while (!wanted || found < wanted) {
        ULONGLONG run = job_next_run(job, from, &events);
        if (!run || run >= until)
            break;
        if (!wanted)
            break;
        time_of(run, &list[found++]);
        from = run + TICKS_PER_MINUTE;
    }
    *count = found;
    if (!found) {
        CoTaskMemFree(list);
        return events ? SCHED_S_EVENT_TRIGGER : S_OK;
    }
    *times = list;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE task_get_next_run_time(ITask *This, SYSTEMTIME *next)
{
    JOB *job = job_of_task(This);
    if (!next)
        return E_INVALIDARG;
    memset(next, 0, sizeof(*next));
    HRESULT status = job_status(job);
    if (status == SCHED_S_TASK_DISABLED || status == SCHED_S_TASK_NOT_SCHEDULED || status == SCHED_S_TASK_NO_VALID_TRIGGERS)
        return status;
    SYSTEMTIME now;
    GetLocalTime(&now);
    BOOL events = FALSE;
    ULONGLONG run = job_next_run(job, ticks_of(&now), &events);
    if (!run)
        return events ? SCHED_S_EVENT_TRIGGER : SCHED_S_TASK_NO_MORE_RUNS;
    time_of(run, next);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE task_set_idle_wait(ITask *This, WORD idle, WORD deadline)
{
    JOB *job = job_of_task(This);
    job->idle_wait = idle;
    job->idle_deadline = deadline;
    job->dirty = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE task_get_idle_wait(ITask *This, WORD *idle, WORD *deadline)
{
    JOB *job = job_of_task(This);
    if (!idle || !deadline)
        return E_INVALIDARG;
    *idle = job->idle_wait;
    *deadline = job->idle_deadline;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE task_run(ITask *This)
{
    JOB *job = job_of_task(This);
    if (!job->on_disk)
        return SCHED_E_TASK_NOT_READY;
    return SCHED_E_SERVICE_NOT_RUNNING;
}

static HRESULT STDMETHODCALLTYPE task_terminate(ITask *This)
{
    return job_of_task(This)->running ? SCHED_E_SERVICE_NOT_RUNNING : SCHED_E_TASK_NOT_RUNNING;
}

static HRESULT STDMETHODCALLTYPE task_edit_work_item(ITask *This, HWND parent, DWORD reserved)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE task_get_most_recent_run_time(ITask *This, SYSTEMTIME *last)
{
    JOB *job = job_of_task(This);
    if (!last)
        return E_INVALIDARG;
    *last = job->last_run;
    return job->last_run.wYear ? S_OK : SCHED_S_TASK_HAS_NOT_RUN;
}

static HRESULT STDMETHODCALLTYPE task_get_status(ITask *This, HRESULT *status)
{
    if (!status)
        return E_INVALIDARG;
    *status = job_status(job_of_task(This));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE task_get_exit_code(ITask *This, DWORD *code)
{
    JOB *job = job_of_task(This);
    if (!code)
        return E_INVALIDARG;
    *code = job->exit_code;
    return job->last_run.wYear ? job->exit_result : SCHED_S_TASK_HAS_NOT_RUN;
}

static HRESULT set_string(JOB *job, int which, LPCWSTR text)
{
    LPWSTR copy = nullptr;
    if (text && *text) {
        copy = local_duplicate(text);
        if (!copy)
            return E_OUTOFMEMORY;
    }
    release_memory(job->strings[which]);
    job->strings[which] = copy;
    job->dirty = TRUE;
    return S_OK;
}

static HRESULT get_string(JOB *job, int which, LPWSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    *out = MstaskDuplicate(job->strings[which] ? job->strings[which] : L"");
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE task_set_comment(ITask *This, LPCWSTR text)
{
    return set_string(job_of_task(This), STRING_COMMENT, text);
}

static HRESULT STDMETHODCALLTYPE task_get_comment(ITask *This, LPWSTR *text)
{
    return get_string(job_of_task(This), STRING_COMMENT, text);
}

static HRESULT STDMETHODCALLTYPE task_set_creator(ITask *This, LPCWSTR text)
{
    return set_string(job_of_task(This), STRING_CREATOR, text);
}

static HRESULT STDMETHODCALLTYPE task_get_creator(ITask *This, LPWSTR *text)
{
    return get_string(job_of_task(This), STRING_CREATOR, text);
}

static HRESULT STDMETHODCALLTYPE task_set_work_item_data(ITask *This, WORD size, BYTE *data)
{
    JOB *job = job_of_task(This);
    if (size && !data)
        return E_INVALIDARG;
    BYTE *copy = nullptr;
    if (size) {
        copy = allocate(size);
        if (!copy)
            return E_OUTOFMEMORY;
        memcpy(copy, data, size);
    }
    release_memory(job->data);
    job->data = copy;
    job->data_size = size;
    job->dirty = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE task_get_work_item_data(ITask *This, WORD *size, BYTE **data)
{
    JOB *job = job_of_task(This);
    if (!size || !data)
        return E_INVALIDARG;
    *size = 0;
    *data = nullptr;
    if (!job->data_size)
        return S_OK;
    BYTE *copy = CoTaskMemAlloc(job->data_size);
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, job->data, job->data_size);
    *size = job->data_size;
    *data = copy;
    return S_OK;
}

/* the retry settings are kept, but the service ignores them, so the calls say they are not implemented. */
static HRESULT STDMETHODCALLTYPE task_set_error_retry_count(ITask *This, WORD count)
{
    job_of_task(This)->retry_count = count;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE task_get_error_retry_count(ITask *This, WORD *count)
{
    if (!count)
        return E_INVALIDARG;
    *count = job_of_task(This)->retry_count;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE task_set_error_retry_interval(ITask *This, WORD minutes)
{
    job_of_task(This)->retry_interval = minutes;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE task_get_error_retry_interval(ITask *This, WORD *minutes)
{
    if (!minutes)
        return E_INVALIDARG;
    *minutes = job_of_task(This)->retry_interval;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE task_set_flags(ITask *This, DWORD flags)
{
    JOB *job = job_of_task(This);
    job->flags = (job->flags & ~(DWORD)FLAG_USER_MASK) | (flags & FLAG_USER_MASK);
    job->dirty = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE task_get_flags(ITask *This, DWORD *flags)
{
    if (!flags)
        return E_INVALIDARG;
    *flags = job_of_task(This)->flags & (FLAG_USER_MASK | 0x200000);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE task_set_account_information(ITask *This, LPCWSTR account, LPCWSTR password)
{
    JOB *job = job_of_task(This);
    if (!account)
        return E_INVALIDARG;
    LPWSTR copy = local_duplicate(account);
    if (!copy)
        return E_OUTOFMEMORY;
    release_memory(job->account);
    job->account = copy;
    job->account_set = TRUE;
    job->dirty = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE task_get_account_information(ITask *This, LPWSTR *account)
{
    JOB *job = job_of_task(This);
    if (!account)
        return E_INVALIDARG;
    *account = nullptr;
    if (!job->path)
        return E_UNEXPECTED;
    LPWSTR name = job->account_set ? MstaskDuplicate(job->account) : account_lookup(job->path);
    if (!name)
        return SCHED_E_ACCOUNT_INFORMATION_NOT_SET;
    *account = name;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE task_set_application_name(ITask *This, LPCWSTR name)
{
    WCHAR trimmed[MAX_PATH * 2];
    if (!name)
        return E_INVALIDARG;
    while (*name == L' ')
        name++;
    SIZE_T length = wcslen(name);
    while (length && name[length - 1] == L' ')
        length--;
    if (length >= RTL_NUMBER_OF(trimmed))
        return E_INVALIDARG;
    memcpy(trimmed, name, length * sizeof(WCHAR));
    trimmed[length] = 0;
    return set_string(job_of_task(This), STRING_APPLICATION, trimmed);
}

static HRESULT STDMETHODCALLTYPE task_get_application_name(ITask *This, LPWSTR *name)
{
    return get_string(job_of_task(This), STRING_APPLICATION, name);
}

static HRESULT STDMETHODCALLTYPE task_set_parameters(ITask *This, LPCWSTR parameters)
{
    return set_string(job_of_task(This), STRING_PARAMETERS, parameters);
}

static HRESULT STDMETHODCALLTYPE task_get_parameters(ITask *This, LPWSTR *parameters)
{
    return get_string(job_of_task(This), STRING_PARAMETERS, parameters);
}

static HRESULT STDMETHODCALLTYPE task_set_working_directory(ITask *This, LPCWSTR directory)
{
    return set_string(job_of_task(This), STRING_DIRECTORY, directory);
}

static HRESULT STDMETHODCALLTYPE task_get_working_directory(ITask *This, LPWSTR *directory)
{
    return get_string(job_of_task(This), STRING_DIRECTORY, directory);
}

static HRESULT STDMETHODCALLTYPE task_set_priority(ITask *This, DWORD priority)
{
    if (priority != IDLE_PRIORITY_CLASS && priority != NORMAL_PRIORITY_CLASS && priority != HIGH_PRIORITY_CLASS &&
        priority != REALTIME_PRIORITY_CLASS)
        return E_INVALIDARG;
    job_of_task(This)->priority = priority;
    job_of_task(This)->dirty = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE task_get_priority(ITask *This, DWORD *priority)
{
    if (!priority)
        return E_INVALIDARG;
    *priority = job_of_task(This)->priority;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE task_set_task_flags(ITask *This, DWORD flags)
{
    JOB *job = job_of_task(This);
    job->task_flags = (job->task_flags & ~(DWORD)FLAG_USER_MASK) | (flags & FLAG_USER_MASK);
    job->dirty = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE task_get_task_flags(ITask *This, DWORD *flags)
{
    if (!flags)
        return E_INVALIDARG;
    *flags = job_of_task(This)->task_flags & FLAG_USER_MASK;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE task_set_max_run_time(ITask *This, DWORD milliseconds)
{
    job_of_task(This)->max_run = milliseconds;
    job_of_task(This)->dirty = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE task_get_max_run_time(ITask *This, DWORD *milliseconds)
{
    if (!milliseconds)
        return E_INVALIDARG;
    *milliseconds = job_of_task(This)->max_run;
    return S_OK;
}

static const ITaskVtbl g_task_vtbl = {
    task_query_interface, task_add_ref, task_release, task_create_trigger, task_delete_trigger, task_get_trigger_count,
    task_get_trigger, task_get_trigger_string, task_get_run_times, task_get_next_run_time, task_set_idle_wait,
    task_get_idle_wait, task_run, task_terminate, task_edit_work_item, task_get_most_recent_run_time, task_get_status,
    task_get_exit_code, task_set_comment, task_get_comment, task_set_creator, task_get_creator,
    task_set_work_item_data, task_get_work_item_data, task_set_error_retry_count, task_get_error_retry_count,
    task_set_error_retry_interval, task_get_error_retry_interval, task_set_flags, task_get_flags,
    task_set_account_information, task_get_account_information, task_set_application_name, task_get_application_name,
    task_set_parameters, task_get_parameters, task_set_working_directory, task_get_working_directory,
    task_set_priority, task_get_priority, task_set_task_flags, task_get_task_flags, task_set_max_run_time,
    task_get_max_run_time,
};

/* ------------------------------------------------------------------ the file interface */

static HRESULT STDMETHODCALLTYPE persist_query_interface(IPersistFile *This, REFIID riid, void **object)
{
    return task_query_interface(&job_of_persist(This)->task, riid, object);
}

static ULONG STDMETHODCALLTYPE persist_add_ref(IPersistFile *This)
{
    return InterlockedIncrement(&job_of_persist(This)->refs);
}

static ULONG STDMETHODCALLTYPE persist_release(IPersistFile *This)
{
    return job_release(job_of_persist(This));
}

static HRESULT STDMETHODCALLTYPE persist_get_class_id(IPersistFile *This, CLSID *clsid)
{
    if (!clsid)
        return E_INVALIDARG;
    *clsid = CLSID_CTask;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE persist_is_dirty(IPersistFile *This)
{
    return job_of_persist(This)->dirty ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE persist_load(IPersistFile *This, LPCOLESTR file, DWORD mode)
{
    if (!file || !*file)
        return E_INVALIDARG;
    return job_load_file(job_of_persist(This), file);
}

static HRESULT STDMETHODCALLTYPE persist_save(IPersistFile *This, LPCOLESTR file, BOOL remember)
{
    return job_save(job_of_persist(This), file, remember);
}

static HRESULT STDMETHODCALLTYPE persist_save_completed(IPersistFile *This, LPCOLESTR file)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE persist_get_cur_file(IPersistFile *This, LPOLESTR *file)
{
    JOB *job = job_of_persist(This);
    if (!file)
        return E_INVALIDARG;
    BOOL named = job->path && *job->path;
    *file = MstaskDuplicate(named ? job->path : L"*.job");
    if (!*file)
        return E_OUTOFMEMORY;
    return named ? S_OK : S_FALSE;
}

static const IPersistFileVtbl g_persist_vtbl = {
    persist_query_interface, persist_add_ref, persist_release, persist_get_class_id, persist_is_dirty, persist_load,
    persist_save, persist_save_completed, persist_get_cur_file,
};

static HRESULT job_new(LPCWSTR path, JOB **out)
{
    JOB *job = allocate(sizeof(*job));
    if (!job)
        return E_OUTOFMEMORY;
    job->task.lpVtbl = &g_task_vtbl;
    job->persist.lpVtbl = &g_persist_vtbl;
    job->refs = 1;
    job->idle_wait = 10;
    job->idle_deadline = 60;
    job->priority = NORMAL_PRIORITY_CLASS;
    job->max_run = 3 * 24 * 60 * 60 * 1000;
    job->status = SCHED_S_TASK_NOT_SCHEDULED;
    job->flags = FLAG_BASE;
    job->exit_result = SCHED_S_TASK_HAS_NOT_RUN;
    if (path) {
        job->path = local_duplicate(path);
        if (!job->path) {
            release_memory(job);
            return E_OUTOFMEMORY;
        }
    }
    InterlockedIncrement(&g_mstask_objects);
    *out = job;
    return S_OK;
}

/* a task that is not in a file yet, to be saved to the path. */
HRESULT JobCreate(LPCWSTR path, REFIID riid, void **object)
{
    JOB *job;
    HRESULT result = job_new(path, &job);
    if (FAILED(result))
        return result;
    result = job->task.lpVtbl->QueryInterface(&job->task, riid, object);
    job_release(job);
    return result;
}

/* the task in a file. */
HRESULT JobLoad(LPCWSTR path, REFIID riid, void **object)
{
    JOB *job;
    HRESULT result = job_new(nullptr, &job);
    if (FAILED(result))
        return result;
    result = job_load_file(job, path);
    if (SUCCEEDED(result))
        result = job->task.lpVtbl->QueryInterface(&job->task, riid, object);
    job_release(job);
    return result;
}
