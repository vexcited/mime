/*
 * variant conversions between strings, dates, currency and decimals.
 *
 * dates are days since 1899-12-30 with the time of day as the fraction,
 * whose sign never changes the time: -1.25 is 1899-12-29 06:00. numbers
 * parse into decimal digits first, then round half to even like windows.
 */
#include "oleaut32_private.h"
#include <mime/stub.h>
#include <mime/win32/winnls.h>

/* the compiler references this symbol from code that uses floating point. */
int _fltused = 0x9875;

#define TYPE_E_CANTLOADLIBRARY ((HRESULT)0x80029C4AL)

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

WINOLEAUTAPI HRESULT WINAPI VarBstrFromDate(DATE date, LCID lcid, ULONG flags, BSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    LONG days, seconds;
    SYSTEMTIME time;
    if (!split_date(date, &days, &seconds) || !VariantTimeToSystemTime(date, &time))
        return E_INVALIDARG;
    BOOL show_date = !(flags & VAR_TIMEVALUEONLY) && (days || (flags & VAR_DATEVALUEONLY));
    BOOL show_time = !(flags & VAR_DATEVALUEONLY) && (seconds || !show_date);
    LCID locale = lcid ? lcid : LOCALE_USER_DEFAULT;
    DWORD override = flags & LOCALE_NOUSEROVERRIDE;
    WCHAR text[160];
    int n = 0;
    if (show_date) {
        int written = GetDateFormatW(locale, DATE_SHORTDATE | override, &time, nullptr, text, 80);
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

/* ------------------------------------------------------------------ numbers */

/* a parsed number: digits without leading zeros, worth digits times ten to the exponent. */
typedef struct {
    BOOL Negative;
    int Count;
    int Exponent;
    BYTE Digits[64];
} NUMBER;

static WCHAR locale_char(LCID lcid, LCTYPE type, WCHAR fallback)
{
    WCHAR text[8];
    if (GetLocaleInfoW(lcid ? lcid : LOCALE_USER_DEFAULT, type, text, RTL_NUMBER_OF(text)) && text[0])
        return text[0];
    return fallback;
}

static HRESULT parse_number(LPCOLESTR string, LCID lcid, ULONG flags, NUMBER *n)
{
    if (!string)
        return E_INVALIDARG;
    WCHAR point = locale_char(lcid, LOCALE_SDECIMAL, L'.'), group = locale_char(lcid, LOCALE_STHOUSAND, L',');
    const WCHAR *p = string;
    memset(n, 0, sizeof(*n));
    while (*p == L' ' || *p == L'\t')
        p++;
    if (*p == L'-' || *p == L'+')
        n->Negative = *p++ == L'-';
    BOOL any = FALSE, fraction = FALSE;
    for (;; p++) {
        if (*p >= L'0' && *p <= L'9') {
            any = TRUE;
            if (!n->Count && *p == L'0') {
                if (fraction)
                    n->Exponent--;
                continue;
            }
            if (n->Count < (int)sizeof(n->Digits)) {
                n->Digits[n->Count++] = (BYTE)(*p - L'0');
                if (fraction)
                    n->Exponent--;
            } else if (!fraction) {
                /* digits past what any type holds still scale the value. */
                n->Exponent++;
            }
        } else if (*p == point && !fraction) {
            fraction = TRUE;
        } else if (*p == group && !fraction && any) {
            continue;
        } else {
            break;
        }
    }
    if (!any)
        return DISP_E_TYPEMISMATCH;
    if ((*p | 0x20) == L'e') {
        const WCHAR *q = p + 1;
        BOOL negative = FALSE;
        if (*q == L'-' || *q == L'+')
            negative = *q++ == L'-';
        if (*q < L'0' || *q > L'9')
            return DISP_E_TYPEMISMATCH;
        int e = 0;
        for (; *q >= L'0' && *q <= L'9'; q++)
            if (e < 10000)
                e = e * 10 + (*q - L'0');
        n->Exponent += negative ? -e : e;
        p = q;
    }
    while (*p == L' ' || *p == L'\t')
        p++;
    if (*p)
        return DISP_E_TYPEMISMATCH;
    /* trailing zeros of the digits move into the exponent. */
    while (n->Count && !n->Digits[n->Count - 1]) {
        n->Count--;
        n->Exponent++;
    }
    return S_OK;
}

/*
 * the value scaled by ten to the given power and rounded half to even, as
 * an unsigned 96-bit integer. FALSE when it does not fit.
 */
static BOOL number_to_integer(const NUMBER *n, int scale, ULONG *high, ULONGLONG *low)
{
    int keep = n->Count + n->Exponent + scale; /* digits before the rounding point */
    ULONGLONG lo = 0;
    ULONG hi = 0;
    for (int i = 0; i < keep; i++) {
        BYTE digit = i < n->Count ? n->Digits[i] : 0;
        /* times ten in 96 bits, then plus the digit. */
        ULONGLONG lo_lo = (lo & 0xFFFFFFFF) * 10, lo_hi = (lo >> 32) * 10 + (lo_lo >> 32);
        ULONGLONG top = (ULONGLONG)hi * 10 + (lo_hi >> 32);
        if (top > 0xFFFFFFFF)
            return FALSE;
        lo = (lo_hi << 32) | (lo_lo & 0xFFFFFFFF);
        hi = (ULONG)top;
        lo += digit;
        if (lo < digit && ++hi == 0)
            return FALSE;
    }
    /* the first dropped digit decides, ties go to the even neighbour; nothing to the left of the digits rounds. */
    BOOL round_up = FALSE;
    if (keep >= 0 && keep < n->Count) {
        BYTE first = n->Digits[keep];
        BOOL rest = keep + 1 < n->Count;
        round_up = first > 5 || (first == 5 && (rest || (lo & 1)));
    }
    if (round_up) {
        if (++lo == 0 && ++hi == 0)
            return FALSE;
    }
    *high = hi;
    *low = lo;
    return TRUE;
}

WINOLEAUTAPI HRESULT WINAPI VarUI4FromStr(LPCOLESTR string, LCID lcid, ULONG flags, ULONG *out)
{
    if (!out)
        return E_INVALIDARG;
    NUMBER n;
    HRESULT hr = parse_number(string, lcid, flags, &n);
    if (FAILED(hr))
        return hr;
    ULONG high;
    ULONGLONG low;
    if (!number_to_integer(&n, 0, &high, &low) || high || low > 0xFFFFFFFF || (n.Negative && low))
        return DISP_E_OVERFLOW;
    *out = (ULONG)low;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarCyFromStr(LPCOLESTR string, LCID lcid, ULONG flags, CY *out)
{
    if (!out)
        return E_INVALIDARG;
    NUMBER n;
    HRESULT hr = parse_number(string, lcid, flags, &n);
    if (FAILED(hr))
        return hr;
    ULONG high;
    ULONGLONG low;
    if (!number_to_integer(&n, 4, &high, &low) || high || low > (n.Negative ? 0x8000000000000000ull : 0x7FFFFFFFFFFFFFFFull))
        return DISP_E_OVERFLOW;
    out->int64 = n.Negative ? (LONGLONG)(0 - low) : (LONGLONG)low;
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI VarDecFromStr(LPCOLESTR string, LCID lcid, ULONG flags, DECIMAL *out)
{
    if (!out)
        return E_INVALIDARG;
    NUMBER n;
    HRESULT hr = parse_number(string, lcid, flags, &n);
    if (FAILED(hr))
        return hr;
    /* the largest scale up to 28 whose value still fits in 96 bits. */
    int scale = n.Exponent < 0 ? -n.Exponent : 0;
    if (scale > 28)
        scale = 28;
    ULONG high;
    ULONGLONG low;
    while (!number_to_integer(&n, scale, &high, &low)) {
        if (!scale)
            return DISP_E_OVERFLOW;
        scale--;
    }
    out->wReserved = 0;
    out->scale = (BYTE)scale;
    out->sign = n.Negative && (high || low) ? 0x80 : 0;
    out->Hi32 = high;
    out->Lo64 = low;
    return S_OK;
}

/* writes the digits of a 96-bit value and a decimal point before the last scale digits, without trailing zeros. */
static HRESULT format_decimal(BOOL negative, ULONG high, ULONGLONG low, int scale, LCID lcid, BSTR *out)
{
    WCHAR digits[40];
    int count = 0;
    ULONG parts[3] = {(ULONG)low, (ULONG)(low >> 32), high};
    do {
        ULONGLONG remainder = 0;
        for (int i = 2; i >= 0; i--) {
            ULONGLONG value = (remainder << 32) | parts[i];
            parts[i] = (ULONG)(value / 10);
            remainder = value % 10;
        }
        digits[count++] = (WCHAR)(L'0' + remainder);
    } while (parts[0] || parts[1] || parts[2]);
    while (count <= scale)
        digits[count++] = L'0';
    int drop = 0;
    while (drop < scale && digits[drop] == L'0')
        drop++;
    WCHAR text[48];
    int n = 0;
    BOOL zero = count - drop == 1 && digits[count - 1] == L'0' && drop == scale;
    if (negative && !zero)
        text[n++] = L'-';
    for (int i = count - 1; i >= scale; i--)
        text[n++] = digits[i];
    if (drop < scale) {
        text[n++] = locale_char(lcid, LOCALE_SDECIMAL, L'.');
        for (int i = scale - 1; i >= drop; i--)
            text[n++] = digits[i];
    }
    text[n] = 0;
    *out = SysAllocString(text);
    return *out ? S_OK : E_OUTOFMEMORY;
}

WINOLEAUTAPI HRESULT WINAPI VarBstrFromCy(CY value, LCID lcid, ULONG flags, BSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    BOOL negative = value.int64 < 0;
    ULONGLONG magnitude = negative ? 0 - (ULONGLONG)value.int64 : (ULONGLONG)value.int64;
    return format_decimal(negative, 0, magnitude, 4, lcid, out);
}

WINOLEAUTAPI HRESULT WINAPI VarBstrFromDec(const DECIMAL *value, LCID lcid, ULONG flags, BSTR *out)
{
    if (!out || !value)
        return E_INVALIDARG;
    if (value->scale > 28 || (value->sign & 0x7F))
        return E_INVALIDARG;
    return format_decimal(value->sign != 0, value->Hi32, value->Lo64, value->scale, lcid, out);
}

/* ------------------------------------------------------------------ type libraries */

/* type library loading needs a parser of the msft and sltg formats, which is not written. */
WINOLEAUTAPI HRESULT WINAPI LoadTypeLib(LPCOLESTR file, void **library)
{
    MIME_STUB("oleaut32");
    if (!library)
        return E_INVALIDARG;
    *library = nullptr;
    return TYPE_E_CANTLOADLIBRARY;
}

WINOLEAUTAPI HRESULT WINAPI RegisterTypeLib(void *library, LPCOLESTR path, LPCOLESTR help_directory)
{
    MIME_STUB("oleaut32");
    return library ? E_NOTIMPL : E_INVALIDARG;
}
