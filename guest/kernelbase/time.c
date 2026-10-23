/* tick counts, performance counters, system and local time, time zones and conversions. */
#include "kernelbase_private.h"

#define TICKS_PER_MS 10000ll
#define TICKS_PER_MINUTE (60ll * 10000000)

static ULONGLONG read_system_time(volatile KSYSTEM_TIME *t)
{
    LONG high;
    ULONG low;
    do {
        high = t->High1Time;
        low = t->LowPart;
    } while (high != t->High2Time);
    return ((ULONGLONG)(ULONG)high << 32) | low;
}

/* ------------------------------------------------------------------ counters */

ULONGLONG WINAPI GetTickCount64(void)
{
    volatile KUSER_SHARED_DATA *k = USER_SHARED_DATA;
    ULONGLONG ticks = read_system_time(&k->TickCount);
    return (ticks >> 24) * k->TickCountMultiplier +
           (((ticks & 0xFFFFFF) * k->TickCountMultiplier) >> 24);
}

DWORD WINAPI GetTickCount(void)
{
    return (DWORD)GetTickCount64();
}

BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *counter)
{
    if (!counter) {
        KbSetError(ERROR_NOACCESS);
        return FALSE;
    }
    return RtlQueryPerformanceCounter(counter);
}

BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *frequency)
{
    if (!frequency) {
        KbSetError(ERROR_NOACCESS);
        return FALSE;
    }
    return RtlQueryPerformanceFrequency(frequency);
}

void WINAPI QueryInterruptTime(PULONGLONG time)
{
    *time = read_system_time(&USER_SHARED_DATA->InterruptTime);
}

BOOL WINAPI QueryUnbiasedInterruptTime(PULONGLONG time)
{
    if (!time) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *time = read_system_time(&USER_SHARED_DATA->InterruptTime) - USER_SHARED_DATA->InterruptTimeBias;
    return TRUE;
}

static ULONGLONG precise_interrupt_time(void)
{
    return MimePreciseTime(FALSE);
}

void WINAPI QueryInterruptTimePrecise(PULONGLONG time)
{
    *time = precise_interrupt_time();
}

void WINAPI QueryUnbiasedInterruptTimePrecise(PULONGLONG time)
{
    *time = precise_interrupt_time() - USER_SHARED_DATA->InterruptTimeBias;
}

/* ------------------------------------------------------------------ system and local time */

static void store_filetime(LPFILETIME ft, LONGLONG v)
{
    ft->dwLowDateTime = (DWORD)v;
    ft->dwHighDateTime = (DWORD)((ULONGLONG)v >> 32);
}

static LONGLONG load_filetime(const FILETIME *ft)
{
    return (LONGLONG)(((ULONGLONG)ft->dwHighDateTime << 32) | ft->dwLowDateTime);
}

void WINAPI GetSystemTimeAsFileTime(LPFILETIME time)
{
    store_filetime(time, (LONGLONG)read_system_time(&USER_SHARED_DATA->SystemTime));
}

void WINAPI GetSystemTimeAsFileTimeRaw(LPFILETIME time)
{
    GetSystemTimeAsFileTime(time);
}

void WINAPI GetSystemTimePreciseAsFileTime(LPFILETIME time)
{
    store_filetime(time, RtlGetSystemTimePrecise());
}

LONG KbTimeZoneBiasMinutes(void)
{
    LONGLONG bias = (LONGLONG)read_system_time(&USER_SHARED_DATA->TimeZoneBias);
    return (LONG)(bias / TICKS_PER_MINUTE);
}

static BOOL is_leap(int year);
static int days_in_month(int year, int month);

void KbFileTimeToSystemTime(LONGLONG t, LPSYSTEMTIME st)
{
    LONGLONG days = t / (86400ll * 10000000);
    LONGLONG rest = t % (86400ll * 10000000);
    st->wDayOfWeek = (WORD)((days + 1) % 7); /* 1601-01-01 was a monday. */
    /* 1601 starts a 400 year cycle of 146097 days. */
    LONGLONG cycles = days / 146097;
    days %= 146097;
    LONGLONG centuries = days / 36524;
    if (centuries == 4)
        centuries = 3;
    days -= centuries * 36524;
    LONGLONG quads = days / 1461;
    days %= 1461;
    LONGLONG years = days / 365;
    if (years == 4)
        years = 3;
    days -= years * 365;
    int year = (int)(1601 + cycles * 400 + centuries * 100 + quads * 4 + years);
    int month = 1;
    while (days >= days_in_month(year, month)) {
        days -= days_in_month(year, month);
        month++;
    }
    st->wYear = (WORD)year;
    st->wMonth = (WORD)month;
    st->wDay = (WORD)(days + 1);
    st->wHour = (WORD)(rest / (3600ll * 10000000));
    rest %= 3600ll * 10000000;
    st->wMinute = (WORD)(rest / (60ll * 10000000));
    rest %= 60ll * 10000000;
    st->wSecond = (WORD)(rest / 10000000);
    st->wMilliseconds = (WORD)(rest % 10000000 / TICKS_PER_MS);
}

static const UCHAR g_month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static BOOL is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int days_in_month(int year, int month)
{
    return month == 2 && is_leap(year) ? 29 : g_month_days[month - 1];
}

static LONGLONG days_since_1601(int year, int month, int day)
{
    LONGLONG y = year - 1601;
    LONGLONG days = y * 365 + y / 4 - y / 100 + y / 400;
    for (int m = 1; m < month; m++)
        days += days_in_month(year, m);
    return days + day - 1;
}

BOOL KbSystemTimeToFileTime(const SYSTEMTIME *st, LONGLONG *t)
{
    if (st->wYear < 1601 || st->wYear > 30827 || st->wMonth < 1 || st->wMonth > 12 || st->wDay < 1 ||
        st->wDay > days_in_month(st->wYear, st->wMonth) || st->wHour > 23 || st->wMinute > 59 || st->wSecond > 59 ||
        st->wMilliseconds > 999)
        return FALSE;
    LONGLONG days = days_since_1601(st->wYear, st->wMonth, st->wDay);
    LONGLONG secs = days * 86400 + st->wHour * 3600 + st->wMinute * 60 + st->wSecond;
    *t = secs * 10000000 + (LONGLONG)st->wMilliseconds * TICKS_PER_MS;
    return TRUE;
}

void WINAPI GetSystemTime(LPSYSTEMTIME time)
{
    KbFileTimeToSystemTime((LONGLONG)read_system_time(&USER_SHARED_DATA->SystemTime), time);
}

void WINAPI GetLocalTime(LPSYSTEMTIME time)
{
    LARGE_INTEGER system, local;
    system.QuadPart = (LONGLONG)read_system_time(&USER_SHARED_DATA->SystemTime);
    RtlSystemTimeToLocalTime(&system, &local);
    KbFileTimeToSystemTime(local.QuadPart, time);
}

static BOOL refuse_time_change(const SYSTEMTIME *time)
{
    LONGLONG t;
    if (!time || !KbSystemTimeToFileTime(time, &t)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    KbSetError(ERROR_ACCESS_DENIED);
    return FALSE;
}

BOOL WINAPI SetSystemTime(const SYSTEMTIME *time)
{
    return refuse_time_change(time);
}

BOOL WINAPI SetLocalTime(const SYSTEMTIME *time)
{
    return refuse_time_change(time);
}

BOOL WINAPI FileTimeToSystemTime(const FILETIME *file_time, LPSYSTEMTIME system_time)
{
    LONGLONG t = load_filetime(file_time);
    if (t < 0) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    KbFileTimeToSystemTime(t, system_time);
    return TRUE;
}

BOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME *system_time, LPFILETIME file_time)
{
    LONGLONG t;
    if (!KbSystemTimeToFileTime(system_time, &t)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    store_filetime(file_time, t);
    return TRUE;
}

BOOL WINAPI FileTimeToLocalFileTime(const FILETIME *file_time, LPFILETIME local)
{
    LARGE_INTEGER s, l;
    s.QuadPart = load_filetime(file_time);
    RtlSystemTimeToLocalTime(&s, &l);
    store_filetime(local, l.QuadPart);
    return TRUE;
}

BOOL WINAPI LocalFileTimeToFileTime(const FILETIME *local, LPFILETIME file_time)
{
    LARGE_INTEGER s, l;
    l.QuadPart = load_filetime(local);
    RtlLocalTimeToSystemTime(&l, &s);
    store_filetime(file_time, s.QuadPart);
    return TRUE;
}

LONG WINAPI CompareFileTime(const FILETIME *a, const FILETIME *b)
{
    if (!a || !b)
        return -1;
    ULONGLONG x = (ULONGLONG)load_filetime(a), y = (ULONGLONG)load_filetime(b);
    return x < y ? -1 : x > y ? 1 : 0;
}

/* ------------------------------------------------------------------ time zones */

#define TZ_CONTROL L"SYSTEM\\CurrentControlSet\\Control\\TimeZoneInformation"
#define TZ_ZONES L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\"

/* the TZI registry value of a zone key. */
typedef struct _TZI {
    LONG Bias, StandardBias, DaylightBias;
    SYSTEMTIME StandardDate, DaylightDate;
} TZI;

static BOOL query_binary(HKEY key, LPCWSTR name, void *out, DWORD size)
{
    DWORD type = 0, got = size;
    return !RegQueryValueExW(key, name, nullptr, &type, (LPBYTE)out, &got) && type == REG_BINARY && got == size;
}

static BOOL query_string(HKEY key, LPCWSTR name, WCHAR *out, DWORD chars)
{
    DWORD type = 0, got = (chars - 1) * sizeof(WCHAR);
    out[0] = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, (LPBYTE)out, &got) || (type != REG_SZ && type != REG_EXPAND_SZ))
        return FALSE;
    out[got / sizeof(WCHAR)] = 0;
    return out[0] != 0;
}

/* always terminates dst. */
static void copy_chars(WCHAR *dst, LPCWSTR src, SIZE_T chars)
{
    SIZE_T n = 0;
    for (; n + 1 < chars && src[n]; n++)
        dst[n] = src[n];
    dst[n] = 0;
}

static void zone_from_rules(LPTIME_ZONE_INFORMATION info, const TZI *rules)
{
    info->Bias = rules->Bias;
    info->StandardBias = rules->StandardBias;
    info->DaylightBias = rules->DaylightBias;
    info->StandardDate = rules->StandardDate;
    info->DaylightDate = rules->DaylightDate;
}

/* reads the machine zone from the registry like windows keeps it. without one, falls back to the clock's offset. */
static void current_zone(LPTIME_ZONE_INFORMATION info, WCHAR *key_name, DWORD key_chars, BOOL *dynamic_disabled)
{
    memset(info, 0, sizeof(*info));
    if (key_name)
        key_name[0] = 0;
    if (dynamic_disabled)
        *dynamic_disabled = FALSE;
    HKEY key;
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, TZ_CONTROL, 0, KEY_QUERY_VALUE, &key)) {
        DWORD value, size = sizeof(value), type = 0;
        BOOL found = !RegQueryValueExW(key, L"Bias", nullptr, &type, (LPBYTE)&value, &size) && type == REG_DWORD;
        if (found) {
            info->Bias = (LONG)value;
            size = sizeof(value);
            if (!RegQueryValueExW(key, L"StandardBias", nullptr, &type, (LPBYTE)&value, &size) && type == REG_DWORD)
                info->StandardBias = (LONG)value;
            size = sizeof(value);
            if (!RegQueryValueExW(key, L"DaylightBias", nullptr, &type, (LPBYTE)&value, &size) && type == REG_DWORD)
                info->DaylightBias = (LONG)value;
            query_binary(key, L"StandardStart", &info->StandardDate, sizeof(SYSTEMTIME));
            query_binary(key, L"DaylightStart", &info->DaylightDate, sizeof(SYSTEMTIME));
            query_string(key, L"StandardName", info->StandardName, RTL_NUMBER_OF(info->StandardName));
            query_string(key, L"DaylightName", info->DaylightName, RTL_NUMBER_OF(info->DaylightName));
            if (key_name)
                query_string(key, L"TimeZoneKeyName", key_name, key_chars);
            size = sizeof(value);
            if (dynamic_disabled && !RegQueryValueExW(key, L"DynamicDaylightTimeDisabled", nullptr, &type,
                                                      (LPBYTE)&value, &size) && type == REG_DWORD)
                *dynamic_disabled = value != 0;
            RegCloseKey(key);
            return;
        }
        RegCloseKey(key);
    }
    /* no zone set: use the clock's offset, without daylight time. */
    LONG bias = KbTimeZoneBiasMinutes();
    info->Bias = bias;
    if (!bias) {
        wcscpy(info->StandardName, L"Coordinated Universal Time");
        wcscpy(info->DaylightName, L"Coordinated Universal Time");
        if (key_name)
            copy_chars(key_name, L"UTC", key_chars);
    } else {
        LONG offset = -bias;
        WCHAR sign = offset < 0 ? '-' : '+';
        if (offset < 0)
            offset = -offset;
        KbFormat(info->StandardName, RTL_NUMBER_OF(info->StandardName), L"UTC%c%02d:%02d", sign, offset / 60,
                 offset % 60);
        wcscpy(info->DaylightName, info->StandardName);
    }
}

static void fill_time_zone(LPTIME_ZONE_INFORMATION info)
{
    current_zone(info, nullptr, 0, nullptr);
}

static BOOL in_daylight(const TIME_ZONE_INFORMATION *tz, LONGLONG t, BOOL is_utc);

/* the TIME_ZONE_ID_* value returned alongside the zone. */
static DWORD zone_state(const TIME_ZONE_INFORMATION *tz)
{
    if (!tz->StandardDate.wMonth || !tz->DaylightDate.wMonth)
        return TIME_ZONE_ID_UNKNOWN;
    /* the kernel switches the period at the exact instant, a tick ahead of the shared page's clock. */
    DWORD id = USER_SHARED_DATA->TimeZoneId;
    if (id == TIME_ZONE_ID_STANDARD || id == TIME_ZONE_ID_DAYLIGHT)
        return id;
    ULONGLONG now = read_system_time(&USER_SHARED_DATA->SystemTime);
    return in_daylight(tz, (LONGLONG)now, TRUE) ? TIME_ZONE_ID_DAYLIGHT : TIME_ZONE_ID_STANDARD;
}

DWORD WINAPI GetTimeZoneInformation(LPTIME_ZONE_INFORMATION info)
{
    if (!info) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return TIME_ZONE_ID_INVALID;
    }
    fill_time_zone(info);
    return zone_state(info);
}

BOOL WINAPI SetTimeZoneInformation(const TIME_ZONE_INFORMATION *info)
{
    KbSetError(ERROR_ACCESS_DENIED);
    return FALSE;
}

DWORD WINAPI GetDynamicTimeZoneInformation(PDYNAMIC_TIME_ZONE_INFORMATION info)
{
    if (!info) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return TIME_ZONE_ID_INVALID;
    }
    TIME_ZONE_INFORMATION tz;
    WCHAR key[128];
    BOOL disabled;
    current_zone(&tz, key, RTL_NUMBER_OF(key), &disabled);
    memset(info, 0, sizeof(*info));
    memcpy(info, &tz, sizeof(tz));
    copy_chars(info->TimeZoneKeyName, key[0] ? key : tz.StandardName, RTL_NUMBER_OF(info->TimeZoneKeyName));
    info->DynamicDaylightTimeDisabled = disabled;
    return zone_state(&tz);
}

/*
 * the zone key holds the current rules, and its Dynamic DST subkey one value per year from FirstEntry to LastEntry for
 * years with other rules. years outside that range use the nearest entry.
 */
static BOOL rules_for_year(LPCWSTR key_name, USHORT year, TZI *rules)
{
    WCHAR path[256];
    if (!key_name[0] || wcslen(key_name) + RTL_NUMBER_OF(TZ_ZONES) + 16 > RTL_NUMBER_OF(path))
        return FALSE;
    wcscpy(path, TZ_ZONES);
    wcscat(path, key_name);
    HKEY zone;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE, &zone))
        return FALSE;
    BOOL found = FALSE;
    HKEY dynamic;
    if (!RegOpenKeyExW(zone, L"Dynamic DST", 0, KEY_QUERY_VALUE, &dynamic)) {
        DWORD first = 0, last = 0, size = sizeof(DWORD), type = 0;
        if (!RegQueryValueExW(dynamic, L"FirstEntry", nullptr, &type, (LPBYTE)&first, &size) && type == REG_DWORD) {
            size = sizeof(DWORD);
            if (!RegQueryValueExW(dynamic, L"LastEntry", nullptr, &type, (LPBYTE)&last, &size) && type == REG_DWORD) {
                DWORD use = year < first ? first : year > last ? last : year;
                WCHAR name[8];
                KbFormat(name, RTL_NUMBER_OF(name), L"%lu", use);
                found = query_binary(dynamic, name, rules, sizeof(*rules));
            }
        }
        RegCloseKey(dynamic);
    }
    if (!found)
        found = query_binary(zone, L"TZI", rules, sizeof(*rules));
    RegCloseKey(zone);
    return found;
}

BOOL WINAPI GetTimeZoneInformationForYear(USHORT year, PDYNAMIC_TIME_ZONE_INFORMATION dtzi, LPTIME_ZONE_INFORMATION info)
{
    if (!info) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR key[128];
    if (dtzi) {
        memcpy(info, dtzi, sizeof(*info));
        copy_chars(key, dtzi->TimeZoneKeyName, RTL_NUMBER_OF(key));
    } else {
        current_zone(info, key, RTL_NUMBER_OF(key), nullptr);
    }
    TZI rules;
    if (!(dtzi && dtzi->DynamicDaylightTimeDisabled) && rules_for_year(key, year, &rules))
        zone_from_rules(info, &rules);
    return TRUE;
}

/* returns local file time. */
static LONGLONG transition_time(const SYSTEMTIME *rule, int year)
{
    SYSTEMTIME st = *rule;
    if (rule->wYear) {
        st.wYear = (WORD)year;
    } else {
        /* wDay is the week of the month, 5 meaning the last one. */
        st.wYear = (WORD)year;
        LONGLONG first = days_since_1601(year, rule->wMonth, 1);
        int first_dow = (int)((first + 1) % 7); /* 1601-01-01 was a monday. */
        int day = 1 + (rule->wDayOfWeek - first_dow + 7) % 7 + (rule->wDay - 1) * 7;
        while (day > days_in_month(year, rule->wMonth))
            day -= 7;
        st.wDay = (WORD)day;
    }
    st.wDayOfWeek = 0;
    LONGLONG t = 0;
    KbSystemTimeToFileTime(&st, &t);
    return t;
}

/* t is utc, or local standard time when is_utc is FALSE. */
static BOOL in_daylight(const TIME_ZONE_INFORMATION *tz, LONGLONG t, BOOL is_utc)
{
    if (!tz->DaylightDate.wMonth || !tz->StandardDate.wMonth)
        return FALSE;
    SYSTEMTIME st;
    KbFileTimeToSystemTime(t, &st);
    int year = st.wYear;
    LONGLONG dst_start = transition_time(&tz->DaylightDate, year);
    LONGLONG dst_end = transition_time(&tz->StandardDate, year);
    if (is_utc) {
        /* transitions are in local time: standard time before the start, daylight time before the end. */
        dst_start += (LONGLONG)(tz->Bias + tz->StandardBias) * TICKS_PER_MINUTE;
        dst_end += (LONGLONG)(tz->Bias + tz->DaylightBias) * TICKS_PER_MINUTE;
    }
    if (dst_start < dst_end)
        return t >= dst_start && t < dst_end;
    return t >= dst_start || t < dst_end;
}

BOOL WINAPI SystemTimeToTzSpecificLocalTime(const TIME_ZONE_INFORMATION *tz, const SYSTEMTIME *universal,
                                            LPSYSTEMTIME local)
{
    TIME_ZONE_INFORMATION current;
    if (!tz) {
        fill_time_zone(&current);
        tz = &current;
    }
    LONGLONG t;
    if (!universal || !local || !KbSystemTimeToFileTime(universal, &t)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LONG bias = tz->Bias + (in_daylight(tz, t, TRUE) ? tz->DaylightBias : tz->StandardBias);
    KbFileTimeToSystemTime(t - (LONGLONG)bias * TICKS_PER_MINUTE, local);
    return TRUE;
}

BOOL WINAPI SystemTimeToTzSpecificLocalTimeEx(const DYNAMIC_TIME_ZONE_INFORMATION *tz, const SYSTEMTIME *universal,
                                              LPSYSTEMTIME local)
{
    TIME_ZONE_INFORMATION info;
    if (tz && universal)
        GetTimeZoneInformationForYear(universal->wYear, (PDYNAMIC_TIME_ZONE_INFORMATION)tz, &info);
    else if (tz)
        memcpy(&info, tz, sizeof(info));
    return SystemTimeToTzSpecificLocalTime(tz ? &info : nullptr, universal, local);
}

BOOL WINAPI TzSpecificLocalTimeToSystemTime(const TIME_ZONE_INFORMATION *tz, const SYSTEMTIME *local,
                                            LPSYSTEMTIME universal)
{
    TIME_ZONE_INFORMATION current;
    if (!tz) {
        fill_time_zone(&current);
        tz = &current;
    }
    LONGLONG t;
    if (!universal || !local || !KbSystemTimeToFileTime(local, &t)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LONG bias = tz->Bias + (in_daylight(tz, t, FALSE) ? tz->DaylightBias : tz->StandardBias);
    KbFileTimeToSystemTime(t + (LONGLONG)bias * TICKS_PER_MINUTE, universal);
    return TRUE;
}

/* ------------------------------------------------------------------ dos times */

BOOL WINAPI DosDateTimeToFileTime(WORD date, WORD time, LPFILETIME file_time)
{
    SYSTEMTIME st = {};
    st.wYear = (WORD)((date >> 9) + 1980);
    st.wMonth = (WORD)((date >> 5) & 0xF);
    st.wDay = (WORD)(date & 0x1F);
    st.wHour = (WORD)(time >> 11);
    st.wMinute = (WORD)((time >> 5) & 0x3F);
    st.wSecond = (WORD)((time & 0x1F) * 2);
    return SystemTimeToFileTime(&st, file_time);
}

BOOL WINAPI FileTimeToDosDateTime(const FILETIME *file_time, LPWORD date, LPWORD time)
{
    SYSTEMTIME st;
    if (!date || !time || !FileTimeToSystemTime(file_time, &st) || st.wYear < 1980 || st.wYear > 2107) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *date = (WORD)(((st.wYear - 1980) << 9) | (st.wMonth << 5) | st.wDay);
    *time = (WORD)((st.wHour << 11) | (st.wMinute << 5) | (st.wSecond / 2));
    return TRUE;
}

/* ------------------------------------------------------------------ adjustment */

BOOL WINAPI GetSystemTimeAdjustment(PDWORD adjustment, PDWORD increment, PBOOL disabled)
{
    ULONG maximum = 0, minimum = 0, current = 156250;
    NtQueryTimerResolution(&maximum, &minimum, &current);
    if (adjustment)
        *adjustment = current;
    if (increment)
        *increment = current;
    if (disabled)
        *disabled = TRUE;
    return TRUE;
}

BOOL WINAPI SetSystemTimeAdjustment(DWORD adjustment, BOOL disabled)
{
    KbSetError(ERROR_PRIVILEGE_NOT_HELD);
    return FALSE;
}

/* ------------------------------------------------------------------ dynamic zones */

/* reads FirstEntry and LastEntry from the zone's Dynamic DST key. */
DWORD WINAPI GetDynamicTimeZoneInformationEffectiveYears(const DYNAMIC_TIME_ZONE_INFORMATION *info, LPDWORD first,
                                                         LPDWORD last)
{
    if (!info || !first || !last)
        return ERROR_INVALID_PARAMETER;
    WCHAR path[256];
    if (!info->TimeZoneKeyName[0] || wcslen(info->TimeZoneKeyName) + RTL_NUMBER_OF(TZ_ZONES) + 16 > RTL_NUMBER_OF(path))
        return ERROR_FILE_NOT_FOUND;
    wcscpy(path, TZ_ZONES);
    wcscat(path, info->TimeZoneKeyName);
    wcscat(path, L"\\Dynamic DST");
    HKEY key;
    LSTATUS status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE, &key);
    if (status)
        return (DWORD)status;
    DWORD low = 0, high = 0, size = sizeof(DWORD), type = 0;
    status = RegQueryValueExW(key, L"FirstEntry", nullptr, &type, (LPBYTE)&low, &size);
    if (!status && type != REG_DWORD)
        status = ERROR_FILE_NOT_FOUND;
    if (!status) {
        size = sizeof(DWORD);
        status = RegQueryValueExW(key, L"LastEntry", nullptr, &type, (LPBYTE)&high, &size);
        if (!status && type != REG_DWORD)
            status = ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(key);
    if (status)
        return (DWORD)status;
    *first = low;
    *last = high;
    return ERROR_SUCCESS;
}

BOOL WINAPI TzSpecificLocalTimeToSystemTimeEx(const DYNAMIC_TIME_ZONE_INFORMATION *tz, const SYSTEMTIME *local,
                                              LPSYSTEMTIME universal)
{
    TIME_ZONE_INFORMATION info;
    if (tz && local)
        GetTimeZoneInformationForYear(local->wYear, (PDYNAMIC_TIME_ZONE_INFORMATION)tz, &info);
    else if (tz)
        memcpy(&info, tz, sizeof(info));
    return TzSpecificLocalTimeToSystemTime(tz ? &info : nullptr, local, universal);
}
