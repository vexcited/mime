/*
 * formatting of dates, times, numbers, currencies and durations for a locale, and the lists of the formats of a locale.
 */
#include "kernelbase_private.h"
#include "nls_locslots.h"

#define NO_LOCALE 0xFFFFFFFFu

/* ------------------------------------------------------------------ output */

typedef struct TEXT_OUT {
    LPWSTR Buffer;
    int Size;
    int Length;
} TEXT_OUT;

static void put(TEXT_OUT *o, WCHAR c)
{
    if (o->Size && o->Length < o->Size)
        o->Buffer[o->Length] = c;
    o->Length++;
}

static void put_text(TEXT_OUT *o, PCWSTR s)
{
    while (*s)
        put(o, *s++);
}

static void put_number(TEXT_OUT *o, UINT value, int digits)
{
    WCHAR buf[16];
    KbFormat(buf, RTL_NUMBER_OF(buf), L"%0*u", digits, value);
    put_text(o, buf);
}

static int finish_text(TEXT_OUT *o)
{
    put(o, 0);
    if (o->Size && o->Length > o->Size) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    return o->Length;
}

static SIZE_T run_length(PCWSTR p)
{
    SIZE_T n = 1;
    while (p[n] == p[0])
        n++;
    return n;
}

/* copies a quoted literal; returns the characters consumed. Two quotes inside it are one, a lone one ends it. */
static SIZE_T put_quoted(TEXT_OUT *o, PCWSTR p)
{
    SIZE_T i = 1;
    for (;;) {
        if (!p[i])
            return i;
        if (p[i] == '\'') {
            if (p[i + 1] == '\'') {
                put(o, '\'');
                i += 2;
                continue;
            }
            return i + 1;
        }
        put(o, p[i++]);
    }
}

#define LS(slot) KbLocaleText(l, (slot))
#define LN(slot) KbLocaleNumber(l, (slot))

/* ------------------------------------------------------------------ dates */

/* a number in the letters of the Hebrew numerals: the hundreds, the tens and the ones, with the marks around the last. */
static void put_hebrew_number(TEXT_OUT *o, int value)
{
    static const WCHAR hundreds[] = {0x05E7, 0x05E8, 0x05E9, 0x05EA};
    static const WCHAR tens[] = {0x05D9, 0x05DB, 0x05DC, 0x05DE, 0x05E0, 0x05E1, 0x05E2, 0x05E4, 0x05E6};
    static const WCHAR ones[] = {0x05D0, 0x05D1, 0x05D2, 0x05D3, 0x05D4, 0x05D5, 0x05D6, 0x05D7, 0x05D8};
    WCHAR letters[16];
    int n = 0;
    value %= 1000;
    while (value >= 400) {
        letters[n++] = hundreds[3];
        value -= 400;
    }
    if (value >= 100) {
        letters[n++] = hundreds[value / 100 - 1];
        value %= 100;
    }
    /* 15 and 16 are not written with the letters of the name of God. */
    if (value == 15 || value == 16) {
        letters[n++] = ones[8];
        letters[n++] = value == 15 ? ones[5] : ones[6];
        value = 0;
    }
    if (value >= 10) {
        letters[n++] = tens[value / 10 - 1];
        value %= 10;
    }
    if (value)
        letters[n++] = ones[value - 1];
    if (n == 1) {
        put(o, letters[0]);
        put(o, '\'');
        return;
    }
    for (int i = 0; i < n; i++) {
        if (i == n - 1)
            put(o, '"');
        put(o, letters[i]);
    }
}

typedef struct DATE_CONTEXT {
    UINT Locale;
    CALID Calendar;
    KB_CALDATE Date;
    LONG Jdn;
} DATE_CONTEXT;

static void put_year(TEXT_OUT *o, const DATE_CONTEXT *c, SIZE_T n)
{
    UINT l = c->Locale;
    int year = c->Date.EraYear;
    if (c->Calendar == CAL_HEBREW && wcsncmp(LS(NLS_S_SISO639LANGNAME), L"he", 2) == 0) {
        put_hebrew_number(o, c->Date.Year);
        return;
    }
    BOOL eras = c->Calendar == CAL_JAPAN || c->Calendar == CAL_TAIWAN;
    if (n == 1)
        put_number(o, (UINT)year % 100 + (eras ? (UINT)year - (UINT)year % 100 : 0), 1);
    else if (n == 2)
        put_number(o, (UINT)year % 100, 2);
    else
        put_number(o, (UINT)year, eras ? 2 : 4);
}

static int format_date(UINT l, const DATE_CONTEXT *c, PCWSTR format, LPWSTR buffer, int size, WCHAR mark)
{
    TEXT_OUT o = {buffer, size, 0};
    const KB_CALDATE *d = &c->Date;
    for (PCWSTR p = format; *p;) {
        WCHAR ch = *p;
        if (ch == '\'') {
            if (mark)
                put(&o, mark);
            p += put_quoted(&o, p);
            continue;
        }
        if (ch != 'd' && ch != 'M' && ch != 'y' && ch != 'g') {
            put(&o, ch);
            p++;
            continue;
        }
        SIZE_T n = run_length(p);
        if (mark)
            put(&o, mark);
        switch (ch) {
        case 'd':
            if (n == 1 || n == 2) {
                if (c->Calendar == CAL_HEBREW && wcsncmp(LS(NLS_S_SISO639LANGNAME), L"he", 2) == 0)
                    put_hebrew_number(&o, d->Day);
                else
                    put_number(&o, (UINT)d->Day, (int)n);
            } else {
                int weekday = (d->DayOfWeek + 6) % 7;
                put_text(&o, KbCalendarDayName(l, c->Calendar, weekday, n == 3));
            }
            break;
        case 'M':
            if (n <= 2) {
                if (c->Calendar == CAL_HEBREW && wcsncmp(LS(NLS_S_SISO639LANGNAME), L"he", 2) == 0)
                    put_hebrew_number(&o, d->Month);
                else
                    put_number(&o, (UINT)d->Month, (int)n);
            } else {
                BOOL after_day = FALSE;
                for (PCWSTR q = format; q < p; q++) {
                    if (*q == '\'') {
                        q++;
                        while (*q && *q != '\'')
                            q++;
                        if (!*q)
                            break;
                    } else if (*q == 'd' && (q + 1 == p || q[1] != 'd' || q[2] != 'd')) {
                        after_day = TRUE;
                    }
                }
                int month = d->Month;
                if (c->Calendar == CAL_HEBREW && !(((7 * d->Year + 1) % 19) < 7) && month >= 7)
                    month++;
                put_text(&o, KbCalendarMonthName(l, c->Calendar, month, n == 3, after_day && n >= 4));
            }
            break;
        case 'y':
            put_year(&o, c, n);
            break;
        case 'g':
            put_text(&o, KbCalendarEraName(l, c->Calendar, d->Era, FALSE));
            break;
        }
        p += n;
    }
    return finish_text(&o);
}

/* a system time as a date of the calendar. */
static BOOL date_context(UINT l, CALID calendar, const SYSTEMTIME *date, DATE_CONTEXT *c)
{
    SYSTEMTIME st;
    if (!date) {
        GetLocalTime(&st);
        date = &st;
    }
    if (date->wYear < 1601 || date->wYear > 30827 || date->wMonth < 1 || date->wMonth > 12 || date->wDay < 1 ||
        date->wDay > KbDaysInGregorianMonth(date->wYear, date->wMonth)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    c->Locale = l;
    c->Calendar = calendar;
    c->Jdn = KbGregorianToJdn(date->wYear, date->wMonth, date->wDay);
    if (!KbCalendarDate(calendar, c->Jdn, &c->Date)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

static int date_format(UINT l, DWORD flags, const SYSTEMTIME *date, LPCWSTR format, LPWSTR buffer, int size)
{
    if (size < 0 || (size && !buffer)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    DWORD kinds = flags & (DATE_SHORTDATE | DATE_LONGDATE | DATE_YEARMONTH);
    DWORD reading = flags & (DATE_LTRREADING | DATE_RTLREADING | DATE_AUTOLAYOUT);
    if ((kinds & (kinds - 1)) || (reading & (reading - 1)) ||
        (flags & ~(DWORD)(DATE_SHORTDATE | DATE_LONGDATE | DATE_USE_ALT_CALENDAR | DATE_YEARMONTH | DATE_LTRREADING |
                          DATE_RTLREADING | DATE_AUTOLAYOUT | LOCALE_NOUSEROVERRIDE | LOCALE_USE_CP_ACP)) ||
        (format && (kinds || (flags & LOCALE_NOUSEROVERRIDE)))) {
        KbSetError(ERROR_INVALID_FLAGS);
        return 0;
    }
    CALID calendar = (CALID)LN(NLS_N_ICALENDARTYPE);
    if (flags & DATE_USE_ALT_CALENDAR) {
        CALID optional = (CALID)LN(NLS_N_IOPTIONALCALENDAR);
        if (optional)
            calendar = optional;
    }
    DATE_CONTEXT c;
    if (!date_context(l, calendar, date, &c))
        return 0;
    if (!format) {
        UINT kind = (flags & DATE_LONGDATE) ? 1 : (flags & DATE_YEARMONTH) ? 2 : 0;
        format = KbCalendarPicture(l, calendar, kind);
    }
    WCHAR mark = 0;
    if (flags & DATE_LTRREADING)
        mark = 0x200E;
    else if (flags & DATE_RTLREADING)
        mark = 0x200F;
    else if (flags & DATE_AUTOLAYOUT)
        mark = LN(NLS_N_IREADINGLAYOUT) == 1 ? 0x200F : 0x200E;
    return format_date(l, &c, format, buffer, size, mark);
}

/* ------------------------------------------------------------------ times */

static int format_time(UINT l, DWORD flags, const SYSTEMTIME *st, PCWSTR format, LPWSTR buffer, int size)
{
    TEXT_OUT o = {buffer, size, 0};
    BOOL force24 = (flags & TIME_FORCE24HOURFORMAT) != 0;
    BOOL skip_minutes = (flags & TIME_NOMINUTESORSECONDS) != 0;
    BOOL skip_seconds = skip_minutes || (flags & TIME_NOSECONDS);
    BOOL skip_marker = (flags & TIME_NOTIMEMARKER) != 0;
    int separator_start = -1; /* where literal text after the last emitted field begins. */
    BOOL drop_literals = FALSE; /* set while the text after a marker left out on request goes with it. */
    int trailing_start = -1;    /* where the text after skipped fields starts, which goes when no field follows. */
    for (PCWSTR p = format; *p;) {
        WCHAR c = *p;
        if (c != 'h' && c != 'H' && c != 'm' && c != 's' && c != 't') {
            int mark = o.Length;
            BOOL quoted = c == '\'';
            if (quoted) {
                p += put_quoted(&o, p);
            } else {
                put(&o, c);
                p++;
            }
            if (drop_literals)
                o.Length = mark;
            else if (quoted)
                separator_start = -1;
            else if (separator_start < 0)
                separator_start = mark;
            continue;
        }
        SIZE_T n = run_length(p);
        /* the marker is the first letter for one t, the shortest form for three and the whole one otherwise. */
        PCWSTR marker = nullptr;
        if (c == 't') {
            BOOL morning = st->wHour < 12;
            marker = n == 3 ? LS(morning ? NLS_S_SSHORTESTAM : NLS_S_SSHORTESTPM) : LS(morning ? NLS_S_S1159 : NLS_S_S2359);
        }
        BOOL skip = (c == 'm' && skip_minutes) || (c == 's' && skip_seconds) || (c == 't' && (skip_marker || !marker[0]));
        if (skip) {
            /* a skipped field takes the text before it along, unless that is quoted. */
            if (separator_start >= 0)
                o.Length = separator_start;
            separator_start = -1;
            if (c == 't') {
                /* a marker left out on request takes the text after it along as well, one that is empty does not. */
                drop_literals = skip_marker;
            } else if (trailing_start < 0) {
                trailing_start = o.Length;
            }
            p += n;
            continue;
        }
        UINT hour12 = st->wHour % 12 ? st->wHour % 12 : 12;
        switch (c) {
        case 'h':
            put_number(&o, force24 ? st->wHour : hour12, n > 1 ? 2 : 1);
            break;
        case 'H':
            put_number(&o, st->wHour, n > 1 ? 2 : 1);
            break;
        case 'm':
            put_number(&o, st->wMinute, n > 1 ? 2 : 1);
            break;
        case 's':
            put_number(&o, st->wSecond, n > 1 ? 2 : 1);
            break;
        case 't':
            if (n == 1)
                put(&o, marker[0]);
            else
                put_text(&o, marker);
            break;
        }
        separator_start = -1;
        drop_literals = FALSE;
        trailing_start = -1;
        p += n;
    }
    if (trailing_start >= 0)
        o.Length = trailing_start;
    return finish_text(&o);
}

static int time_format(UINT l, DWORD flags, const SYSTEMTIME *time, LPCWSTR format, LPWSTR buffer, int size)
{
    if (size < 0 || (size && !buffer)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if ((flags & ~(DWORD)(TIME_NOMINUTESORSECONDS | TIME_NOSECONDS | TIME_NOTIMEMARKER | TIME_FORCE24HOURFORMAT |
                          LOCALE_NOUSEROVERRIDE | LOCALE_USE_CP_ACP)) ||
        (format && (flags & LOCALE_NOUSEROVERRIDE))) {
        KbSetError(ERROR_INVALID_FLAGS);
        return 0;
    }
    SYSTEMTIME st;
    if (time) {
        if (time->wHour > 23 || time->wMinute > 59 || time->wSecond > 59 || time->wMilliseconds > 999) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        st = *time;
    } else {
        GetLocalTime(&st);
    }
    return format_time(l, flags, &st, format ? format : LS(NLS_S_STIMEFORMAT), buffer, size);
}

/* ------------------------------------------------------------------ the entry points of dates and times */

static UINT locale_of_lcid(LCID locale)
{
    UINT l = KbLocaleFromLcid(locale, TRUE);
    if (l == NO_LOCALE)
        KbSetError(ERROR_INVALID_PARAMETER);
    return l;
}

static UINT locale_of_name(LPCWSTR name)
{
    UINT l = KbLocaleFromName(name);
    if (l == NO_LOCALE)
        KbSetError(ERROR_INVALID_PARAMETER);
    return l;
}

int WINAPI GetDateFormatW(LCID locale, DWORD flags, const SYSTEMTIME *date, LPCWSTR format, LPWSTR buffer, int size)
{
    UINT l = locale_of_lcid(locale);
    return l == NO_LOCALE ? 0 : date_format(l, flags, date, format, buffer, size);
}

int WINAPI GetDateFormatEx(LPCWSTR name, DWORD flags, const SYSTEMTIME *date, LPCWSTR format, LPWSTR buffer, int size,
                           LPCWSTR calendar)
{
    if (calendar) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UINT l = locale_of_name(name);
    return l == NO_LOCALE ? 0 : date_format(l, flags, date, format, buffer, size);
}

int WINAPI GetTimeFormatW(LCID locale, DWORD flags, const SYSTEMTIME *time, LPCWSTR format, LPWSTR buffer, int size)
{
    UINT l = locale_of_lcid(locale);
    return l == NO_LOCALE ? 0 : time_format(l, flags, time, format, buffer, size);
}

int WINAPI GetTimeFormatEx(LPCWSTR name, DWORD flags, const SYSTEMTIME *time, LPCWSTR format, LPWSTR buffer, int size)
{
    UINT l = locale_of_name(name);
    return l == NO_LOCALE ? 0 : time_format(l, flags, time, format, buffer, size);
}

/* ansi wrappers share one shape: convert the format, format wide, convert the result. */
static int narrow_result(UINT cp, int wide_result, PCWSTR wide, LPSTR buffer, int size)
{
    if (!wide_result)
        return 0;
    int need = WideCharToMultiByte(cp, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (!size)
        return need;
    if (size < need) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    return WideCharToMultiByte(cp, 0, wide, -1, buffer, size, nullptr, nullptr);
}

int WINAPI GetDateFormatA(LCID locale, DWORD flags, const SYSTEMTIME *date, LPCSTR format, LPSTR buffer, int size)
{
    UINT l = locale_of_lcid(locale);
    if (l == NO_LOCALE)
        return 0;
    if (size < 0 || (size && !buffer)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UINT cp = (flags & LOCALE_USE_CP_ACP) ? CP_ACP : KbLocaleCodePage(locale, 0);
    PWSTR wformat = KbAnsiToWideCp(cp, format);
    if (format && !wformat)
        return 0;
    WCHAR w[512];
    int r = date_format(l, flags, date, wformat, w, RTL_NUMBER_OF(w));
    KbFree(wformat);
    return narrow_result(cp, r, w, buffer, size);
}

int WINAPI GetTimeFormatA(LCID locale, DWORD flags, const SYSTEMTIME *time, LPCSTR format, LPSTR buffer, int size)
{
    UINT l = locale_of_lcid(locale);
    if (l == NO_LOCALE)
        return 0;
    if (size < 0 || (size && !buffer)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UINT cp = (flags & LOCALE_USE_CP_ACP) ? CP_ACP : KbLocaleCodePage(locale, 0);
    PWSTR wformat = KbAnsiToWideCp(cp, format);
    if (format && !wformat)
        return 0;
    WCHAR w[512];
    int r = time_format(l, flags, time, wformat, w, RTL_NUMBER_OF(w));
    KbFree(wformat);
    return narrow_result(cp, r, w, buffer, size);
}

/* ------------------------------------------------------------------ numbers and currencies */

typedef struct NUMBER_PARTS {
    WCHAR Integer[128];
    WCHAR Fraction[128];
    BOOL Negative;
} NUMBER_PARTS;

/* parses and rounds a plain decimal string to the given number of fraction digits. */
static BOOL parse_number(PCWSTR value, UINT digits, NUMBER_PARTS *n)
{
    memset(n, 0, sizeof(*n));
    PCWSTR p = value;
    if (*p == '-') {
        n->Negative = TRUE;
        p++;
    }
    SIZE_T ilen = 0, flen = 0;
    WCHAR fraction[256];
    while (*p >= '0' && *p <= '9') {
        if (ilen >= RTL_NUMBER_OF(n->Integer) - 2)
            return FALSE;
        n->Integer[ilen++] = *p++;
    }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            if (flen >= RTL_NUMBER_OF(fraction) - 1)
                return FALSE;
            fraction[flen++] = *p++;
        }
    }
    if (*p)
        return FALSE;
    if (digits >= RTL_NUMBER_OF(n->Fraction) - 1)
        digits = RTL_NUMBER_OF(n->Fraction) - 2;
    BOOL round_up = flen > digits && fraction[digits] >= '5';
    for (UINT i = 0; i < digits; i++)
        n->Fraction[i] = i < flen ? fraction[i] : '0';
    n->Fraction[digits] = 0;
    if (!ilen)
        n->Integer[ilen++] = '0';
    n->Integer[ilen] = 0;
    if (round_up) {
        int i = (int)digits - 1;
        for (; i >= 0; i--) {
            if (n->Fraction[i] == '9') {
                n->Fraction[i] = '0';
            } else {
                n->Fraction[i]++;
                break;
            }
        }
        if (i < 0) {
            int k = (int)ilen - 1;
            for (; k >= 0; k--) {
                if (n->Integer[k] == '9') {
                    n->Integer[k] = '0';
                } else {
                    n->Integer[k]++;
                    break;
                }
            }
            if (k < 0) {
                memmove(n->Integer + 1, n->Integer, (ilen + 1) * sizeof(WCHAR));
                n->Integer[0] = '1';
            }
        }
    }
    /* strip leading zeros of the integer part. */
    SIZE_T lead = 0;
    while (n->Integer[lead] == '0' && n->Integer[lead + 1])
        lead++;
    memmove(n->Integer, n->Integer + lead, (wcslen(n->Integer + lead) + 1) * sizeof(WCHAR));
    BOOL zero = !wcscmp(n->Integer, L"0");
    for (UINT i = 0; zero && n->Fraction[i]; i++)
        if (n->Fraction[i] != '0')
            zero = FALSE;
    if (zero)
        n->Negative = FALSE;
    return TRUE;
}

/*
 * The grouping is read as decimal digits, the first of them the size of the group at the right. A last digit that is not
 * zero repeats, a last zero ends the groups and the digits left stay together. A zero before the last digit is a group of
 * no digits, which puts the separator twice.
 */
static void put_grouped(TEXT_OUT *o, PCWSTR integer, UINT grouping, PCWSTR separator)
{
    UINT groups[8];
    UINT count = 0;
    BOOL repeat = FALSE;
    if (grouping) {
        WCHAR spec[16];
        KbFormat(spec, RTL_NUMBER_OF(spec), L"%u", grouping);
        SIZE_T len = wcslen(spec);
        for (SIZE_T i = 0; i < len && count < RTL_NUMBER_OF(groups); i++)
            groups[count++] = (UINT)(spec[i] - '0');
        repeat = groups[count - 1] != 0;
        if (!repeat)
            count--;
    }
    SIZE_T n = wcslen(integer);
    /* how many separators go after each position, counted from the right. */
    UINT separators[128] = {};
    SIZE_T pos = 0;
    for (UINT g = 0; count && g < 64; g++) {
        UINT size = g < count ? groups[g] : groups[count - 1];
        if (g >= count && !repeat)
            break;
        pos += size;
        if (pos >= n)
            break;
        if (pos < RTL_NUMBER_OF(separators))
            separators[pos]++;
    }
    for (SIZE_T i = 0; i < n; i++) {
        put(o, integer[i]);
        SIZE_T from_right = n - 1 - i;
        if (from_right && from_right < RTL_NUMBER_OF(separators))
            for (UINT k = 0; k < separators[from_right]; k++)
                put_text(o, separator);
    }
}

static UINT grouping_value(PCWSTR spec)
{
    /* "3;0" means 3 repeating, "3;2;0" means 32, "3" alone means 30. */
    UINT value = 0;
    BOOL ends_zero = FALSE;
    for (PCWSTR p = spec; *p; p++) {
        if (*p >= '1' && *p <= '9') {
            value = value * 10 + (UINT)(*p - '0');
            ends_zero = FALSE;
        } else if (*p == '0') {
            ends_zero = TRUE;
        }
    }
    return ends_zero || !value ? value : value * 10;
}

static void put_number_body(TEXT_OUT *o, const NUMBER_PARTS *n, UINT digits, UINT leading_zero, UINT grouping,
                            PCWSTR decimal, PCWSTR thousand)
{
    if (wcscmp(n->Integer, L"0") || leading_zero || !digits)
        put_grouped(o, n->Integer, grouping, thousand);
    if (digits) {
        put_text(o, decimal);
        put_text(o, n->Fraction);
    }
}

static int number_format(UINT l, DWORD flags, LPCWSTR value, const NUMBERFMTW *format, LPWSTR buffer, int size)
{
    if (!value || size < 0 || (size && !buffer)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if ((format && (flags & LOCALE_NOUSEROVERRIDE)) || (flags & ~(DWORD)(LOCALE_NOUSEROVERRIDE | LOCALE_USE_CP_ACP))) {
        KbSetError(ERROR_INVALID_FLAGS);
        return 0;
    }
    NUMBERFMTW f;
    if (format) {
        if (!format->lpDecimalSep || !format->lpThousandSep || format->NegativeOrder > 4 || format->LeadingZero > 1 ||
            format->Grouping > 9999) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        f = *format;
    } else {
        f.NumDigits = LN(NLS_N_IDIGITS);
        f.LeadingZero = LN(NLS_N_ILZERO);
        f.Grouping = grouping_value(LS(NLS_S_SGROUPING));
        f.lpDecimalSep = (LPWSTR)LS(NLS_S_SDECIMAL);
        f.lpThousandSep = (LPWSTR)LS(NLS_S_STHOUSAND);
        f.NegativeOrder = LN(NLS_N_INEGNUMBER);
    }
    NUMBER_PARTS n;
    if (!parse_number(value, f.NumDigits, &n)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    TEXT_OUT o = {buffer, size, 0};
    if (n.Negative) {
        switch (f.NegativeOrder) {
        case 0:
            put(&o, '(');
            break;
        case 1:
            put_text(&o, LS(NLS_S_SNEGATIVESIGN));
            break;
        case 2:
            put_text(&o, LS(NLS_S_SNEGATIVESIGN));
            put(&o, ' ');
            break;
        }
    }
    put_number_body(&o, &n, f.NumDigits, f.LeadingZero, f.Grouping, f.lpDecimalSep, f.lpThousandSep);
    if (n.Negative) {
        switch (f.NegativeOrder) {
        case 0:
            put(&o, ')');
            break;
        case 3:
            put_text(&o, LS(NLS_S_SNEGATIVESIGN));
            break;
        case 4:
            put(&o, ' ');
            put_text(&o, LS(NLS_S_SNEGATIVESIGN));
            break;
        }
    }
    return finish_text(&o);
}

int WINAPI GetNumberFormatW(LCID locale, DWORD flags, LPCWSTR value, const NUMBERFMTW *format, LPWSTR buffer, int size)
{
    UINT l = locale_of_lcid(locale);
    return l == NO_LOCALE ? 0 : number_format(l, flags, value, format, buffer, size);
}

int WINAPI GetNumberFormatEx(LPCWSTR name, DWORD flags, LPCWSTR value, const NUMBERFMTW *format, LPWSTR buffer, int size)
{
    UINT l = locale_of_name(name);
    return l == NO_LOCALE ? 0 : number_format(l, flags, value, format, buffer, size);
}

int WINAPI GetNumberFormatA(LCID locale, DWORD flags, LPCSTR value, const NUMBERFMTA *format, LPSTR buffer, int size)
{
    UINT l = locale_of_lcid(locale);
    if (l == NO_LOCALE)
        return 0;
    if (!value || size < 0 || (size && !buffer)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UINT cp = (flags & LOCALE_USE_CP_ACP) ? CP_ACP : KbLocaleCodePage(locale, 0);
    PWSTR wvalue = KbAnsiToWideCp(cp, value);
    NUMBERFMTW wf;
    PWSTR dec = nullptr, sep = nullptr;
    if (format) {
        wf.NumDigits = format->NumDigits;
        wf.LeadingZero = format->LeadingZero;
        wf.Grouping = format->Grouping;
        wf.NegativeOrder = format->NegativeOrder;
        dec = KbAnsiToWideCp(cp, format->lpDecimalSep);
        sep = KbAnsiToWideCp(cp, format->lpThousandSep);
        wf.lpDecimalSep = dec;
        wf.lpThousandSep = sep;
    }
    WCHAR w[512];
    int r = wvalue ? number_format(l, flags, wvalue, format ? &wf : nullptr, w, RTL_NUMBER_OF(w)) : 0;
    KbFree(wvalue);
    KbFree(dec);
    KbFree(sep);
    return narrow_result(cp, r, w, buffer, size);
}

static int currency_format(UINT l, DWORD flags, LPCWSTR value, const CURRENCYFMTW *format, LPWSTR buffer, int size)
{
    if (!value || size < 0 || (size && !buffer)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if ((format && (flags & LOCALE_NOUSEROVERRIDE)) || (flags & ~(DWORD)(LOCALE_NOUSEROVERRIDE | LOCALE_USE_CP_ACP))) {
        KbSetError(ERROR_INVALID_FLAGS);
        return 0;
    }
    CURRENCYFMTW f;
    if (format) {
        if (!format->lpDecimalSep || !format->lpThousandSep || !format->lpCurrencySymbol || format->NegativeOrder > 15 ||
            format->PositiveOrder > 3 || format->LeadingZero > 1 || format->Grouping > 9999) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        f = *format;
    } else {
        f.NumDigits = LN(NLS_N_ICURRDIGITS);
        f.LeadingZero = LN(NLS_N_ILZERO);
        f.Grouping = grouping_value(LS(NLS_S_SMONGROUPING));
        f.lpDecimalSep = (LPWSTR)LS(NLS_S_SMONDECIMALSEP);
        f.lpThousandSep = (LPWSTR)LS(NLS_S_SMONTHOUSANDSEP);
        f.NegativeOrder = LN(NLS_N_INEGCURR);
        f.PositiveOrder = LN(NLS_N_ICURRENCY);
        f.lpCurrencySymbol = (LPWSTR)LS(NLS_S_SCURRENCY);
    }
    NUMBER_PARTS n;
    if (!parse_number(value, f.NumDigits, &n)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* each pattern holds literal brackets and spaces, '-' for the sign, '$' for the symbol and '#' for the number. */
    static const PCWSTR positive[] = {L"$#", L"#$", L"$ #", L"# $"};
    static const PCWSTR negative[] = {L"($#)", L"-$#",  L"$-#",  L"$#-",  L"(#$)",  L"-#$",  L"#-$",   L"#$-",
                                      L"-# $", L"-$ #", L"# $-", L"$ #-", L"$ -#", L"#- $", L"($ #)", L"(# $)"};
    PCWSTR pattern = n.Negative ? negative[f.NegativeOrder] : positive[f.PositiveOrder];
    TEXT_OUT o = {buffer, size, 0};
    for (PCWSTR p = pattern; *p; p++) {
        if (*p == '$')
            put_text(&o, f.lpCurrencySymbol);
        else if (*p == '#')
            put_number_body(&o, &n, f.NumDigits, f.LeadingZero, f.Grouping, f.lpDecimalSep, f.lpThousandSep);
        else if (*p == '-')
            put_text(&o, LS(NLS_S_SNEGATIVESIGN));
        else
            put(&o, *p);
    }
    return finish_text(&o);
}

int WINAPI GetCurrencyFormatW(LCID locale, DWORD flags, LPCWSTR value, const CURRENCYFMTW *format, LPWSTR buffer,
                              int size)
{
    UINT l = locale_of_lcid(locale);
    return l == NO_LOCALE ? 0 : currency_format(l, flags, value, format, buffer, size);
}

int WINAPI GetCurrencyFormatEx(LPCWSTR name, DWORD flags, LPCWSTR value, const CURRENCYFMTW *format, LPWSTR buffer,
                               int size)
{
    UINT l = locale_of_name(name);
    return l == NO_LOCALE ? 0 : currency_format(l, flags, value, format, buffer, size);
}

int WINAPI GetCurrencyFormatA(LCID locale, DWORD flags, LPCSTR value, const CURRENCYFMTA *format, LPSTR buffer, int size)
{
    UINT l = locale_of_lcid(locale);
    if (l == NO_LOCALE)
        return 0;
    if (!value || size < 0 || (size && !buffer)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UINT cp = (flags & LOCALE_USE_CP_ACP) ? CP_ACP : KbLocaleCodePage(locale, 0);
    PWSTR wvalue = KbAnsiToWideCp(cp, value);
    CURRENCYFMTW wf;
    PWSTR dec = nullptr, sep = nullptr, symbol = nullptr;
    if (format) {
        wf.NumDigits = format->NumDigits;
        wf.LeadingZero = format->LeadingZero;
        wf.Grouping = format->Grouping;
        wf.NegativeOrder = format->NegativeOrder;
        wf.PositiveOrder = format->PositiveOrder;
        dec = KbAnsiToWideCp(cp, format->lpDecimalSep);
        sep = KbAnsiToWideCp(cp, format->lpThousandSep);
        symbol = KbAnsiToWideCp(cp, format->lpCurrencySymbol);
        wf.lpDecimalSep = dec;
        wf.lpThousandSep = sep;
        wf.lpCurrencySymbol = symbol;
    }
    WCHAR w[512];
    int r = wvalue ? currency_format(l, flags, wvalue, format ? &wf : nullptr, w, RTL_NUMBER_OF(w)) : 0;
    KbFree(wvalue);
    KbFree(dec);
    KbFree(sep);
    KbFree(symbol);
    return narrow_result(cp, r, w, buffer, size);
}

/* ------------------------------------------------------------------ durations */

/*
 * A run of d, h, m or s is that unit of the duration with at least as many digits as the run is long, and a run of f is
 * its fraction with that many digits, nine at most. The highest unit in the picture takes the units above it too, and a
 * run of f that is the highest takes the whole seconds as well and counts the duration in that fraction of a second.
 */
static BOOL duration_unit(WCHAR c)
{
    return c == 'd' || c == 'h' || c == 'H' || c == 'm' || c == 's' || c == 'f';
}

static void put_wide_number(TEXT_OUT *o, ULONGLONG value, SIZE_T digits)
{
    WCHAR text[24];
    int n = 0;
    do {
        text[n++] = (WCHAR)('0' + value % 10);
        value /= 10;
    } while (value);
    for (SIZE_T i = (SIZE_T)n; i < digits; i++)
        put(o, '0');
    while (n)
        put(o, text[--n]);
}

static int duration_format(UINT l, ULONGLONG ticks, PCWSTR format, LPWSTR buffer, int size)
{
    static const ULONGLONG unit[] = {24ull * 3600 * 10000000, 3600ull * 10000000, 60ull * 10000000, 10000000ull, 1};
    if (!format)
        format = LS(NLS_S_SDURATION);
    BOOL present[5] = {};
    for (PCWSTR p = format; *p;) {
        if (*p == '\'') {
            p += put_quoted(&(TEXT_OUT){nullptr, 0, 0}, p);
            continue;
        }
        SIZE_T n = run_length(p);
        if (duration_unit(*p))
            present[*p == 'd' ? 0 : (*p == 'h' || *p == 'H') ? 1 : *p == 'm' ? 2 : *p == 's' ? 3 : 4] = TRUE;
        p += n;
    }
    /* the ticks that are left for each unit, taking away the ones counted by the units above it that are in the picture. */
    ULONGLONG amount[5] = {};
    ULONGLONG rest = ticks;
    int highest = -1;
    for (int i = 0; i < 5; i++) {
        if (!present[i])
            continue;
        if (highest < 0)
            highest = i;
        if (i < 4) {
            amount[i] = rest / unit[i];
            rest %= unit[i];
        }
    }
    TEXT_OUT o = {buffer, size, 0};
    for (PCWSTR p = format; *p;) {
        WCHAR c = *p;
        if (c == '\'') {
            p += put_quoted(&o, p);
            continue;
        }
        SIZE_T n = run_length(p);
        if (!duration_unit(c)) {
            for (SIZE_T i = 0; i < n; i++)
                put(&o, c);
        } else if (c != 'f') {
            put_wide_number(&o, amount[c == 'd' ? 0 : (c == 'h' || c == 'H') ? 1 : c == 'm' ? 2 : 3], n);
        } else {
            SIZE_T digits = n > 9 ? 9 : n;
            if (highest == 4) {
                /* the duration in the unit of that many digits of a second, the ones above counted in. */
                ULONGLONG value = ticks;
                for (SIZE_T i = 7; i < digits; i++)
                    value *= 10;
                for (SIZE_T i = digits; i < 7; i++)
                    value /= 10;
                put_wide_number(&o, value, n);
            } else {
                WCHAR fraction[10];
                ULONGLONG f = rest;
                for (int i = 0; i < 7; i++) {
                    fraction[i] = (WCHAR)('0' + f / 1000000);
                    f = (f % 1000000) * 10;
                }
                fraction[7] = fraction[8] = '0';
                for (SIZE_T i = 0; i < digits; i++)
                    put(&o, fraction[i]);
            }
        }
        p += n;
    }
    return finish_text(&o);
}

static ULONGLONG system_time_duration(const SYSTEMTIME *st)
{
    return (((ULONGLONG)(st->wDay * 24 + st->wHour) * 60 + st->wMinute) * 60 + st->wSecond) * 10000000ull +
           (ULONGLONG)st->wMilliseconds * 10000ull;
}

static int duration_entry(UINT l, DWORD flags, const SYSTEMTIME *duration, ULONGLONG ticks, LPCWSTR format, LPWSTR buffer,
                          int size)
{
    if (flags || size < 0 || (size && !buffer)) {
        KbSetError(flags ? ERROR_INVALID_FLAGS : ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (duration) {
        /* a duration is given by the time or by the ticks, not both. */
        if (ticks) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        ticks = system_time_duration(duration);
    }
    return duration_format(l, ticks, format, buffer, size);
}

int WINAPI GetDurationFormatEx(LPCWSTR name, DWORD flags, const SYSTEMTIME *duration, ULONGLONG ticks, LPCWSTR format,
                               LPWSTR buffer, int size)
{
    UINT l = locale_of_name(name);
    return l == NO_LOCALE ? 0 : duration_entry(l, flags, duration, ticks, format, buffer, size);
}

int WINAPI GetDurationFormat(LCID locale, DWORD flags, const SYSTEMTIME *duration, ULONGLONG ticks, LPCWSTR format,
                             LPWSTR buffer, int size)
{
    UINT l = locale_of_lcid(locale);
    return l == NO_LOCALE ? 0 : duration_entry(l, flags, duration, ticks, format, buffer, size);
}

/* ------------------------------------------------------------------ the formats of a locale */

#define MAX_FORMATS 12

typedef struct FORMAT_LIST {
    PCWSTR Format[MAX_FORMATS];
    WCHAR Storage[MAX_FORMATS][80];
    int Count;
} FORMAT_LIST;

static void add_format(FORMAT_LIST *list, PCWSTR format)
{
    if (!format || !*format || list->Count >= MAX_FORMATS)
        return;
    for (int i = 0; i < list->Count; i++)
        if (!wcscmp(list->Format[i], format))
            return;
    list->Format[list->Count++] = format;
}

/* a copy of a picture with the fields written another way: the year in four digits or two, numbers with a zero before. */
static PCWSTR add_variant(FORMAT_LIST *list, PCWSTR picture, WCHAR field, int from, int to)
{
    WCHAR *out = list->Storage[list->Count < MAX_FORMATS ? list->Count : MAX_FORMATS - 1];
    int n = 0;
    BOOL quoted = FALSE;
    for (PCWSTR p = picture; *p && n < 78; p++) {
        if (*p == '\'')
            quoted = !quoted;
        if (!quoted && *p == field) {
            int run = 1;
            while (p[run] == field)
                run++;
            int count = run == from ? to : run;
            for (int i = 0; i < count && n < 78; i++)
                out[n++] = field;
            p += run - 1;
            continue;
        }
        out[n++] = *p;
    }
    out[n] = 0;
    add_format(list, out);
    return out;
}

static void date_format_list(UINT l, CALID calendar, UINT kind, FORMAT_LIST *list)
{
    PCWSTR primary = KbCalendarPicture(l, calendar, kind);
    add_format(list, primary);
    if (kind == 0) {
        add_variant(list, primary, 'y', 4, 2);
        add_variant(list, primary, 'y', 2, 4);
        add_variant(list, primary, 'M', 1, 2);
        add_variant(list, primary, 'd', 1, 2);
        add_format(list, L"yyyy-MM-dd");
    } else if (kind == 1) {
        add_variant(list, primary, 'd', 1, 2);
    }
}

static void time_format_list(UINT l, BOOL short_form, FORMAT_LIST *list)
{
    PCWSTR primary = KbLocaleText(l, short_form ? NLS_S_SSHORTTIME : NLS_S_STIMEFORMAT);
    add_format(list, primary);
    add_variant(list, primary, 'h', 1, 2);
    add_variant(list, primary, 'H', 1, 2);
    add_variant(list, primary, 'H', 2, 1);
    add_variant(list, primary, 'h', 2, 1);
}

static PWSTR narrow_text(UINT cp, PCWSTR text, char *buffer, int size)
{
    WideCharToMultiByte(cp, 0, text, -1, buffer, size, nullptr, nullptr);
    buffer[size - 1] = 0;
    return (PWSTR)buffer;
}

/* the calendar a list of formats is for. */
static CALID list_calendar(UINT l, DWORD flags)
{
    CALID calendar = (CALID)KbLocaleNumber(l, NLS_N_ICALENDARTYPE);
    if (flags & DATE_USE_ALT_CALENDAR) {
        CALID optional = (CALID)KbLocaleNumber(l, NLS_N_IOPTIONALCALENDAR);
        if (optional)
            calendar = optional;
    }
    return calendar;
}

static BOOL enumerate_dates(UINT l, DWORD flags, DATEFMT_ENUMPROCEXEX proc_ex, LPARAM param, DATEFMT_ENUMPROCW proc,
                            UINT cp, DATEFMT_ENUMPROCA proc_a, DATEFMT_ENUMPROCEXW proc_exw, DATEFMT_ENUMPROCEXA proc_exa)
{
    UINT kind = (flags & DATE_LONGDATE) ? 1 : (flags & DATE_YEARMONTH) ? 2 : 0;
    if ((flags & ~(DWORD)(DATE_SHORTDATE | DATE_LONGDATE | DATE_YEARMONTH | DATE_MONTHDAY | DATE_USE_ALT_CALENDAR |
                          LOCALE_USE_CP_ACP)) ||
        (flags & DATE_MONTHDAY)) {
        if (flags & DATE_MONTHDAY)
            kind = 3;
        else {
            KbSetError(ERROR_INVALID_FLAGS);
            return FALSE;
        }
    }
    CALID calendar = list_calendar(l, flags);
    FORMAT_LIST *list = KbAlloc(sizeof(*list));
    if (!list) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    list->Count = 0;
    date_format_list(l, calendar, kind, list);
    for (int i = 0; i < list->Count; i++) {
        BOOL more;
        char narrow[160];
        if (proc_ex)
            more = proc_ex((LPWSTR)list->Format[i], calendar, param);
        else if (proc_exw)
            more = proc_exw((LPWSTR)list->Format[i], calendar);
        else if (proc_exa)
            more = proc_exa((LPSTR)narrow_text(cp, list->Format[i], narrow, sizeof(narrow)), calendar);
        else if (proc_a)
            more = proc_a((LPSTR)narrow_text(cp, list->Format[i], narrow, sizeof(narrow)));
        else
            more = proc((LPWSTR)list->Format[i]);
        if (!more)
            break;
    }
    KbFree(list);
    return TRUE;
}

BOOL WINAPI EnumDateFormatsW(DATEFMT_ENUMPROCW proc, LCID locale, DWORD flags)
{
    UINT l = locale_of_lcid(locale);
    if (l == NO_LOCALE)
        return FALSE;
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return enumerate_dates(l, flags, nullptr, 0, proc, 0, nullptr, nullptr, nullptr);
}

BOOL WINAPI EnumDateFormatsA(DATEFMT_ENUMPROCA proc, LCID locale, DWORD flags)
{
    UINT l = locale_of_lcid(locale);
    if (l == NO_LOCALE)
        return FALSE;
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return enumerate_dates(l, flags, nullptr, 0, nullptr, (flags & LOCALE_USE_CP_ACP) ? CP_ACP : KbLocaleCodePage(locale, 0), proc,
                           nullptr, nullptr);
}

BOOL WINAPI EnumDateFormatsExW(DATEFMT_ENUMPROCEXW proc, LCID locale, DWORD flags)
{
    UINT l = locale_of_lcid(locale);
    if (l == NO_LOCALE)
        return FALSE;
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return enumerate_dates(l, flags, nullptr, 0, nullptr, 0, nullptr, proc, nullptr);
}

BOOL WINAPI EnumDateFormatsExA(DATEFMT_ENUMPROCEXA proc, LCID locale, DWORD flags)
{
    UINT l = locale_of_lcid(locale);
    if (l == NO_LOCALE)
        return FALSE;
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return enumerate_dates(l, flags, nullptr, 0, nullptr, (flags & LOCALE_USE_CP_ACP) ? CP_ACP : KbLocaleCodePage(locale, 0), nullptr,
                           nullptr, proc);
}

BOOL WINAPI EnumDateFormatsExEx(DATEFMT_ENUMPROCEXEX proc, LPCWSTR name, DWORD flags, LPARAM param)
{
    UINT l = locale_of_name(name);
    if (l == NO_LOCALE)
        return FALSE;
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return enumerate_dates(l, flags, proc, param, nullptr, 0, nullptr, nullptr, nullptr);
}

static BOOL enumerate_times(UINT l, DWORD flags, TIMEFMT_ENUMPROCEX proc_ex, LPARAM param, TIMEFMT_ENUMPROCW proc,
                            UINT cp, TIMEFMT_ENUMPROCA proc_a)
{
    if (flags & ~(DWORD)(TIME_NOSECONDS | TIME_NOMINUTESORSECONDS | LOCALE_USE_CP_ACP)) {
        KbSetError(ERROR_INVALID_FLAGS);
        return FALSE;
    }
    FORMAT_LIST *list = KbAlloc(sizeof(*list));
    if (!list) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    list->Count = 0;
    time_format_list(l, (flags & TIME_NOSECONDS) != 0, list);
    for (int i = 0; i < list->Count; i++) {
        BOOL more;
        char narrow[160];
        if (proc_ex)
            more = proc_ex((LPWSTR)list->Format[i], param);
        else if (proc_a)
            more = proc_a((LPSTR)narrow_text(cp, list->Format[i], narrow, sizeof(narrow)));
        else
            more = proc((LPWSTR)list->Format[i]);
        if (!more)
            break;
    }
    KbFree(list);
    return TRUE;
}

BOOL WINAPI EnumTimeFormatsW(TIMEFMT_ENUMPROCW proc, LCID locale, DWORD flags)
{
    UINT l = locale_of_lcid(locale);
    if (l == NO_LOCALE)
        return FALSE;
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return enumerate_times(l, flags, nullptr, 0, proc, 0, nullptr);
}

BOOL WINAPI EnumTimeFormatsA(TIMEFMT_ENUMPROCA proc, LCID locale, DWORD flags)
{
    UINT l = locale_of_lcid(locale);
    if (l == NO_LOCALE)
        return FALSE;
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return enumerate_times(l, flags, nullptr, 0, nullptr, (flags & LOCALE_USE_CP_ACP) ? CP_ACP : KbLocaleCodePage(locale, 0), proc);
}

BOOL WINAPI EnumTimeFormatsEx(TIMEFMT_ENUMPROCEX proc, LPCWSTR name, DWORD flags, LPARAM param)
{
    UINT l = locale_of_name(name);
    if (l == NO_LOCALE)
        return FALSE;
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return enumerate_times(l, flags, proc, param, nullptr, 0, nullptr);
}

/* ------------------------------------------------------------------ the values of calendars */

static BOOL enumerate_calendar_info(UINT l, CALID calendar, CALTYPE type, CALINFO_ENUMPROCEXEX proc_exex, LPARAM param,
                                    CALINFO_ENUMPROCW proc, CALINFO_ENUMPROCEXW proc_exw, UINT cp, CALINFO_ENUMPROCA proc_a,
                                    CALINFO_ENUMPROCEXA proc_exa)
{
    const UINT8 *list = KbLocaleCalendars(l);
    CALTYPE base = type & ~(CAL_RETURN_NUMBER | CAL_NOUSEROVERRIDE | CAL_USE_CP_ACP);
    if (calendar != ENUM_ALL_CALENDARS && (!KbValidCalendar(calendar) || !KbLocaleSupportsCalendar(l, calendar))) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (int i = 0; i < 8 && list[i]; i++) {
        CALID cal = list[i];
        if (calendar != ENUM_ALL_CALENDARS && cal != calendar)
            continue;
        WCHAR text[160];
        UINT kind = base == CAL_SSHORTDATE ? 0 : base == CAL_SLONGDATE ? 1 : base == CAL_SYEARMONTH ? 2 : base == CAL_SMONTHDAY ? 3 : 4;
        FORMAT_LIST *formats = nullptr;
        int count = 1;
        if (kind < 4) {
            formats = KbAlloc(sizeof(*formats));
            if (!formats)
                continue;
            formats->Count = 0;
            if (kind == 3)
                add_format(formats, KbCalendarPicture(l, cal, 3));
            else
                date_format_list(l, cal, kind, formats);
            count = formats->Count;
        }
        for (int k = 0; k < count; k++) {
            PCWSTR value;
            if (formats) {
                value = formats->Format[k];
            } else {
                if (!KbCalendarInfo(l, cal, type & ~CAL_RETURN_NUMBER, text, RTL_NUMBER_OF(text), nullptr))
                    break;
                value = text;
            }
            char narrow[320];
            BOOL more;
            if (proc_exex)
                more = proc_exex((LPWSTR)value, cal, nullptr, param);
            else if (proc_exw)
                more = proc_exw((LPWSTR)value, cal);
            else if (proc_exa)
                more = proc_exa((LPSTR)narrow_text(cp, value, narrow, sizeof(narrow)), cal);
            else if (proc_a)
                more = proc_a((LPSTR)narrow_text(cp, value, narrow, sizeof(narrow)));
            else
                more = proc((LPWSTR)value);
            if (!more) {
                KbFree(formats);
                return TRUE;
            }
        }
        KbFree(formats);
    }
    return TRUE;
}

BOOL WINAPI EnumCalendarInfoW(CALINFO_ENUMPROCW proc, LCID locale, CALID calendar, CALTYPE type)
{
    UINT l = locale_of_lcid(locale);
    if (l == NO_LOCALE)
        return FALSE;
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return enumerate_calendar_info(l, calendar, type, nullptr, 0, proc, nullptr, 0, nullptr, nullptr);
}

BOOL WINAPI EnumCalendarInfoA(CALINFO_ENUMPROCA proc, LCID locale, CALID calendar, CALTYPE type)
{
    UINT l = locale_of_lcid(locale);
    if (l == NO_LOCALE)
        return FALSE;
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return enumerate_calendar_info(l, calendar, type, nullptr, 0, nullptr, nullptr,
                                   (type & CAL_USE_CP_ACP) ? CP_ACP : KbLocaleCodePage(locale, 0), proc, nullptr);
}

BOOL WINAPI EnumCalendarInfoExW(CALINFO_ENUMPROCEXW proc, LCID locale, CALID calendar, CALTYPE type)
{
    UINT l = locale_of_lcid(locale);
    if (l == NO_LOCALE)
        return FALSE;
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return enumerate_calendar_info(l, calendar, type, nullptr, 0, nullptr, proc, 0, nullptr, nullptr);
}

BOOL WINAPI EnumCalendarInfoExA(CALINFO_ENUMPROCEXA proc, LCID locale, CALID calendar, CALTYPE type)
{
    UINT l = locale_of_lcid(locale);
    if (l == NO_LOCALE)
        return FALSE;
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return enumerate_calendar_info(l, calendar, type, nullptr, 0, nullptr, nullptr,
                                   (type & CAL_USE_CP_ACP) ? CP_ACP : KbLocaleCodePage(locale, 0), nullptr, proc);
}

BOOL WINAPI EnumCalendarInfoExEx(CALINFO_ENUMPROCEXEX proc, LPCWSTR name, CALID calendar, LPCWSTR reserved, CALTYPE type,
                                 LPARAM param)
{
    UINT l = locale_of_name(name);
    if (l == NO_LOCALE)
        return FALSE;
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return enumerate_calendar_info(l, calendar, type, proc, param, nullptr, nullptr, 0, nullptr, nullptr);
}
