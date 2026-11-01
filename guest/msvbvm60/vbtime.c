/*
 * dates and times: Now, Date, Time, Timer, the parts of a date, DateAdd, DateDiff, DateSerial and the names that
 * oleaut32 formats.
 */
#include "vb.h"

static int g_calendar;

VB_STD SHORT vb_rtcGetCurrentCalendar(void) { return (SHORT)g_calendar; }

VB_STD void vb_rtcSetCurrentCalendar(UINT calendar)
{
    if (calendar > 1)
        VbRaise(VBERR_INVALID_CALL);
    g_calendar = (int)calendar;
}

static ULONG calendar_flags(void) { return g_calendar == 1 ? VAR_CALENDAR_HIJRI : 0; }

static LCID user_locale(void)
{
    LCID lcid = GetUserDefaultLCID();
    return lcid ? lcid : 0x409;
}

static VARIANT *return_date(VARIANT *dest, DATE date)
{
    VariantInit(dest);
    V_VT(dest) = VT_DATE;
    V_DATE(dest) = date;
    return dest;
}

static VARIANT *return_null(VARIANT *dest)
{
    VariantInit(dest);
    V_VT(dest) = VT_NULL;
    return dest;
}

static VARIANT *return_short(VARIANT *dest, SHORT value)
{
    VariantInit(dest);
    V_VT(dest) = VT_I2;
    V_I2(dest) = value;
    return dest;
}

static double vb_floor(double x)
{
    double whole = (double)(long long)x;
    return whole > x ? whole - 1 : whole;
}

static double vb_ceil(double x)
{
    double whole = (double)(long long)x;
    return whole < x ? whole + 1 : whole;
}

static double round_even(double x)
{
    double f = vb_floor(x);
    double d = x - f;
    if (d > 0.5 || (d == 0.5 && ((long long)f & 1)))
        return f + 1;
    return f;
}

/* the date of a local time, as the date variant holds it. */
static DATE now_date(void)
{
    SYSTEMTIME st;
    DATE date = 0;
    GetLocalTime(&st);
    st.wMilliseconds = 0;
    SystemTimeToVariantTime(&st, &date);
    return date;
}

static void udate_of(const VARIANT *v, UDATE *udate)
{
    VbCheck(VarUdateFromDate(vb___vbaDateVar((VARIANT *)v), calendar_flags(), udate));
}

VB_STD VARIANT *vb_rtcGetPresentDate(VARIANT *dest) { return return_date(dest, now_date()); }

VB_STD VARIANT *vb_rtcGetDateVar(VARIANT *dest)
{
    DATE now = now_date();
    return return_date(dest, vb_floor(now));
}

VB_STD VARIANT *vb_rtcGetTimeVar(VARIANT *dest)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    return return_date(dest, (double)(st.wSecond + 60 * (st.wMinute + 60 * st.wHour)) / 86400.0);
}

VB_STD double vb_rtcGetTimer(void)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    return (double)(st.wSecond + 60 * (st.wMinute + 60 * st.wHour)) + (double)st.wMilliseconds * 0.001;
}

VB_STD BSTR vb_rtcGetDateBstr(void)
{
    SYSTEMTIME st;
    WCHAR text[16];
    GetLocalTime(&st);
    wsprintfW(text, L"%02u-%02u-%04u", st.wMonth, st.wDay, st.wYear);
    BSTR b = SysAllocString(text);
    if (!b)
        VbRaise(VBERR_OUT_OF_STRING_SPACE);
    return b;
}

VB_STD BSTR vb_rtcGetTimeBstr(void)
{
    SYSTEMTIME st;
    WCHAR text[16];
    GetLocalTime(&st);
    wsprintfW(text, L"%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    BSTR b = SysAllocString(text);
    if (!b)
        VbRaise(VBERR_OUT_OF_STRING_SPACE);
    return b;
}

VB_STD VARIANT *vb_rtcGetDateValue(VARIANT *dest, BSTR text)
{
    DATE date;
    VbCheck(VarDateFromStr(text, user_locale(), g_calendar == 1 ? 10 : 2, &date));
    return return_date(dest, date);
}

VB_STD VARIANT *vb_rtcGetTimeValue(VARIANT *dest, BSTR text)
{
    DATE date;
    VbCheck(VarDateFromStr(text, user_locale(), g_calendar == 1 ? 9 : 1, &date));
    return return_date(dest, date);
}

/* ------------------------------------------------------------------ the parts of a date */

static VARIANT *date_part(VARIANT *dest, VARIANT *src, int part)
{
    VARIANT v;
    UDATE u;
    VbValue(&v, src);
    if (V_VT(&v) == VT_NULL)
        return return_null(dest);
    udate_of(&v, &u);
    VariantClear(&v);
    switch (part) {
    case 0: return return_short(dest, (SHORT)u.st.wYear);
    case 1: return return_short(dest, (SHORT)u.st.wMonth);
    case 2: return return_short(dest, (SHORT)u.st.wDay);
    case 3: return return_short(dest, (SHORT)u.st.wHour);
    case 4: return return_short(dest, (SHORT)u.st.wMinute);
    default: return return_short(dest, (SHORT)u.st.wSecond);
    }
}

VB_STD VARIANT *vb_rtcGetYear(VARIANT *dest, VARIANT *src) { return date_part(dest, src, 0); }
VB_STD VARIANT *vb_rtcGetMonthOfYear(VARIANT *dest, VARIANT *src) { return date_part(dest, src, 1); }
VB_STD VARIANT *vb_rtcGetDayOfMonth(VARIANT *dest, VARIANT *src) { return date_part(dest, src, 2); }
VB_STD VARIANT *vb_rtcGetHourOfDay(VARIANT *dest, VARIANT *src) { return date_part(dest, src, 3); }
VB_STD VARIANT *vb_rtcGetMinuteOfHour(VARIANT *dest, VARIANT *src) { return date_part(dest, src, 4); }
VB_STD VARIANT *vb_rtcGetSecondOfMinute(VARIANT *dest, VARIANT *src) { return date_part(dest, src, 5); }

static int first_day_default(void)
{
    DWORD first = 0;
    WCHAR text[4];
    if (GetLocaleInfoW(user_locale(), LOCALE_IFIRSTDAYOFWEEK | LOCALE_RETURN_NUMBER, text, 2))
        first = *(DWORD *)text;
    return (int)(first + 1) % 7 + 1;
}

static int first_week_default(void)
{
    DWORD first = 0;
    WCHAR text[4];
    if (GetLocaleInfoW(user_locale(), LOCALE_IFIRSTWEEKOFYEAR | LOCALE_RETURN_NUMBER, text, 2))
        first = *(DWORD *)text;
    return first == 0 ? 1 : first == 1 ? 3 : 2;
}

static int weekday_of(const UDATE *u, UINT first)
{
    if (first >= 8)
        VbRaise(VBERR_INVALID_CALL);
    if (!first)
        first = (UINT)first_day_default();
    return ((int)u->st.wDayOfWeek - (int)first + 8) % 7 + 1;
}

VB_STD VARIANT *vb_rtcGetDayOfWeek(VARIANT *dest, VARIANT *src, UINT first)
{
    VARIANT v;
    UDATE u;
    VbValue(&v, src);
    if (V_VT(&v) == VT_NULL)
        return return_null(dest);
    udate_of(&v, &u);
    VariantClear(&v);
    return return_short(dest, (SHORT)weekday_of(&u, first));
}

static int VarWeekNumber(const WCHAR *text)
{
    int n = 0;
    for (; text && *text >= L'0' && *text <= L'9'; text++)
        n = n * 10 + (*text - L'0');
    return n;
}

static int interval_code(BSTR interval)
{
    static const WCHAR *const names[] = {L"yyyy", L"y", L"m", L"d", L"h", L"n", L"s", L"ww", L"w", L"q"};
    static const int codes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    for (int i = 0; i < 10; i++)
        if (interval && CompareStringW(0x409, NORM_IGNORECASE, interval, -1, names[i], -1) == CSTR_EQUAL)
            return codes[i];
    VbRaise(VBERR_INVALID_CALL);
}

VB_STD VARIANT *vb_rtcDatePart(VARIANT *dest, BSTR interval, VARIANT *src, UINT first_day, UINT first_week)
{
    VARIANT v;
    UDATE u;
    int code = interval_code(interval);
    VbValue(&v, src);
    if (V_VT(&v) == VT_NULL)
        return return_null(dest);
    udate_of(&v, &u);
    switch (code) {
    case 1: return_short(dest, (SHORT)u.st.wYear); break;
    case 2: return_short(dest, (SHORT)u.wDayOfYear); break;
    case 3: return_short(dest, (SHORT)u.st.wMonth); break;
    case 4: return_short(dest, (SHORT)u.st.wDay); break;
    case 5: return_short(dest, (SHORT)u.st.wHour); break;
    case 6: return_short(dest, (SHORT)u.st.wMinute); break;
    case 7: return_short(dest, (SHORT)u.st.wSecond); break;
    case 8: {
        if (first_day >= 8 || first_week >= 4)
            VbRaise(VBERR_INVALID_CALL);
        if (!first_day)
            first_day = (UINT)first_day_default();
        if (!first_week)
            first_week = (UINT)first_week_default();
        BSTR text = nullptr;
        VbCheck(VarFormat(&v, L"ww", (int)first_day, (int)first_week, 0, &text));
        return_short(dest, (SHORT)VarWeekNumber(text));
        SysFreeString(text);
        break;
    }
    case 9: return_short(dest, (SHORT)weekday_of(&u, first_day)); break;
    default: return_short(dest, (SHORT)((u.st.wMonth + 2) / 3)); break;
    }
    VariantClear(&v);
    return dest;
}

/* ------------------------------------------------------------------ DateAdd and DateDiff */

static DATE date_of(const UDATE *u)
{
    DATE date;
    UDATE copy = *u;
    if (FAILED(VarDateFromUdate(&copy, calendar_flags(), &date)))
        VbRaise(VBERR_INVALID_CALL);
    return date;
}

VB_STD VARIANT *vb_rtcDateAdd(VARIANT *dest, BSTR interval, double number, VARIANT *src)
{
    VARIANT v;
    UDATE u;
    int code = interval_code(interval);
    LONG n = (LONG)round_even(number);
    VbValue(&v, src);
    if (V_VT(&v) == VT_NULL)
        return return_null(dest);
    DATE date;
    VbCheck(VariantChangeTypeEx(&v, &v, VbLocale(), 0, VT_DATE));
    date = V_DATE(&v);
    udate_of(&v, &u);
    double whole = date < 0 ? vb_ceil(date) : vb_floor(date), frac = __builtin_fabs(date - whole);
    switch (code) {
    case 10:
        n *= 3;
        [[fallthrough]];
    case 3: {
        LONG months = u.st.wMonth - 1 + n;
        LONG years = months / 12;
        LONG month = months % 12;
        if (month < 0) {
            month += 12;
            years--;
        }
        u.st.wMonth = (WORD)(month + 1);
        LONG year = u.st.wYear + years;
        if (year < 100 || year > 9999)
            VbRaise(VBERR_INVALID_CALL);
        u.st.wYear = (WORD)year;
        goto clamp;
    }
    case 1: {
        LONG year = u.st.wYear + n;
        if (year < 100 || year > 9999)
            VbRaise(VBERR_INVALID_CALL);
        u.st.wYear = (WORD)year;
    clamp:
        for (;;) {
            DATE probe;
            UDATE test = u;
            if (SUCCEEDED(VarDateFromUdate(&test, calendar_flags() | 4, &probe)))
                break;
            if (--u.st.wDay <= 28)
                VbRaise(VBERR_INVALID_CALL);
        }
        date = date_of(&u);
        break;
    }
    case 8: n *= 7; [[fallthrough]];
    case 2:
    case 4:
    case 9: {
        double target = whole + (double)n;
        date = target < 0 ? target - frac : target + frac;
        break;
    }
    default: {
        double unit = code == 5 ? 1.0 / 24 : code == 6 ? 1.0 / 1440 : 1.0 / 86400;
        date += (date < 0 ? -1 : 1) * (double)n * unit;
        break;
    }
    }
    if (date <= -657435.0 || date >= 2958466.0)
        VbRaise(VBERR_INVALID_CALL);
    VariantClear(&v);
    return return_date(dest, date);
}

static VARIANT *return_long(VARIANT *dest, LONG value)
{
    VariantInit(dest);
    V_VT(dest) = VT_I4;
    V_I4(dest) = value;
    return dest;
}

VB_STD VARIANT *vb_rtcDateDiff(VARIANT *dest, BSTR interval, VARIANT *first, VARIANT *second, UINT first_day, UINT first_week)
{
    VARIANT a, b;
    UDATE ua, ub;
    int code = interval_code(interval);
    VbValue(&a, first);
    VbValue(&b, second);
    if (V_VT(&a) == VT_NULL || V_VT(&b) == VT_NULL)
        return return_null(dest);
    VbCheck(VariantChangeTypeEx(&a, &a, VbLocale(), 0, VT_DATE));
    VbCheck(VariantChangeTypeEx(&b, &b, VbLocale(), 0, VT_DATE));
    const double half_second = 0.000005787037037037037;
    double d1 = V_DATE(&a), d2 = V_DATE(&b);
    double t1 = d1 >= 0 ? d1 + half_second : d1 - half_second;
    double t2 = d2 >= 0 ? d2 + half_second : d2 - half_second;
    udate_of(&a, &ua);
    udate_of(&b, &ub);
    LONG result;
    switch (code) {
    case 1: result = ub.st.wYear - ua.st.wYear; break;
    case 2:
    case 4: result = (LONG)vb_floor(t2) - (LONG)vb_floor(t1); break;
    case 3: result = ub.st.wMonth + 12 * (ub.st.wYear - ua.st.wYear) - ua.st.wMonth; break;
    case 5: result = (LONG)vb_floor(t2 * 24) - (LONG)vb_floor(t1 * 24); break;
    case 6:
    case 7: {
        double scale = code == 6 ? 1440.0 : 86400.0;
        double diff = vb_floor(t2 * scale) - vb_floor(t1 * scale);
        if (diff > 2147483647.0 || diff < -2147483648.0) {
            VariantInit(dest);
            V_VT(dest) = VT_R8;
            V_R8(dest) = diff;
            return dest;
        }
        result = (LONG)diff;
        break;
    }
    case 8: {
        int w1 = weekday_of(&ua, first_day), w2 = weekday_of(&ub, first_day);
        result = (w1 + (LONG)vb_floor(t2) - w2 - (LONG)vb_floor(t1)) / 7;
        break;
    }
    case 9: result = ((LONG)vb_floor(t2) - (LONG)vb_floor(t1)) / 7; break;
    default: result = (ub.st.wMonth - 1) / 3 + 4 * (ub.st.wYear - ua.st.wYear) - (ua.st.wMonth - 1) / 3; break;
    }
    (void)first_week;
    return return_long(dest, result);
}

/* ------------------------------------------------------------------ DateSerial and TimeSerial */

VB_STD VARIANT *vb_rtcPackDate(VARIANT *dest, SHORT year, SHORT month, SHORT day)
{
    LONG y = year;
    if (y >= 0 && y < 30)
        y += 2000;
    else if (y >= 30 && y < 100)
        y += 1900;
    LONG months = y * 12 + (month - 1);
    LONG yy = months >= 0 ? months / 12 : -((-months + 11) / 12);
    LONG mm = months - yy * 12;
    if (yy < 100 || yy > 9999)
        VbRaise(VBERR_INVALID_CALL);
    UDATE u = {};
    u.st.wYear = (WORD)yy;
    u.st.wMonth = (WORD)(mm + 1);
    u.st.wDay = 1;
    DATE first;
    if (FAILED(VarDateFromUdate(&u, calendar_flags(), &first)))
        VbRaise(VBERR_INVALID_CALL);
    DATE date = first + (double)(day - 1);
    if (date <= -657435.0 || date >= 2958466.0)
        VbRaise(VBERR_INVALID_CALL);
    return return_date(dest, date);
}

VB_STD VARIANT *vb_rtcPackTime(VARIANT *dest, SHORT hour, SHORT minute, SHORT second)
{
    LONG total = (LONG)hour * 3600 + (LONG)minute * 60 + second;
    LONG days = total >= 0 ? total / 86400 : -((-total + 86399) / 86400);
    LONG rest = total - days * 86400;
    double fraction = (double)rest / 86400.0;
    return return_date(dest, days < 0 ? (double)days - fraction : (double)days + fraction);
}

/* ------------------------------------------------------------------ setting the clock */

VB_STD void vb_rtcSetDateBstr(BSTR text)
{
    DATE date;
    SYSTEMTIME now, target;
    VbCheck(VarDateFromStr(text, user_locale(), 2, &date));
    GetLocalTime(&now);
    VariantTimeToSystemTime(date, &target);
    target.wHour = now.wHour;
    target.wMinute = now.wMinute;
    target.wSecond = now.wSecond;
    target.wMilliseconds = now.wMilliseconds;
    if (!SetLocalTime(&target))
        VbRaise(VBERR_INVALID_CALL);
}

VB_STD void vb_rtcSetDateVar(VARIANT *v)
{
    VARIANT converted;
    VariantInit(&converted);
    VbCheck(VariantChangeTypeEx(&converted, v, VbLocale(), 0, VT_DATE));
    SYSTEMTIME now, target;
    GetLocalTime(&now);
    VariantTimeToSystemTime(V_DATE(&converted), &target);
    target.wHour = now.wHour;
    target.wMinute = now.wMinute;
    target.wSecond = now.wSecond;
    target.wMilliseconds = now.wMilliseconds;
    if (!SetLocalTime(&target))
        VbRaise(VBERR_INVALID_CALL);
}

static void set_time(DATE date)
{
    SYSTEMTIME now, target;
    GetLocalTime(&now);
    VariantTimeToSystemTime(date, &target);
    now.wHour = target.wHour;
    now.wMinute = target.wMinute;
    now.wSecond = target.wSecond;
    now.wMilliseconds = 0;
    if (!SetLocalTime(&now))
        VbRaise(VBERR_INVALID_CALL);
}

VB_STD void vb_rtcSetTimeBstr(BSTR text)
{
    DATE date;
    VbCheck(VarDateFromStr(text, user_locale(), 1, &date));
    set_time(date);
}

VB_STD void vb_rtcSetTimeVar(VARIANT *v)
{
    VARIANT converted;
    VariantInit(&converted);
    VbCheck(VariantChangeTypeEx(&converted, v, VbLocale(), 0, VT_DATE));
    set_time(V_DATE(&converted));
}

/* ------------------------------------------------------------------ the names and formats */

static void check_format(HRESULT hr)
{
    if (hr == DISP_E_EXCEPTION)
        hr = DISP_E_TYPEMISMATCH;
    VbCheck(hr);
}

VB_STD BSTR vb_rtcFormatNumber(VARIANT *v, LONG digits, LONG leading, LONG parens, LONG group)
{
    BSTR text = nullptr;
    check_format(VarFormatNumber(v, digits, leading, parens, group, calendar_flags(), &text));
    return text;
}

VB_STD BSTR vb_rtcFormatCurrency(VARIANT *v, LONG digits, LONG leading, LONG parens, LONG group)
{
    BSTR text = nullptr;
    check_format(VarFormatCurrency(v, digits, leading, parens, group, calendar_flags(), &text));
    return text;
}

VB_STD BSTR vb_rtcFormatPercent(VARIANT *v, LONG digits, LONG leading, LONG parens, LONG group)
{
    BSTR text = nullptr;
    check_format(VarFormatPercent(v, digits, leading, parens, group, calendar_flags(), &text));
    return text;
}

VB_STD BSTR vb_rtcFormatDateTime(VARIANT *v, LONG named)
{
    BSTR text = nullptr;
    check_format(VarFormatDateTime(v, named, calendar_flags(), &text));
    return text;
}

VB_STD BSTR vb_rtcWeekdayName(LONG weekday, SHORT abbreviate, LONG first_day)
{
    BSTR text = nullptr;
    HRESULT hr = VarWeekdayName(weekday, abbreviate, first_day, calendar_flags(), &text);
    VbCheck(hr);
    return text;
}

VB_STD BSTR vb_rtcMonthName(LONG month, SHORT abbreviate)
{
    BSTR text = nullptr;
    HRESULT hr = VarMonthName(month, abbreviate, calendar_flags(), &text);
    VbCheck(hr);
    return text;
}

/* Format: the picture is a string that may be missing, and the first day and week default to the system. */
VB_STD BSTR vb_rtcBstrFromFormatVar(VARIANT *v, VARIANT *picture, LONG first_day, LONG first_week)
{
    BSTR format = nullptr;
    BOOL owned = FALSE;
    if (!VbIsMissing(picture)) {
        if ((V_VT(picture) & ~VT_BYREF) == VT_BSTR)
            format = (V_VT(picture) & VT_BYREF) ? *V_BSTRREF(picture) : V_BSTR(picture);
        else {
            VbText(picture, &format);
            owned = TRUE;
        }
    }
    if ((V_VT(v) & ~VT_BYREF) == VT_DISPATCH) {
        IDispatch *object = (V_VT(v) & VT_BYREF) ? *V_DISPATCHREF(v) : V_DISPATCH(v);
        if (!object)
            VbRaise(91);
    }
    VARIANT empty;
    VARIANT *source = v;
    if (V_VT(v) == VT_EMPTY) {
        VariantInit(&empty);
        V_VT(&empty) = VT_NULL;
        source = &empty;
        V_VT(&empty) = VT_NULL;
    }
    BSTR out = nullptr;
    HRESULT hr = VarFormat(source, format, first_day, first_week, calendar_flags(), &out);
    if (owned)
        SysFreeString(format);
    if (hr == DISP_E_EXCEPTION)
        hr = DISP_E_TYPEMISMATCH;
    VbCheck(hr);
    return out;
}

VB_STD VARIANT *vb_rtcVarFromFormatVar(VARIANT *dest, VARIANT *v, VARIANT *picture, LONG first_day, LONG first_week)
{
    BSTR text = vb_rtcBstrFromFormatVar(v, picture, first_day, first_week);
    VariantInit(dest);
    V_VT(dest) = VT_BSTR;
    V_BSTR(dest) = text;
    return dest;
}

/* CDate and the conversions to a date. */
VB_STD double vb_rtcDateFromVar(VARIANT *v) { return vb___vbaDateVar(v); }

VB_STD VARIANT *vb_rtcVarDateFromVar(VARIANT *dest, VARIANT *v)
{
    VARIANT value;
    VbValue(&value, v);
    VariantInit(dest);
    if (V_VT(&value) == VT_NULL)
        VbRaise(VBERR_INVALID_USE_OF_NULL);
    VbCheck(VariantChangeTypeEx(dest, &value, VbLocale(), 0, VT_DATE));
    VariantClear(&value);
    return dest;
}

VB_STD VARIANT *vb___vbaVarDateVar(VARIANT *dest, VARIANT *v) { return vb_rtcVarDateFromVar(dest, v); }
