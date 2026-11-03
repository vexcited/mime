/*
 * calendar arithmetic, calendar names per locale and the calendar info functions. every calendar converts through the
 * Julian day number of the Gregorian date the system time carries.
 */
#include "kernelbase_private.h"
#include "nls_locslots.h"

/* ------------------------------------------------------------------ day numbers */

#define JDN_UNIX_EPOCH 2440588
#define JDN_1601 2305814 /* 1 January 1601. */

/* Julian day number of a Gregorian date, counted from noon on 24 November 4714 BC. */
static LONG gregorian_to_jdn(int year, int month, int day)
{
    int a = (14 - month) / 12;
    int y = year + 4800 - a;
    int m = month + 12 * a - 3;
    return day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
}

static void jdn_to_gregorian(LONG jdn, int *year, int *month, int *day)
{
    LONG a = jdn + 32044;
    LONG b = (4 * a + 3) / 146097;
    LONG c = a - 146097 * b / 4;
    LONG d = (4 * c + 3) / 1461;
    LONG e = c - 1461 * d / 4;
    LONG m = (5 * e + 2) / 153;
    *day = (int)(e - (153 * m + 2) / 5 + 1);
    *month = (int)(m + 3 - 12 * (m / 10));
    *year = (int)(100 * b + d - 4800 + m / 10);
}

static int day_of_week(LONG jdn)
{
    return (int)((jdn + 1) % 7);
}

static BOOL gregorian_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

/* ------------------------------------------------------------------ calendar dates */

/* the arithmetic Hijri calendar counts days from this absolute day number, the one of 1 January 1 being 0. */
#define HIJRI_FIRST_ABSOLUTE 227013
#define ABSOLUTE_FROM_JDN 1721426

static BOOL hijri_leap(UINT y)
{
    return (11 * y + 14) % 30 < 11;
}

static UINT hijri_year_days(UINT y)
{
    return 354 + hijri_leap(y);
}

/* the first day of a year as an absolute day number. */
static UINT hijri_year_start(UINT y)
{
    UINT cycles = (y - 1) / 30;
    UINT start = 318930 * cycles / 30 + HIJRI_FIRST_ABSOLUTE;
    UINT left = y - 30 * cycles - 1;
    UINT t = 11 * left + 14;
    for (UINT i = left; i; i--) {
        start += (t % 30 < 11) + 354;
        t -= 11;
    }
    return start;
}

static void jdn_to_islamic(LONG jdn, int *y, int *m, int *d)
{
    static const UINT month_days[] = {0, 30, 59, 89, 118, 148, 177, 207, 236, 266, 295, 325, 355};
    UINT day = (UINT)(jdn - ABSOLUTE_FROM_JDN) + 1;
    UINT estimate = (30 * day - 6810390) / 10631;
    UINT year = estimate + 1;
    UINT start = hijri_year_start(year);
    if (day >= start) {
        if (day == start) {
            year = estimate;
            start -= hijri_year_days(estimate);
        } else if (day > start + hijri_year_days(year)) {
            start += hijri_year_days(year);
            year = estimate + 2;
        }
    } else {
        start -= hijri_year_days(year);
        year = estimate;
    }
    UINT in_year = day - start;
    UINT month = 1;
    while (month <= 12 && in_year > month_days[month - 1])
        month++;
    *y = (int)year;
    *m = (int)month - 1;
    *d = (int)(in_year - month_days[month - 2]);
}

/* Um Al Qura months come from a table. */
#define UMALQURA_FIRST_YEAR 1318
#define UMALQURA_MONTHS 2196

static UINT16 g_umalqura_start[UMALQURA_MONTHS + 1]; /* days since the first day of the table. */
static LONG g_umalqura_jdn0;
static volatile LONG g_umalqura_ready;

extern const UINT8 g_nls_persian_newyear[]; /* the day of March the Persian year starts on, for the years from 1601. */
extern const UINT8 g_nls_umalqura_bits[]; /* one bit per month, 1 for a month of 30 days. */
extern UINT KbLocaleCount(void);

static void umalqura_setup(void)
{
    if (g_umalqura_ready)
        return;
    UINT16 days = 0;
    for (int i = 0; i < UMALQURA_MONTHS; i++) {
        g_umalqura_start[i] = days;
        days = (UINT16)(days + 29 + ((g_nls_umalqura_bits[i >> 3] >> (i & 7)) & 1));
    }
    g_umalqura_start[UMALQURA_MONTHS] = days;
    g_umalqura_jdn0 = gregorian_to_jdn(1900, 4, 30);
    InterlockedExchange(&g_umalqura_ready, 1);
}

static BOOL jdn_to_umalqura(LONG jdn, int *y, int *m, int *d)
{
    umalqura_setup();
    LONG day = jdn - g_umalqura_jdn0;
    if (day < 0 || day >= g_umalqura_start[UMALQURA_MONTHS])
        return FALSE;
    int lo = 0, hi = UMALQURA_MONTHS - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (g_umalqura_start[mid] <= day)
            lo = mid;
        else
            hi = mid - 1;
    }
    *y = UMALQURA_FIRST_YEAR + lo / 12;
    *m = lo % 12 + 1;
    *d = (int)(day - g_umalqura_start[lo]) + 1;
    return TRUE;
}

/* ------------------------------------------------------------------ Hebrew calendar */

static LONG hebrew_elapsed_days(int year)
{
    LONG months = (235L * year - 234) / 19;
    LONG parts = 12084 + 13753L * months;
    LONG day = 29 * months + parts / 25920;
    if ((3 * (day + 1)) % 7 < 3)
        day++;
    return day;
}

static int hebrew_new_year_delay(int year)
{
    LONG previous = hebrew_elapsed_days(year - 1), this_year = hebrew_elapsed_days(year), next = hebrew_elapsed_days(year + 1);
    if (next - this_year == 356)
        return 2;
    if (this_year - previous == 382)
        return 1;
    return 0;
}

/* the day number of 1 Tishri. */
static LONG hebrew_new_year(int year)
{
    return -1373427 + 1721425 + hebrew_elapsed_days(year) + hebrew_new_year_delay(year);
}

static BOOL hebrew_leap(int year)
{
    return (7 * year + 1) % 19 < 7;
}

static int hebrew_months_in_year(int year)
{
    return hebrew_leap(year) ? 13 : 12;
}

/* months count from Tishri, a leap year's two Adars being the sixth and seventh. */
static int hebrew_days_in_month(int year, int month)
{
    int length = (int)(hebrew_new_year(year + 1) - hebrew_new_year(year));
    BOOL leap = hebrew_leap(year);
    if (!leap && month >= 7)
        month++; /* months after Adar are numbered one higher in a leap year. */
    switch (month) {
    case 1:
    case 5:
    case 8:
    case 10:
    case 12:
        return 30;
    case 2:
        return length % 10 == 5 ? 30 : 29;
    case 3:
        return length % 10 == 3 ? 29 : 30;
    case 4:
    case 9:
    case 11:
    case 13:
        return 29;
    case 6:
        return leap ? 30 : 29;
    case 7:
        return 29;
    default:
        return 29;
    }
}

static void jdn_to_hebrew(LONG jdn, int *y, int *m, int *d)
{
    int year = (int)((jdn - (-1373427 + 1721425)) * 98496 / 35975351) + 1;
    while (hebrew_new_year(year) > jdn)
        year--;
    while (hebrew_new_year(year + 1) <= jdn)
        year++;
    LONG day = jdn - hebrew_new_year(year);
    int month = 1;
    int months = hebrew_months_in_year(year);
    while (month < months && day >= hebrew_days_in_month(year, month)) {
        day -= hebrew_days_in_month(year, month);
        month++;
    }
    *y = year;
    *m = month;
    *d = (int)day + 1;
}

/* ------------------------------------------------------------------ Persian calendar */

/* the Persian year starts on the day of March the table says, which windows sets by the sun. */
static LONG persian_new_year(int gregorian_year)
{
    if (gregorian_year < 1601)
        return gregorian_to_jdn(1601, 1, 1) - NLS_PERSIAN_DAYS_BEFORE_1601;
    if (gregorian_year > 9999)
        gregorian_year = 9999;
    return gregorian_to_jdn(gregorian_year, 3, g_nls_persian_newyear[gregorian_year - 1601]);
}

static void jdn_to_persian(LONG jdn, int *y, int *m, int *d)
{
    int g, gm, gd;
    jdn_to_gregorian(jdn, &g, &gm, &gd);
    LONG start = persian_new_year(g);
    if (jdn < start)
        start = persian_new_year(--g);
    int day = (int)(jdn - start);
    *y = g - 621;
    if (day < 186) {
        *m = day / 31 + 1;
        *d = day % 31 + 1;
    } else {
        *m = 7 + (day - 186) / 30;
        *d = (day - 186) % 30 + 1;
    }
}

/* ------------------------------------------------------------------ Japanese eras */

typedef struct JAPANESE_ERA {
    int Year, Month, Day; /* the first day of the era. */
} JAPANESE_ERA;

/* newest first, as windows lists them: a date's era is the first one that does not start after it. */
static constexpr JAPANESE_ERA g_japanese_eras[] = {
    {2019, 5, 1}, {1989, 1, 8}, {1926, 12, 25}, {1912, 7, 30}, {1868, 1, 1},
};
#define JAPANESE_ERAS 5

/* ------------------------------------------------------------------ calendars */

static BOOL gregorian_calendar(CALID cal)
{
    return cal == CAL_GREGORIAN || cal == CAL_GREGORIAN_US || cal == CAL_GREGORIAN_ME_FRENCH ||
           cal == CAL_GREGORIAN_ARABIC || cal == CAL_GREGORIAN_XLIT_ENGLISH || cal == CAL_GREGORIAN_XLIT_FRENCH;
}

BOOL KbValidCalendar(CALID cal)
{
    return gregorian_calendar(cal) || cal == CAL_JAPAN || cal == CAL_TAIWAN || cal == CAL_KOREA || cal == CAL_HIJRI ||
           cal == CAL_THAI || cal == CAL_HEBREW || cal == CAL_PERSIAN || cal == CAL_UMALQURA;
}

/* the first and last day number a calendar supports. */
static void calendar_range(CALID cal, LONG *first, LONG *last)
{
    *first = JDN_1601;
    *last = gregorian_to_jdn(32767, 12, 31);
    switch (cal) {
    case CAL_HIJRI:
        *last = gregorian_to_jdn(9999, 12, 31);
        break;
    case CAL_PERSIAN:
        *last = gregorian_to_jdn(9999, 12, 31);
        break;
    case CAL_HEBREW:
        *first = gregorian_to_jdn(1583, 1, 1);
        *last = gregorian_to_jdn(2239, 9, 29);
        if (*first < JDN_1601)
            *first = JDN_1601;
        break;
    case CAL_UMALQURA:
        *first = gregorian_to_jdn(1900, 4, 30);
        *last = gregorian_to_jdn(2077, 11, 16);
        break;
    default:
        break;
    }
}

LONG KbGregorianToJdn(int year, int month, int day)
{
    return gregorian_to_jdn(year, month, day);
}

int KbDaysInGregorianMonth(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && gregorian_leap(year) ? 29 : days[month - 1];
}

/* a date in a calendar, with era and era year for calendars that have them. */
BOOL KbCalendarDate(CALID cal, LONG jdn, KB_CALDATE *out)
{
    LONG first, last;
    calendar_range(cal, &first, &last);
    if (jdn < first || jdn > last)
        return FALSE;
    memset(out, 0, sizeof(*out));
    out->DayOfWeek = day_of_week(jdn);
    int y, m, d;
    switch (cal) {
    case CAL_HIJRI:
        jdn_to_islamic(jdn, &y, &m, &d);
        out->Era = 1;
        break;
    case CAL_UMALQURA:
        if (!jdn_to_umalqura(jdn, &y, &m, &d))
            return FALSE;
        out->Era = 1;
        break;
    case CAL_HEBREW:
        jdn_to_hebrew(jdn, &y, &m, &d);
        out->Era = 1;
        break;
    case CAL_PERSIAN:
        jdn_to_persian(jdn, &y, &m, &d);
        out->Era = 1;
        break;
    default:
        jdn_to_gregorian(jdn, &y, &m, &d);
        out->Era = 1;
        break;
    }
    out->Year = y;
    out->Month = m;
    out->Day = d;
    out->EraYear = y;
    switch (cal) {
    case CAL_THAI:
        out->Year = y + 543;
        out->EraYear = out->Year;
        break;
    case CAL_KOREA:
        out->Year = y + 2333;
        out->EraYear = out->Year;
        break;
    case CAL_TAIWAN:
        if (y >= 1912) {
            out->Year = y - 1911;
            out->Era = 1;
        } else {
            out->Year = 1912 - y;
            out->Era = 2;
        }
        out->EraYear = out->Year;
        break;
    case CAL_JAPAN: {
        out->Era = 1;
        out->EraYear = y;
        for (int i = 0; i < JAPANESE_ERAS; i++) {
            if (jdn >= gregorian_to_jdn(g_japanese_eras[i].Year, g_japanese_eras[i].Month, g_japanese_eras[i].Day)) {
                out->Era = JAPANESE_ERAS - i;
                out->EraYear = y - g_japanese_eras[i].Year + 1;
                break;
            }
        }
        break;
    }
    default:
        break;
    }
    return TRUE;
}

int KbCalendarMonthsInYear(CALID cal, int year)
{
    if (cal == CAL_HEBREW)
        return hebrew_months_in_year(year);
    return 12;
}

/* ------------------------------------------------------------------ calendar data */

BOOL KbLocaleSupportsCalendar(UINT locale, CALID cal)
{
    return KbLocaleCalendarText(locale, cal, 0) != nullptr;
}

/* a calendar's text in a locale, or the Gregorian one when the locale does not have the calendar. */
static PCWSTR record_text(UINT locale, CALID cal, UINT slot)
{
    PCWSTR text = KbLocaleCalendarText(locale, cal, slot);
    return text ? text : KbLocaleCalendarText(locale, CAL_GREGORIAN, slot);
}

PCWSTR KbCalendarDayName(UINT locale, CALID cal, int weekday, BOOL abbreviated)
{
    return record_text(locale, cal, (abbreviated ? CAL_SABBREVDAYNAME1 : CAL_SDAYNAME1) + weekday);
}

PCWSTR KbCalendarMonthName(UINT locale, CALID cal, int month, BOOL abbreviated, BOOL genitive)
{
    if (month < 1 || month > 13)
        return L"";
    UINT type = (abbreviated ? CAL_SABBREVMONTHNAME1 : CAL_SMONTHNAME1) + month - 1;
    return record_text(locale, cal, genitive ? type + NLS_CALREC_GENITIVE : type);
}

/* the n-th string of a list of strings that ends with an empty one, or NULL. */
static PCWSTR list_item(PCWSTR list, int n)
{
    for (; *list; list += wcslen(list) + 1, n--)
        if (!n)
            return list;
    return nullptr;
}

/* Gregorian dates are in the anno domini era, the Japanese calendar has the names of its eras in a list. */
PCWSTR KbCalendarEraName(UINT locale, CALID cal, int era, BOOL abbreviated)
{
    CALTYPE type = abbreviated ? CAL_SABBREVERASTRING : CAL_SERASTRING;
    if (cal == CAL_JAPAN && era >= 1 && era <= JAPANESE_ERAS) {
        PCWSTR list = KbLocaleCalendarList(locale, cal, type);
        PCWSTR text = list ? list_item(list, JAPANESE_ERAS - era) : nullptr;
        if (text)
            return text;
    }
    return record_text(locale, cal, type);
}

/* ------------------------------------------------------------------ calendar information */

/* a calendar's date format of the given kind in a locale. */
PCWSTR KbCalendarPicture(UINT locale, CALID cal, UINT kind)
{
    static const UINT types[] = {CAL_SSHORTDATE, CAL_SLONGDATE, CAL_SYEARMONTH, CAL_SMONTHDAY};
    return record_text(locale, cal, types[kind]);
}

int KbCalendarInfo(UINT locale, CALID calendar, CALTYPE type, LPWSTR data, int size, LPDWORD value)
{
    BOOL want_number = (type & CAL_RETURN_NUMBER) != 0;
    BOOL genitive = (type & LOCALE_RETURN_GENITIVE_NAMES) != 0;
    CALTYPE base = type & ~(CAL_RETURN_NUMBER | CAL_NOUSEROVERRIDE | CAL_USE_CP_ACP | LOCALE_RETURN_GENITIVE_NAMES);
    if (size < 0 || (size && !data)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UINT offset_range, two_digit_max;
    if (!KbLocaleCalendarFacts(calendar, &offset_range, &two_digit_max) ||
        (base != CAL_ITWODIGITYEARMAX && !KbLocaleSupportsCalendar(locale, calendar))) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UINT kind = KbLocaleCalendarTypeKind(base);
    PCWSTR text = nullptr;
    DWORD number = 0;
    if (kind == 1) {
        number = base == CAL_ICALINTVALUE ? calendar : base == CAL_IYEAROFFSETRANGE ? offset_range : two_digit_max;
    } else if (kind == 0 || (kind & 0x8000)) {
        UINT slot = genitive && base >= CAL_SMONTHNAME1 && base <= CAL_SABBREVMONTHNAME13 ? base + NLS_CALREC_GENITIVE : base;
        text = KbLocaleCalendarText(locale, calendar, slot);
        if (!text || ((kind & 0x8000) && !*text)) {
            KbSetError(kind & 0x7FFF);
            return 0;
        }
    } else {
        KbSetError(kind);
        return 0;
    }
    BOOL numeric = kind == 1;
    if (want_number) {
        if (!numeric) {
            KbSetError(ERROR_INVALID_FLAGS);
            return 0;
        }
        if (value)
            *value = number;
        return sizeof(DWORD) / sizeof(WCHAR);
    }
    WCHAR buffer[16];
    if (numeric) {
        KbFormat(buffer, RTL_NUMBER_OF(buffer), L"%u", number);
        text = buffer;
    }
    int n = (int)wcslen(text) + 1;
    if (!size)
        return n;
    if (size < n) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(data, text, (SIZE_T)n * sizeof(WCHAR));
    return n;
}

static UINT locale_or_error(LCID lcid)
{
    UINT l = KbLocaleFromLcid(lcid, TRUE);
    if (l == 0xFFFFFFFFu)
        KbSetError(ERROR_INVALID_PARAMETER);
    return l;
}

int WINAPI GetCalendarInfoW(LCID locale, CALID calendar, CALTYPE type, LPWSTR data, int size, LPDWORD value)
{
    UINT l = locale_or_error(locale);
    return l == 0xFFFFFFFFu ? 0 : KbCalendarInfo(l, calendar, type, data, size, value);
}

int WINAPI GetCalendarInfoEx(LPCWSTR name, CALID calendar, LPCWSTR reserved, CALTYPE type, LPWSTR data, int size,
                             LPDWORD value)
{
    UINT l = KbLocaleFromName(name);
    if (l == 0xFFFFFFFFu) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return KbCalendarInfo(l, calendar, type, data, size, value);
}

int WINAPI GetCalendarInfoA(LCID locale, CALID calendar, CALTYPE type, LPSTR data, int size, LPDWORD value)
{
    UINT l = locale_or_error(locale);
    if (l == 0xFFFFFFFFu)
        return 0;
    if (size < 0 || (size && !data)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (type & CAL_RETURN_NUMBER)
        return KbCalendarInfo(l, calendar, type, nullptr, 0, value);
    WCHAR w[256];
    if (!KbCalendarInfo(l, calendar, type, w, RTL_NUMBER_OF(w), value))
        return 0;
    UINT cp = (type & CAL_USE_CP_ACP) ? CP_ACP : KbLocaleCodePage(locale, 0);
    int need = WideCharToMultiByte(cp, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (!size)
        return need;
    if (size < need) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    return WideCharToMultiByte(cp, 0, w, -1, data, size, nullptr, nullptr);
}

BOOL WINAPI SetCalendarInfoW(LCID locale, CALID calendar, CALTYPE type, LPCWSTR data)
{
    KbSetError(ERROR_ACCESS_DENIED);
    return FALSE;
}

BOOL WINAPI SetCalendarInfoA(LCID locale, CALID calendar, CALTYPE type, LPCSTR data)
{
    KbSetError(ERROR_ACCESS_DENIED);
    return FALSE;
}
