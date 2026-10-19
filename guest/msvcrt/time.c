/*
 * time: the time zone of _tzset, conversions between time_t and
 * struct tm, strftime, asctime and the clock functions.
 */
#include "msvcrt_private.h"

#define MAX_TIME64 32535215999ll /* 3000-12-31 23:59:59 */
#define MAX_TIME32 0x7fffffffll
#define EPOCH_FILETIME 116444736000000000ll

/* msvcrt's values before the first _tzset. */
long MsvcrtTimezone = 8 * 3600;
int MsvcrtDaylight = 1;
long MsvcrtDstbias = -3600;
static char g_tzname_std[64] = "PST";
static char g_tzname_dst[64] = "PDT";
char *MsvcrtTzname[2] = {g_tzname_std, g_tzname_dst};
static wchar_t g_wtzname_std[64] = L"PST";
static wchar_t g_wtzname_dst[64] = L"PDT";
static wchar_t *g_wtzname[2] = {g_wtzname_std, g_wtzname_dst};

/* the exported data. */
long _timezone = 8 * 3600;
int _daylight = 1;
long _dstbias = -3600;
char *_tzname[2] = {g_tzname_std, g_tzname_dst};

static int g_tz_from_system;
static TIME_ZONE_INFORMATION g_tzi;
static int g_tz_done;

static void publish(void)
{
    _timezone = MsvcrtTimezone;
    _daylight = MsvcrtDaylight;
    _dstbias = MsvcrtDstbias;
    MultiByteToWideChar(CP_ACP, 0, g_tzname_std, -1, g_wtzname_std, 64);
    MultiByteToWideChar(CP_ACP, 0, g_tzname_dst, -1, g_wtzname_dst, 64);
}

/* a time zone like "EST5EDT" or "CET-1:00CEST" from the TZ variable. */
static void parse_tz(const char *tz)
{
    strncpy(g_tzname_std, tz, 3);
    g_tzname_std[3] = 0;
    const char *p = tz + 3;
    int neg = *p == '-';
    if (neg)
        p++;
    long seconds = 3600L * atol(p);
    while (*p == '+' || (*p >= '0' && *p <= '9'))
        p++;
    if (*p == ':') {
        p++;
        seconds += 60L * atol(p);
        while (*p >= '0' && *p <= '9')
            p++;
        if (*p == ':') {
            p++;
            seconds += atol(p);
            while (*p >= '0' && *p <= '9')
                p++;
        }
    }
    MsvcrtTimezone = neg ? -seconds : seconds;
    MsvcrtDaylight = *p != 0;
    if (MsvcrtDaylight) {
        strncpy(g_tzname_dst, p, 3);
        g_tzname_dst[3] = 0;
    } else {
        g_tzname_dst[0] = 0;
    }
}

void MsvcrtTzset(void)
{
    _lock(_TIME_LOCK);
    g_tz_from_system = 0;
    const char *tz = getenv("TZ");
    if (tz && *tz) {
        parse_tz(tz);
        MsvcrtDstbias = -3600;
    } else if (GetTimeZoneInformation(&g_tzi) != TIME_ZONE_ID_INVALID) {
        g_tz_from_system = 1;
        MsvcrtTimezone = g_tzi.Bias * 60;
        if (g_tzi.StandardDate.wMonth)
            MsvcrtTimezone += g_tzi.StandardBias * 60;
        if (g_tzi.DaylightDate.wMonth && g_tzi.DaylightBias) {
            MsvcrtDaylight = 1;
            MsvcrtDstbias = (g_tzi.DaylightBias - g_tzi.StandardBias) * 60;
        } else {
            MsvcrtDaylight = 0;
            MsvcrtDstbias = 0;
        }
        BOOL used = FALSE;
        if (!WideCharToMultiByte(CP_ACP, 0, g_tzi.StandardName, -1, g_tzname_std, 63, nullptr, &used) || used)
            g_tzname_std[0] = 0;
        g_tzname_std[63] = 0;
        used = FALSE;
        if (!WideCharToMultiByte(CP_ACP, 0, g_tzi.DaylightName, -1, g_tzname_dst, 63, nullptr, &used) || used)
            g_tzname_dst[0] = 0;
        g_tzname_dst[63] = 0;
    }
    g_tz_done = 1;
    publish();
    _unlock(_TIME_LOCK);
}

static void tzset_once(void)
{
    if (!g_tz_done)
        MsvcrtTzset();
}

void __cdecl _tzset(void)
{
    MsvcrtTzset();
}

long *__cdecl __p__timezone(void)
{
    return &_timezone;
}

int *__cdecl __p__daylight(void)
{
    return &_daylight;
}

long *__cdecl __p__dstbias(void)
{
    return &_dstbias;
}

char **__cdecl __p__tzname(void)
{
    return _tzname;
}

wchar_t **__cdecl __p__wtzname(void)
{
    return g_wtzname;
}

errno_t __cdecl _get_timezone(long *value)
{
    if (!value) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *value = MsvcrtTimezone;
    return 0;
}

errno_t __cdecl _get_daylight(int *value)
{
    if (!value) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *value = MsvcrtDaylight;
    return 0;
}

errno_t __cdecl _get_dstbias(long *value)
{
    if (!value) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *value = MsvcrtDstbias;
    return 0;
}

errno_t __cdecl _get_tzname(size_t *len, char *buf, size_t size, int index)
{
    if ((!buf && size) || index < 0 || index > 1 || !len) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *len = strlen(MsvcrtTzname[index]) + 1;
    if (!buf)
        return 0;
    if (*len > size)
        return ERANGE;
    strcpy(buf, MsvcrtTzname[index]);
    return 0;
}

/* ------------------------------------------------------------------ calendar arithmetic */

static int is_leap(long long year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static const short g_month_start[2][13] = {
    {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365},
    {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366},
};

/* days from 1970-01-01 to year-month-day, month 1 to 12. */
static long long days_from_civil(long long y, int m, int d)
{
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    long long yoe = y - era * 400;
    long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(long long z, long long *y, int *m, int *d)
{
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    long long doe = z - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long long mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = yoe + era * 400 + (*m <= 2);
}

static void fill_tm(long long t, struct tm *tm)
{
    long long days = t / 86400;
    long long secs = t % 86400;
    if (secs < 0) {
        secs += 86400;
        days--;
    }
    long long y;
    int m, d;
    civil_from_days(days, &y, &m, &d);
    tm->tm_year = (int)(y - 1900);
    tm->tm_mon = m - 1;
    tm->tm_mday = d;
    tm->tm_yday = g_month_start[is_leap(y)][m - 1] + d - 1;
    tm->tm_wday = (int)((days % 7 + 11) % 7);
    tm->tm_hour = (int)(secs / 3600);
    tm->tm_min = (int)(secs / 60 % 60);
    tm->tm_sec = (int)(secs % 60);
    tm->tm_isdst = 0;
}

/* ------------------------------------------------------------------ daylight saving time */

/* year day of a transition rule: absolute when year is set, else the week-th weekday of the month. */
static int rule_yday(int year, int month, int week, int weekday, int day, int absolute)
{
    int leap = is_leap(1900LL + year);
    int first = g_month_start[leap][month - 1];
    if (absolute)
        return first + day - 1;
    long long days = days_from_civil(1900LL + year, month, 1);
    int wday_first = (int)((days % 7 + 11) % 7);
    int yday = first + (weekday - wday_first + 7) % 7 + 7 * (week - 1);
    int last = g_month_start[leap][month] - 1;
    while (yday > last)
        yday -= 7;
    return yday;
}

typedef struct {
    int year;
    int start_yday, start_ms;
    int end_yday, end_ms;
} DST_RULE;

static DST_RULE g_dst_cache = {-1};

static void compute_rule(int year, DST_RULE *r)
{
    int start_ms, end_ms;
    if (g_tz_from_system) {
        const SYSTEMTIME *s = &g_tzi.DaylightDate, *e = &g_tzi.StandardDate;
        r->start_yday = rule_yday(year, s->wMonth, s->wDay, s->wDayOfWeek, s->wDay, s->wYear != 0);
        r->end_yday = rule_yday(year, e->wMonth, e->wDay, e->wDayOfWeek, e->wDay, e->wYear != 0);
        start_ms = ((s->wHour * 60 + s->wMinute) * 60 + s->wSecond) * 1000 + s->wMilliseconds;
        end_ms = ((e->wHour * 60 + e->wMinute) * 60 + e->wSecond) * 1000 + e->wMilliseconds;
    } else if (year >= 107) {
        /* united states rules since 2007, used with a TZ variable like msvcrt. */
        r->start_yday = rule_yday(year, 3, 2, 0, 0, 0);
        r->end_yday = rule_yday(year, 11, 1, 0, 0, 0);
        start_ms = end_ms = 2 * 3600 * 1000;
    } else {
        r->start_yday = rule_yday(year, 4, 1, 0, 0, 0);
        r->end_yday = rule_yday(year, 10, 5, 0, 0, 0);
        start_ms = end_ms = 2 * 3600 * 1000;
    }
    /* the end is given in daylight time; tm values are standard time. */
    end_ms += (int)MsvcrtDstbias * 1000;
    if (end_ms < 0) {
        end_ms += 86400000;
        r->end_yday--;
    } else if (end_ms >= 86400000) {
        end_ms -= 86400000;
        r->end_yday++;
    }
    r->start_ms = start_ms;
    r->end_ms = end_ms;
    r->year = year;
}

int MsvcrtIsInDst(const struct tm *tm)
{
    if (!MsvcrtDaylight)
        return 0;
    _lock(_TIME_LOCK);
    if (g_dst_cache.year != tm->tm_year)
        compute_rule(tm->tm_year, &g_dst_cache);
    DST_RULE r = g_dst_cache;
    _unlock(_TIME_LOCK);
    int yd = tm->tm_yday;
    if (r.start_yday < r.end_yday) {
        if (yd < r.start_yday || yd > r.end_yday)
            return 0;
        if (yd > r.start_yday && yd < r.end_yday)
            return 1;
    } else {
        if (yd < r.end_yday || yd > r.start_yday)
            return 1;
        if (yd > r.end_yday && yd < r.start_yday)
            return 0;
    }
    int ms = ((tm->tm_hour * 60 + tm->tm_min) * 60 + tm->tm_sec) * 1000;
    if (yd == r.start_yday)
        return ms >= r.start_ms;
    return ms < r.end_ms;
}

/* ------------------------------------------------------------------ time_t and struct tm */

__time64_t __cdecl _time64(__time64_t *out)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    long long t = (long long)((((ULONGLONG)ft.dwHighDateTime << 32 | ft.dwLowDateTime) - EPOCH_FILETIME) / 10000000);
    if (t > MAX_TIME64)
        t = -1;
    if (out)
        *out = t;
    return t;
}

__time64_t __cdecl time(__time64_t *out)
{
    return _time64(out);
}

__time32_t __cdecl _time32(__time32_t *out)
{
    __time64_t t = _time64(nullptr);
    if (t > MAX_TIME32)
        t = -1;
    if (out)
        *out = (__time32_t)t;
    return (__time32_t)t;
}

errno_t __cdecl _gmtime64_s(struct tm *tm, const __time64_t *t)
{
    if (tm)
        memset(tm, 0xff, sizeof(*tm));
    if (!tm || !t) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (*t < -43200 || *t > MAX_TIME64 + 43200) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    fill_tm(*t, tm);
    return 0;
}

struct tm *__cdecl _gmtime64(const __time64_t *t)
{
    struct tm *tm = &MsvcrtThreadData()->tm;
    return _gmtime64_s(tm, t) ? nullptr : tm;
}

struct tm *__cdecl gmtime(const __time64_t *t)
{
    return _gmtime64(t);
}

errno_t __cdecl _gmtime32_s(struct tm *tm, const __time32_t *t)
{
    if (!t) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    __time64_t t64 = *t;
    if (t64 < 0) {
        if (tm)
            memset(tm, 0xff, sizeof(*tm));
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    return _gmtime64_s(tm, &t64);
}

struct tm *__cdecl _gmtime32(const __time32_t *t)
{
    struct tm *tm = &MsvcrtThreadData()->tm;
    return _gmtime32_s(tm, t) ? nullptr : tm;
}

errno_t __cdecl _localtime64_s(struct tm *tm, const __time64_t *t)
{
    if (tm)
        memset(tm, 0xff, sizeof(*tm));
    if (!tm || !t) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    if (*t < 0 || *t > MAX_TIME64) {
        MsvcrtSetErrno(EINVAL);
        return EINVAL;
    }
    tzset_once();
    long long local = *t - MsvcrtTimezone;
    fill_tm(local, tm);
    if (MsvcrtDaylight && MsvcrtIsInDst(tm)) {
        fill_tm(local - MsvcrtDstbias, tm);
        tm->tm_isdst = 1;
    }
    return 0;
}

struct tm *__cdecl _localtime64(const __time64_t *t)
{
    struct tm *tm = &MsvcrtThreadData()->tm;
    return _localtime64_s(tm, t) ? nullptr : tm;
}

struct tm *__cdecl localtime(const __time64_t *t)
{
    return _localtime64(t);
}

errno_t __cdecl _localtime32_s(struct tm *tm, const __time32_t *t)
{
    if (!t) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    __time64_t t64 = *t;
    return _localtime64_s(tm, &t64);
}

struct tm *__cdecl _localtime32(const __time32_t *t)
{
    struct tm *tm = &MsvcrtThreadData()->tm;
    return _localtime32_s(tm, t) ? nullptr : tm;
}

/* normalizes tm and returns its value as utc seconds, or -1 out of range. */
static long long tm_to_seconds(struct tm *tm)
{
    long long mon = tm->tm_mon;
    long long year = tm->tm_year + mon / 12;
    mon %= 12;
    if (mon < 0) {
        mon += 12;
        year--;
    }
    long long days = days_from_civil(1900 + year, (int)mon + 1, 1) + tm->tm_mday - 1;
    return days * 86400 + (long long)tm->tm_hour * 3600 + (long long)tm->tm_min * 60 + tm->tm_sec;
}

static __time64_t make_time(struct tm *tm, int local, long long max)
{
    if (!tm) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    long long t = tm_to_seconds(tm);
    if (local) {
        tzset_once();
        t += MsvcrtTimezone;
        struct tm norm;
        fill_tm(t - MsvcrtTimezone, &norm);
        if (tm->tm_isdst > 0 || (tm->tm_isdst < 0 && MsvcrtDaylight && MsvcrtIsInDst(&norm)))
            t += MsvcrtDstbias;
    }
    if (t < 0 || t > max) {
        MsvcrtSetErrno(EINVAL);
        return -1;
    }
    struct tm out;
    if (local) {
        if (_localtime64_s(&out, &t))
            return -1;
    } else {
        fill_tm(t, &out);
    }
    *tm = out;
    return t;
}

__time64_t __cdecl _mktime64(struct tm *tm)
{
    return make_time(tm, 1, MAX_TIME64);
}

__time64_t __cdecl mktime(struct tm *tm)
{
    return make_time(tm, 1, MAX_TIME64);
}

__time32_t __cdecl _mktime32(struct tm *tm)
{
    return (__time32_t)make_time(tm, 1, MAX_TIME32);
}

__time64_t __cdecl _mkgmtime64(struct tm *tm)
{
    return make_time(tm, 0, MAX_TIME64);
}

__time64_t __cdecl _mkgmtime(struct tm *tm)
{
    return make_time(tm, 0, MAX_TIME64);
}

__time32_t __cdecl _mkgmtime32(struct tm *tm)
{
    return (__time32_t)make_time(tm, 0, MAX_TIME32);
}

double __cdecl difftime(__time64_t a, __time64_t b)
{
    return (double)(a - b);
}

double __cdecl _difftime64(__time64_t a, __time64_t b)
{
    return (double)(a - b);
}

double __cdecl _difftime32(__time32_t a, __time32_t b)
{
    return (double)a - (double)b;
}

/* seconds since 1970 of a utc file time. */
__time64_t MsvcrtFileTimeToTime(const FILETIME *ft)
{
    ULONGLONG v = (ULONGLONG)ft->dwHighDateTime << 32 | ft->dwLowDateTime;
    return (__time64_t)((long long)(v - EPOCH_FILETIME) / 10000000);
}

/* the time_t of a local date like msvcrt's __loctotime64_t. */
__time64_t MsvcrtLocalToTime(int year, int month, int day, int hour, int min, int sec, int dst)
{
    struct tm tm = {sec, min, hour, day, month - 1, year - 1900, 0, 0, dst};
    return make_time(&tm, 1, MAX_TIME64);
}

/* ------------------------------------------------------------------ _ftime and clock */

struct __timeb64 {
    __time64_t time;
    unsigned short millitm;
    short timezone;
    short dstflag;
};

struct __timeb32 {
    __time32_t time;
    unsigned short millitm;
    short timezone;
    short dstflag;
};

errno_t __cdecl _ftime64_s(struct __timeb64 *tb)
{
    if (!tb) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    tzset_once();
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULONGLONG v = ((ULONGLONG)ft.dwHighDateTime << 32 | ft.dwLowDateTime) - EPOCH_FILETIME;
    tb->time = (__time64_t)(v / 10000000);
    tb->millitm = (unsigned short)(v / 10000 % 1000);
    tb->timezone = (short)(MsvcrtTimezone / 60);
    struct tm tm;
    tb->dstflag = 0;
    if (!_localtime64_s(&tm, &tb->time))
        tb->dstflag = (short)(tm.tm_isdst > 0);
    return 0;
}

void __cdecl _ftime64(struct __timeb64 *tb)
{
    _ftime64_s(tb);
}

errno_t __cdecl _ftime32_s(struct __timeb32 *tb)
{
    if (!tb) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    struct __timeb64 t;
    _ftime64_s(&t);
    tb->time = (__time32_t)t.time;
    tb->millitm = t.millitm;
    tb->timezone = t.timezone;
    tb->dstflag = t.dstflag;
    return 0;
}

void __cdecl _ftime32(struct __timeb32 *tb)
{
    _ftime32_s(tb);
}

void __cdecl _ftime(struct __timeb64 *tb)
{
    _ftime64_s(tb);
}

static LARGE_INTEGER g_clock_start;

void MsvcrtClockInit(void)
{
    QueryPerformanceCounter(&g_clock_start);
}

clock_t __cdecl clock(void)
{
    LARGE_INTEGER now, freq;
    if (!QueryPerformanceCounter(&now) || !QueryPerformanceFrequency(&freq) || !freq.QuadPart)
        return -1;
    long long elapsed = now.QuadPart - g_clock_start.QuadPart;
    return (clock_t)(elapsed / freq.QuadPart * 1000 + elapsed % freq.QuadPart * 1000 / freq.QuadPart);
}

/* ------------------------------------------------------------------ text */

static const char *const g_days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *const g_months[] = {"January", "February", "March",     "April",   "May",      "June",
                                       "July",    "August",   "September", "October", "November", "December"};

static int tm_valid(const struct tm *tm)
{
    return tm->tm_sec >= 0 && tm->tm_sec <= 59 && tm->tm_min >= 0 && tm->tm_min <= 59 && tm->tm_hour >= 0 &&
           tm->tm_hour <= 23 && tm->tm_mday >= 1 && tm->tm_mday <= 31 && tm->tm_mon >= 0 && tm->tm_mon <= 11 &&
           tm->tm_wday >= 0 && tm->tm_wday <= 6 && tm->tm_yday >= 0 && tm->tm_yday <= 365 && tm->tm_year >= 0;
}

/* "Wed Jan 02 02:03:55 1980\n", msvcrt padding the day with a zero. */
static void format_asctime(const struct tm *tm, char *buf)
{
    _snprintf(buf, 26, "%.3s %.3s %02d %02d:%02d:%02d %4d\n", g_days[tm->tm_wday], g_months[tm->tm_mon], tm->tm_mday,
              tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900);
    buf[25] = 0;
}

errno_t __cdecl asctime_s(char *buf, size_t size, const struct tm *tm)
{
    if (!buf || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    *buf = 0;
    if (size < 26 || !tm || !tm_valid(tm) || tm->tm_year > 8099) {
        MSVCRT_INVALID(size < 26 ? ERANGE : EINVAL);
        return size < 26 ? ERANGE : EINVAL;
    }
    format_asctime(tm, buf);
    return 0;
}

char *__cdecl asctime(const struct tm *tm)
{
    char *buf = MsvcrtThreadData()->asctime_buf;
    return asctime_s(buf, 26, tm) ? nullptr : buf;
}

errno_t __cdecl _wasctime_s(wchar_t *buf, size_t size, const struct tm *tm)
{
    char tmp[26];
    if (!buf || !size) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    errno_t r = asctime_s(tmp, 26, tm);
    *buf = 0;
    if (r)
        return r;
    if (size < 26) {
        MSVCRT_INVALID(ERANGE);
        return ERANGE;
    }
    for (int i = 0; i < 26; i++)
        buf[i] = (unsigned char)tmp[i];
    return 0;
}

wchar_t *__cdecl _wasctime(const struct tm *tm)
{
    wchar_t *buf = MsvcrtThreadData()->wasctime_buf;
    return _wasctime_s(buf, 26, tm) ? nullptr : buf;
}

char *__cdecl _ctime64(const __time64_t *t)
{
    struct tm tm;
    if (!t || _localtime64_s(&tm, t))
        return nullptr;
    char *buf = MsvcrtThreadData()->asctime_buf;
    format_asctime(&tm, buf);
    return buf;
}

char *__cdecl ctime(const __time64_t *t)
{
    return _ctime64(t);
}

char *__cdecl _ctime32(const __time32_t *t)
{
    if (!t)
        return nullptr;
    __time64_t t64 = *t;
    return _ctime64(&t64);
}

errno_t __cdecl _ctime64_s(char *buf, size_t size, const __time64_t *t)
{
    if (!buf || !size || !t) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    struct tm tm;
    errno_t r = _localtime64_s(&tm, t);
    if (r) {
        *buf = 0;
        return r;
    }
    return asctime_s(buf, size, &tm);
}

wchar_t *__cdecl _wctime64(const __time64_t *t)
{
    struct tm tm;
    if (!t || _localtime64_s(&tm, t))
        return nullptr;
    return _wasctime(&tm);
}

wchar_t *__cdecl _wctime(const __time64_t *t)
{
    return _wctime64(t);
}

wchar_t *__cdecl _wctime32(const __time32_t *t)
{
    if (!t)
        return nullptr;
    __time64_t t64 = *t;
    return _wctime64(&t64);
}

char *__cdecl _strdate(char *buf)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snprintf(buf, 9, "%02d/%02d/%02d", st.wMonth, st.wDay, st.wYear % 100);
    buf[8] = 0;
    return buf;
}

char *__cdecl _strtime(char *buf)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snprintf(buf, 9, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    buf[8] = 0;
    return buf;
}

errno_t __cdecl _strdate_s(char *buf, size_t size)
{
    if (!buf || size < 9) {
        if (buf && size)
            *buf = 0;
        MSVCRT_INVALID(buf && size ? ERANGE : EINVAL);
        return buf && size ? ERANGE : EINVAL;
    }
    _strdate(buf);
    return 0;
}

errno_t __cdecl _strtime_s(char *buf, size_t size)
{
    if (!buf || size < 9) {
        if (buf && size)
            *buf = 0;
        MSVCRT_INVALID(buf && size ? ERANGE : EINVAL);
        return buf && size ? ERANGE : EINVAL;
    }
    _strtime(buf);
    return 0;
}

wchar_t *__cdecl _wstrdate(wchar_t *buf)
{
    char tmp[9];
    _strdate(tmp);
    for (int i = 0; i < 9; i++)
        buf[i] = (unsigned char)tmp[i];
    return buf;
}

wchar_t *__cdecl _wstrtime(wchar_t *buf)
{
    char tmp[9];
    _strtime(tmp);
    for (int i = 0; i < 9; i++)
        buf[i] = (unsigned char)tmp[i];
    return buf;
}

/* ------------------------------------------------------------------ strftime */

typedef struct {
    char *out;
    size_t size;
    size_t len;
    int overflow;
} STRF;

static void strf_puts(STRF *s, const char *text)
{
    for (; *text; text++) {
        if (s->len + 1 >= s->size) {
            s->overflow = 1;
            return;
        }
        s->out[s->len++] = *text;
    }
}

static void strf_num(STRF *s, int value, int digits, int nozero)
{
    char tmp[16];
    if (nozero)
        _snprintf(tmp, sizeof(tmp), "%d", value);
    else
        _snprintf(tmp, sizeof(tmp), "%0*d", digits, value);
    tmp[15] = 0;
    strf_puts(s, tmp);
}

static int strf_expand(STRF *s, const char *fmt, const struct tm *tm);

/* one conversion of the c locale; returns 0 for an unknown one. */
static int strf_conv(STRF *s, char c, int alt, const struct tm *tm)
{
    int hour12 = tm->tm_hour % 12;
    if (!hour12)
        hour12 = 12;
    switch (c) {
    case 'a':
        if ((unsigned)tm->tm_wday > 6)
            return 0;
        {
            char abbr[4] = {};
            memcpy(abbr, g_days[tm->tm_wday], 3);
            strf_puts(s, abbr);
        }
        break;
    case 'A':
        if ((unsigned)tm->tm_wday > 6)
            return 0;
        strf_puts(s, g_days[tm->tm_wday]);
        break;
    case 'b':
    case 'h':
        if ((unsigned)tm->tm_mon > 11)
            return 0;
        {
            char abbr[4] = {};
            memcpy(abbr, g_months[tm->tm_mon], 3);
            strf_puts(s, abbr);
        }
        break;
    case 'B':
        if ((unsigned)tm->tm_mon > 11)
            return 0;
        strf_puts(s, g_months[tm->tm_mon]);
        break;
    case 'c':
        if (alt)
            return strf_expand(s, "%A, %B %#d, %Y, %H:%M:%S", tm);
        return strf_expand(s, "%m/%d/%y %H:%M:%S", tm);
    case 'x':
        if (alt)
            return strf_expand(s, "%A, %B %#d, %Y", tm);
        return strf_expand(s, "%m/%d/%y", tm);
    case 'X':
        return strf_expand(s, "%H:%M:%S", tm);
    case 'd':
        strf_num(s, tm->tm_mday, 2, alt);
        break;
    case 'H':
        strf_num(s, tm->tm_hour, 2, alt);
        break;
    case 'I':
        strf_num(s, hour12, 2, alt);
        break;
    case 'j':
        strf_num(s, tm->tm_yday + 1, 3, alt);
        break;
    case 'm':
        strf_num(s, tm->tm_mon + 1, 2, alt);
        break;
    case 'M':
        strf_num(s, tm->tm_min, 2, alt);
        break;
    case 'p':
        strf_puts(s, tm->tm_hour < 12 ? "AM" : "PM");
        break;
    case 'S':
        strf_num(s, tm->tm_sec, 2, alt);
        break;
    case 'U':
        strf_num(s, (tm->tm_yday + 7 - tm->tm_wday) / 7, 2, alt);
        break;
    case 'W':
        strf_num(s, (tm->tm_yday + 7 - (tm->tm_wday + 6) % 7) / 7, 2, alt);
        break;
    case 'w':
        strf_num(s, tm->tm_wday, 1, alt);
        break;
    case 'y':
        strf_num(s, (tm->tm_year + 1900) % 100, 2, alt);
        break;
    case 'Y':
        strf_num(s, tm->tm_year + 1900, 4, 1);
        break;
    case 'z':
    case 'Z':
        tzset_once();
        strf_puts(s, MsvcrtTzname[tm->tm_isdst > 0]);
        break;
    case '%':
        strf_puts(s, "%");
        break;
    default:
        return 0;
    }
    return 1;
}

static int strf_expand(STRF *s, const char *fmt, const struct tm *tm)
{
    for (; *fmt && !s->overflow; fmt++) {
        if (*fmt != '%') {
            char one[2] = {*fmt, 0};
            strf_puts(s, one);
            continue;
        }
        fmt++;
        int alt = 0;
        if (*fmt == '#') {
            alt = 1;
            fmt++;
        }
        if (!*fmt || !strf_conv(s, *fmt, alt, tm))
            return 0;
    }
    return 1;
}

static int tm_fields_valid(const struct tm *tm)
{
    return tm->tm_mon >= 0 && tm->tm_mon <= 11 && tm->tm_mday >= 1 && tm->tm_mday <= 31 && tm->tm_hour >= 0 &&
           tm->tm_hour <= 23 && tm->tm_min >= 0 && tm->tm_min <= 59 && tm->tm_sec >= 0 && tm->tm_sec <= 59 &&
           tm->tm_wday >= 0 && tm->tm_wday <= 6 && tm->tm_yday >= 0 && tm->tm_yday <= 365 && tm->tm_year >= -1900 &&
           tm->tm_year <= 8099;
}

size_t __cdecl strftime(char *buf, size_t size, const char *fmt, const struct tm *tm)
{
    if (!buf || !size || !fmt || !tm) {
        if (buf && size)
            *buf = 0;
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    if (!tm_fields_valid(tm)) {
        *buf = 0;
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    STRF s = {buf, size, 0, 0};
    if (!strf_expand(&s, fmt, tm)) {
        *buf = 0;
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    if (s.overflow) {
        *buf = 0;
        MsvcrtSetErrno(ERANGE);
        return 0;
    }
    buf[s.len] = 0;
    return s.len;
}

size_t __cdecl _strftime_l(char *buf, size_t size, const char *fmt, const struct tm *tm, void *locale)
{
    return strftime(buf, size, fmt, tm);
}

size_t __cdecl wcsftime(wchar_t *buf, size_t size, const wchar_t *fmt, const struct tm *tm)
{
    if (!buf || !size || !fmt || !tm) {
        if (buf && size)
            *buf = 0;
        MSVCRT_INVALID(EINVAL);
        return 0;
    }
    char *afmt = MsvcrtWToA(fmt);
    if (!afmt) {
        *buf = 0;
        return 0;
    }
    size_t asize = size * 4 + 1;
    char *abuf = malloc(asize);
    size_t r = 0;
    *buf = 0;
    if (abuf) {
        r = strftime(abuf, asize, afmt, tm);
        if (r) {
            int n = MultiByteToWideChar(CP_ACP, 0, abuf, -1, buf, (int)size);
            if (n <= 0) {
                *buf = 0;
                MsvcrtSetErrno(ERANGE);
                r = 0;
            } else {
                r = n - 1;
            }
        }
        free(abuf);
    }
    free(afmt);
    return r;
}

size_t __cdecl _wcsftime_l(wchar_t *buf, size_t size, const wchar_t *fmt, const struct tm *tm, void *locale)
{
    return wcsftime(buf, size, fmt, tm);
}

/* ------------------------------------------------------------------ the names of the time */

/*
 * the day and month names the way the c++ library asks for them: one string with a separator in front of each name,
 * the short one and the long one of every day or month. Only the names of the c locale are known.
 */
static void *names_string(const char *const *names, int count, int wide)
{
    size_t total = 1;
    for (int i = 0; i < count; i++)
        total += 4 + strlen(names[i]) + 1;
    char *a = malloc(total * (wide ? sizeof(wchar_t) : 1));
    if (!a)
        return nullptr;
    char *out = a;
    wchar_t *w = (wchar_t *)a;
    size_t at = 0;
    for (int i = 0; i < count; i++) {
        char entry[64];
        _snprintf(entry, sizeof(entry), ":%.3s:%s", names[i], names[i]);
        for (char *c = entry; *c; c++) {
            if (wide)
                w[at++] = (unsigned char)*c;
            else
                out[at++] = *c;
        }
    }
    if (wide)
        w[at] = 0;
    else
        out[at] = 0;
    return a;
}

DECLSPEC_EXPORT char *__cdecl _Getdays(void)
{
    return names_string(g_days, 7, 0);
}

DECLSPEC_EXPORT char *__cdecl _Getmonths(void)
{
    return names_string(g_months, 12, 0);
}

DECLSPEC_EXPORT wchar_t *__cdecl _W_Getdays(void)
{
    return names_string(g_days, 7, 1);
}

DECLSPEC_EXPORT wchar_t *__cdecl _W_Getmonths(void)
{
    return names_string(g_months, 12, 1);
}

/* an opaque block that _Strftime and _Wcsftime are given back, to be freed by the caller. */
DECLSPEC_EXPORT void *__cdecl _Gettnames(void)
{
    return calloc(1, 256);
}

DECLSPEC_EXPORT void *__cdecl _W_Gettnames(void)
{
    return calloc(1, 256);
}

DECLSPEC_EXPORT size_t __cdecl _Strftime(char *buf, size_t size, const char *fmt, const struct tm *tm, void *names)
{
    return strftime(buf, size, fmt, tm);
}

DECLSPEC_EXPORT size_t __cdecl _Wcsftime(wchar_t *buf, size_t size, const wchar_t *fmt, const struct tm *tm,
                                         void *names)
{
    return wcsftime(buf, size, fmt, tm);
}

errno_t __cdecl _ctime32_s(char *buf, size_t size, const __time32_t *t)
{
    if (!t) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    __time64_t t64 = *t;
    return _ctime64_s(buf, size, &t64);
}

errno_t __cdecl _wctime64_s(wchar_t *buf, size_t size, const __time64_t *t)
{
    if (!buf || !size || !t) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    struct tm tm;
    errno_t r = _localtime64_s(&tm, t);
    if (r) {
        *buf = 0;
        return r;
    }
    return _wasctime_s(buf, size, &tm);
}

errno_t __cdecl _wctime32_s(wchar_t *buf, size_t size, const __time32_t *t)
{
    if (!t) {
        MSVCRT_INVALID(EINVAL);
        return EINVAL;
    }
    __time64_t t64 = *t;
    return _wctime64_s(buf, size, &t64);
}

errno_t __cdecl _wstrdate_s(wchar_t *buf, size_t size)
{
    if (!buf || size < 9) {
        if (buf && size)
            *buf = 0;
        MSVCRT_INVALID(buf && size ? ERANGE : EINVAL);
        return buf && size ? ERANGE : EINVAL;
    }
    _wstrdate(buf);
    return 0;
}

errno_t __cdecl _wstrtime_s(wchar_t *buf, size_t size)
{
    if (!buf || size < 9) {
        if (buf && size)
            *buf = 0;
        MSVCRT_INVALID(buf && size ? ERANGE : EINVAL);
        return buf && size ? ERANGE : EINVAL;
    }
    _wstrtime(buf);
    return 0;
}
