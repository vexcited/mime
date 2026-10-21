/*
 * dates: the conversions between variant times, system times and strings.
 *
 * dates are days since 1899-12-30 with the time of day as the fraction,
 * whose sign never changes the time: -1.25 is 1899-12-29 06:00. numbers
 * parse into decimal digits first, then round half to even like windows.
 */
#include "oleaut32_private.h"
#include <mime/win32/winnls.h>

/* ------------------------------------------------------------------ dates */

#define OLE_EPOCH_DAYS (-25569) /* 1899-12-30 in days since 1970-01-01 */
#define DATE_MIN (-657434.0)    /* 0100-01-01 */
#define DATE_MAX 2958466.0      /* 10000-01-01, exclusive */

static LONG days_from_civil(LONG y, LONG m, LONG d)
{
    y -= m <= 2;
    LONG era = (y >= 0 ? y : y - 399) / 400;
    LONG yoe = y - era * 400;
    LONG doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    LONG doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(LONG z, LONG *y, LONG *m, LONG *d)
{
    z += 719468;
    LONG era = (z >= 0 ? z : z - 146096) / 146097;
    LONG doe = z - era * 146097;
    LONG yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    LONG doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    LONG mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = yoe + era * 400 + (*m <= 2);
}

static LONG days_in_month(LONG y, LONG m)
{
    static const BYTE days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    BOOL leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return days[m - 1] + (m == 2 && leap);
}

/* two digit years follow the windows window: 00 to 29 are 2000s, 30 to 99 are 1900s. */
static LONG full_year(LONG y)
{
    return y < 30 ? y + 2000 : y < 100 ? y + 1900 : y;
}

static BOOL make_date(LONG y, LONG m, LONG d, LONG h, LONG mi, LONG s, DATE *out)
{
    if (y < 100 || y > 9999 || m < 1 || m > 12 || d < 1 || d > days_in_month(y, m) || h < 0 || h > 23 ||
        mi < 0 || mi > 59 || s < 0 || s > 59)
        return FALSE;
    LONG days = days_from_civil(y, m, d) - OLE_EPOCH_DAYS;
    double time = (h * 3600 + mi * 60 + s) / 86400.0;
    *out = days >= 0 ? days + time : days - time;
    return TRUE;
}

WINOLEAUTAPI INT WINAPI SystemTimeToVariantTime(LPSYSTEMTIME time, double *date)
{
    if (!time || !date)
        return FALSE;
    return make_date(full_year(time->wYear), time->wMonth, time->wDay, time->wHour, time->wMinute, time->wSecond,
                     date);
}

/* splits a date into its calendar day and the seconds of its time, rounded to the nearest second. */
static BOOL split_date(DATE date, LONG *days, LONG *seconds)
{
    if (!(date >= DATE_MIN && date < DATE_MAX))
        return FALSE;
    LONG whole = (LONG)date;
    double fraction = date - whole;
    if (fraction < 0)
        fraction = -fraction;
    LONG s = (LONG)(fraction * 86400.0 + 0.5);
    if (s >= 86400) {
        s -= 86400;
        whole++;
    }
    *days = whole;
    *seconds = s;
    return TRUE;
}

WINOLEAUTAPI INT WINAPI VariantTimeToSystemTime(DATE date, LPSYSTEMTIME time)
{
    LONG days, seconds, y, m, d;
    if (!time || !split_date(date, &days, &seconds))
        return FALSE;
    LONG absolute = days + OLE_EPOCH_DAYS;
    civil_from_days(absolute, &y, &m, &d);
    if (y < 100 || y > 9999)
        return FALSE;
    time->wYear = (WORD)y;
    time->wMonth = (WORD)m;
    time->wDay = (WORD)d;
    /* 1970-01-01 was a thursday. */
    time->wDayOfWeek = (WORD)(((absolute % 7) + 11) % 7);
    time->wHour = (WORD)(seconds / 3600);
    time->wMinute = (WORD)(seconds / 60 % 60);
    time->wSecond = (WORD)(seconds % 60);
    time->wMilliseconds = 0;
    return TRUE;
}

static int date_order(LCID lcid)
{
    WCHAR order[4];
    if (GetLocaleInfoW(lcid ? lcid : LOCALE_USER_DEFAULT, LOCALE_IDATE, order, RTL_NUMBER_OF(order)) &&
        order[0] >= L'0' && order[0] <= L'2')
        return order[0] - L'0';
    return 0;
}

static const WCHAR *const g_months[12] = {L"january", L"february", L"march",     L"april",   L"may",      L"june",
                                          L"july",    L"august",   L"september", L"october", L"november", L"december"};
static const WCHAR *const g_days[7] = {L"sunday",   L"monday", L"tuesday", L"wednesday",
                                       L"thursday", L"friday", L"saturday"};

/* a word that is a whole name or an abbreviation of at least three letters. */
static BOOL name_matches(const WCHAR *word, int length, const WCHAR *name)
{
    if (length < 3 || length > (int)wcslen(name))
        return FALSE;
    for (int i = 0; i < length; i++)
        if ((word[i] | 0x20) != name[i])
            return FALSE;
    return TRUE;
}

WINOLEAUTAPI HRESULT WINAPI VarDateFromStr(LPCOLESTR string, LCID lcid, ULONG flags, DATE *out)
{
    if (!string || !out)
        return E_INVALIDARG;
    LONG numbers[3], times[3];
    int number_digits[3], number_count = 0, time_count = 0, month = 0, meridian = 0;
    BOOL in_time = FALSE;
    const WCHAR *p = string;
    while (*p) {
        WCHAR c = *p;
        if (c >= L'0' && c <= L'9') {
            LONG value = 0;
            int digits = 0;
            while (*p >= L'0' && *p <= L'9') {
                if (++digits > 4)
                    return DISP_E_TYPEMISMATCH;
                value = value * 10 + (*p++ - L'0');
            }
            /* a number before a colon starts a time, the numbers after it continue it. */
            if (*p == L':' || in_time) {
                if (time_count == 3)
                    return DISP_E_TYPEMISMATCH;
                times[time_count++] = value;
                in_time = *p == L':';
                if (*p == L':')
                    p++;
                else if (*p == L'.' && time_count == 3)
                    for (p++; *p >= L'0' && *p <= L'9'; p++)
                        ;
                continue;
            }
            const WCHAR *after = p;
            while (*after == L' ')
                after++;
            if ((*after | 0x20) == L'a' || (*after | 0x20) == L'p') {
                /* "3 pm" is an hour. */
                if (time_count)
                    return DISP_E_TYPEMISMATCH;
                times[time_count++] = value;
                continue;
            }
            if (number_count == 3)
                return DISP_E_TYPEMISMATCH;
            number_digits[number_count] = digits;
            numbers[number_count++] = value;
        } else if ((c | 0x20) >= L'a' && (c | 0x20) <= L'z') {
            const WCHAR *word = p;
            while ((*p | 0x20) >= L'a' && (*p | 0x20) <= L'z')
                p++;
            int length = (int)(p - word);
            BOOL known = FALSE;
            if (length <= 2 && ((word[0] | 0x20) == L'a' || (word[0] | 0x20) == L'p') &&
                (length == 1 || (word[1] | 0x20) == L'm')) {
                meridian = (word[0] | 0x20) == L'p' ? 2 : 1;
                known = TRUE;
            }
            for (int i = 0; i < 12 && !known; i++)
                if (name_matches(word, length, g_months[i])) {
                    if (month)
                        return DISP_E_TYPEMISMATCH;
                    month = i + 1;
                    known = TRUE;
                }
            for (int i = 0; i < 7 && !known; i++)
                known = name_matches(word, length, g_days[i]);
            if (!known)
                return DISP_E_TYPEMISMATCH;
            in_time = FALSE;
        } else if (c == L' ' || c == L',' || c == L'/' || c == L'-' || c == L'.' || c == L'\t') {
            in_time = FALSE;
            p++;
        } else {
            return DISP_E_TYPEMISMATCH;
        }
    }
    LONG y = 1899, m = 12, d = 30;
    BOOL has_date = number_count || month;
    if (has_date) {
        SYSTEMTIME now;
        GetLocalTime(&now);
        if (month) {
            /* with a month name, the numbers are the day and the year, a year being large or four digits. */
            if (number_count < 1 || number_count > 2)
                return DISP_E_TYPEMISMATCH;
            int day_index = number_count == 2 && (number_digits[0] > 2 || numbers[0] > 31) ? 1 : 0;
            m = month;
            d = numbers[day_index];
            y = number_count == 2 ? full_year(numbers[1 - day_index]) : now.wYear;
        } else if (number_count == 3) {
            int order = number_digits[0] > 2 || numbers[0] > 31 ? 2 : date_order(lcid);
            LONG a = numbers[0], b = numbers[1], e = numbers[2];
            if (order == 2)
                y = a, m = b, d = e;
            else if (order == 1)
                d = a, m = b, y = e;
            else
                m = a, d = b, y = e;
            y = full_year(y);
        } else if (number_count == 2) {
            m = numbers[0], d = numbers[1];
            if (date_order(lcid) == 1 || m > 12)
                m = numbers[1], d = numbers[0];
            y = now.wYear;
        } else {
            return DISP_E_TYPEMISMATCH;
        }
    } else if (!time_count) {
        return DISP_E_TYPEMISMATCH;
    }
    LONG h = time_count > 0 ? times[0] : 0, mi = time_count > 1 ? times[1] : 0, s = time_count > 2 ? times[2] : 0;
    if (meridian) {
        if (!time_count || h < 1 || h > 12)
            return DISP_E_TYPEMISMATCH;
        h = h % 12 + (meridian == 2 ? 12 : 0);
    }
    DATE date;
    if (!make_date(y, m, d, h, mi, s, &date))
        return DISP_E_TYPEMISMATCH;
    *out = date;
    return S_OK;
}

/*
 * the short date of a day, also for the years before 1601 that the formatting of dates does not take: the day is formatted
 * in a year of the same calendar 400 years on, which has the same weekdays, and the year of the pattern is written in as
 * a literal, as many digits as the number has.
 */
static int format_short_date(LCID locale, DWORD override, const SYSTEMTIME *time, WCHAR *text, int size)
{
    if (time->wYear >= 1601)
        return GetDateFormatW(locale, DATE_SHORTDATE | override, time, nullptr, text, size);
    WCHAR pattern[80], format[160];
    if (!GetLocaleInfoW(locale, LOCALE_SSHORTDATE | override, pattern, 80))
        return 0;
    int n = 0;
    for (const WCHAR *p = pattern; *p && n < 140;) {
        if (*p == L'\'') {
            format[n++] = *p++;
            while (*p && n < 140) {
                format[n++] = *p;
                if (*p++ == L'\'')
                    break;
            }
        } else if (*p == L'y') {
            int run = 0;
            while (*p == L'y') {
                run++;
                p++;
            }
            WCHAR number[8];
            int digits = 0;
            LONG year = run <= 2 ? time->wYear % 100 : time->wYear;
            WCHAR reversed[8];
            do {
                reversed[digits++] = (WCHAR)(L'0' + year % 10);
                year /= 10;
            } while (year);
            if (run == 2 && digits < 2)
                reversed[digits++] = L'0';
            for (int i = 0; i < digits; i++)
                number[i] = reversed[digits - 1 - i];
            format[n++] = L'\'';
            for (int i = 0; i < digits; i++)
                format[n++] = number[i];
            format[n++] = L'\'';
        } else {
            format[n++] = *p++;
        }
    }
    format[n] = 0;
    SYSTEMTIME shifted = *time;
    shifted.wYear = (WORD)(time->wYear + 400 * ((1601 - time->wYear + 399) / 400));
    return GetDateFormatW(locale, 0, &shifted, format, text, size);
}

WINOLEAUTAPI HRESULT WINAPI VarBstrFromDate(DATE date, LCID lcid, ULONG flags, BSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    LONG days, seconds;
    SYSTEMTIME time;
    if (flags & LOCALE_USE_NLS) {
        /* the formats of the locale, the date and the time, always both, with the flags handed on as they are. */
        memset(&time, 0, sizeof(time));
        VariantTimeToSystemTime(date, &time);
        DWORD pass = flags & ~(ULONG)LOCALE_USE_NLS;
        WCHAR buffer[256];
        int written = GetDateFormatW(lcid, pass, &time, nullptr, buffer, 256);
        if (!written)
            return E_INVALIDARG;
        buffer[written - 1] = L' ';
        if (!GetTimeFormatW(lcid, pass, &time, nullptr, buffer + written, 255 - written))
            return E_INVALIDARG;
        *out = SysAllocString(buffer);
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    if (!split_date(date, &days, &seconds) || !VariantTimeToSystemTime(date, &time))
        return E_INVALIDARG;
    BOOL show_date = !(flags & VAR_TIMEVALUEONLY) && (days || (flags & VAR_DATEVALUEONLY));
    BOOL show_time = !(flags & VAR_DATEVALUEONLY) && (seconds || !show_date);
    LCID locale = lcid ? lcid : LOCALE_USER_DEFAULT;
    DWORD override = flags & LOCALE_NOUSEROVERRIDE;
    WCHAR text[160];
    int n = 0;
    if (show_date) {
        int written = format_short_date(locale, override, &time, text, 80);
        if (!written)
            return E_INVALIDARG;
        n = written - 1;
    }
    if (show_time) {
        if (n)
            text[n++] = L' ';
        if (!GetTimeFormatW(locale, override, &time, nullptr, text + n, (int)RTL_NUMBER_OF(text) - n))
            return E_INVALIDARG;
    } else {
        text[n] = 0;
    }
    *out = SysAllocString(text);
    return *out ? S_OK : E_OUTOFMEMORY;
}

WINOLEAUTAPI HRESULT WINAPI VarUdateFromDate(DATE date, ULONG flags, UDATE *udate)
{
    SYSTEMTIME time;
    if (!udate)
        return E_INVALIDARG;
    if (!VariantTimeToSystemTime(date, &time))
        return E_INVALIDARG;
    udate->st = time;
    udate->wDayOfYear = (USHORT)(days_from_civil(time.wYear, time.wMonth, time.wDay) -
                                 days_from_civil(time.wYear, 1, 1) + 1);
    return S_OK;
}

/* a field of a UDATE past its range rolls into the next one unless the date is to be checked. */
static HRESULT date_from_fields(const SYSTEMTIME *st, ULONG flags, DATE *out)
{
    LONG y = st->wYear, m = st->wMonth, d = st->wDay, h = st->wHour, mi = st->wMinute, s = st->wSecond;
    if (flags & VAR_VALIDDATE) {
        if (m < 1 || m > 12 || d < 1 || y < 100 || y > 9999 || d > days_in_month(y, m) || h > 23 || mi > 59 || s > 59)
            return E_INVALIDARG;
    }
    if (!(flags & VAR_VALIDDATE) && y < 100)
        y = full_year(y);
    /* months into years. */
    LONG month0 = m - 1;
    y += month0 >= 0 ? month0 / 12 : -((11 - month0) / 12);
    month0 = ((month0 % 12) + 12) % 12;
    if (y < 100 || y > 9999)
        return E_INVALIDARG;
    LONGLONG seconds = (LONGLONG)h * 3600 + mi * 60 + s;
    LONGLONG days = (LONGLONG)days_from_civil(y, month0 + 1, 1) - OLE_EPOCH_DAYS + (d - 1);
    days += seconds >= 0 ? seconds / 86400 : -((86399 - seconds) / 86400);
    seconds = ((seconds % 86400) + 86400) % 86400;
    if (days < -657434 || days > 2958465)
        return E_INVALIDARG;
    double time = seconds / 86400.0;
    *out = days >= 0 ? (double)days + time : (double)days - time;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarDateFromUdateEx(UDATE *udate, LCID lcid, ULONG flags, DATE *date)
{
    if (!udate || !date)
        return E_INVALIDARG;
    return date_from_fields(&udate->st, flags, date);
}

WINOLEAUTAPI HRESULT WINAPI VarDateFromUdate(UDATE *udate, ULONG flags, DATE *date)
{
    return VarDateFromUdateEx(udate, 0x409, flags, date);
}

WINOLEAUTAPI INT WINAPI DosDateTimeToVariantTime(USHORT dos_date, USHORT dos_time, DATE *date)
{
    if (!date)
        return FALSE;
    return make_date(1980 + (dos_date >> 9), (dos_date >> 5) & 15, dos_date & 31, dos_time >> 11, (dos_time >> 5) & 63,
                     (dos_time & 31) * 2, date);
}

WINOLEAUTAPI INT WINAPI VariantTimeToDosDateTime(DATE date, USHORT *dos_date, USHORT *dos_time)
{
    SYSTEMTIME time;
    if (!dos_date || !dos_time || !VariantTimeToSystemTime(date, &time) || time.wYear < 1980 || time.wYear > 2107)
        return FALSE;
    *dos_date = (USHORT)(((time.wYear - 1980) << 9) | (time.wMonth << 5) | time.wDay);
    *dos_time = (USHORT)((time.wHour << 11) | (time.wMinute << 5) | (time.wSecond / 2));
    return TRUE;
}
