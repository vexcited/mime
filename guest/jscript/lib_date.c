/*
 * Date: time values in milliseconds since 1970, the time zone of the machine, and the formats of JScript.
 */
#include "js.h"

#define ARG(i) ((i) < argc ? args[i] : UNDEF)

static const double MS_DAY = 86400000.0;

static Str *S(Engine *e, const char *z)
{
    return str_intern_ascii(e, z);
}

/* ------------------------------------------------------------------ calendar arithmetic */

static double day_of(double t)
{
    return floor(t / MS_DAY);
}

static double time_within_day(double t)
{
    double r = fmod(t, MS_DAY);
    return r < 0 ? r + MS_DAY : r;
}

static double days_from_year(double y)
{
    return 365 * (y - 1970) + floor((y - 1969) / 4) - floor((y - 1901) / 100) + floor((y - 1601) / 400);
}

static BOOL is_leap(double y)
{
    return fmod(y, 4) == 0 && (fmod(y, 100) != 0 || fmod(y, 400) == 0);
}

static double year_from_time(double t)
{
    double y = floor(t / (MS_DAY * 365.2425)) + 1970;
    while (days_from_year(y) * MS_DAY > t)
        y--;
    while (days_from_year(y + 1) * MS_DAY <= t)
        y++;
    return y;
}

static const int g_cum[2][13] = {{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365},
                                 {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366}};

static void ymd_from_time(double t, double *year, int *month, int *date)
{
    double y = year_from_time(t);
    int d = (int)(day_of(t) - days_from_year(y));
    int leap = is_leap(y);
    int m = 0;
    while (m < 11 && d >= g_cum[leap][m + 1])
        m++;
    *year = y;
    *month = m;
    *date = d - g_cum[leap][m] + 1;
}

static int weekday(double t)
{
    double r = fmod(day_of(t) + 4, 7);
    if (r < 0)
        r += 7;
    return (int)r;
}

static double make_time(double h, double m, double s, double ms)
{
    if (is_nan(h) || is_nan(m) || is_nan(s) || is_nan(ms) || is_inf(h) || is_inf(m) || is_inf(s) || is_inf(ms))
        return js_nan;
    return to_integer_d(h) * 3600000.0 + to_integer_d(m) * 60000.0 + to_integer_d(s) * 1000.0 + to_integer_d(ms);
}

static double make_day(double year, double month, double date)
{
    if (is_nan(year) || is_nan(month) || is_nan(date) || is_inf(year) || is_inf(month) || is_inf(date))
        return js_nan;
    double y = to_integer_d(year), m = to_integer_d(month), dt = to_integer_d(date);
    double ym = y + floor(m / 12);
    double mn = fmod(m, 12);
    if (mn < 0)
        mn += 12;
    if (fabs(ym) > 400000)
        return js_nan;
    double day = days_from_year(ym) + g_cum[is_leap(ym)][(int)mn];
    return day + dt - 1;
}

static double make_date(double day, double time)
{
    if (is_nan(day) || is_nan(time) || is_inf(day) || is_inf(time))
        return js_nan;
    return day * MS_DAY + time;
}

static double time_clip(double t)
{
    if (is_nan(t) || is_inf(t) || fabs(t) > 8.64e15)
        return js_nan;
    return to_integer_d(t) + 0.0;
}

/* ------------------------------------------------------------------ the time zone */

#define EPOCH_DIFF 11644473600000.0

static double tz_offset_utc(double t)
{
    /* the offset in ms to add to utc to get local time. */
    TIME_ZONE_INFORMATION tzi;
    GetTimeZoneInformation(&tzi);
    double ft_ms = t + EPOCH_DIFF;
    if (ft_ms < 0 || ft_ms > 9.2e14)
        return -(double)tzi.Bias * 60000.0;
    ULONGLONG ft = (ULONGLONG)(ft_ms * 10000.0);
    FILETIME f = {(DWORD)ft, (DWORD)(ft >> 32)};
    SYSTEMTIME utc, local;
    if (!FileTimeToSystemTime(&f, &utc) || !SystemTimeToTzSpecificLocalTime(&tzi, &utc, &local))
        return -(double)tzi.Bias * 60000.0;
    SystemTimeToFileTime(&local, &f);
    ULONGLONG lt = ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime;
    return (double)((LONGLONG)(lt - (ft - ft % 10000))) / 10000.0;
}

static double local_time(double t)
{
    if (is_nan(t))
        return t;
    return t + tz_offset_utc(t);
}

static double utc_time(double local)
{
    if (is_nan(local))
        return local;
    TIME_ZONE_INFORMATION tzi;
    GetTimeZoneInformation(&tzi);
    double guess = -(double)tzi.Bias * 60000.0;
    double off1 = tz_offset_utc(local - guess);
    double utc = local - off1;
    double off2 = tz_offset_utc(utc);
    if (off2 != off1)
        utc = local - off2;
    return utc;
}

/* ------------------------------------------------------------------ names and formats */

static const char *const g_days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const g_months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static int this_time(Engine *e, Val *self, double **slot)
{
    if (self->t != T_OBJ || self->u.o->cls != C_DATE) {
        const WCHAR *what = self->t == T_OBJ ? L"[object]" : self->t == T_NUM ? L"[number]" : self->t == T_STR ? L"[string]" :
                            self->t == T_BOOL ? L"[boolean]" : L"undefined";
        return throw_error(e, ERR_TYPE, 5006, what);
    }
    *slot = &self->u.o->u.prim.prim.u.n;
    return RC_OK;
}

static void tz_name(double t, WCHAR *out)
{
    double off = tz_offset_utc(t) / 60000.0;
    if (off == 0) {
        wcscpy(out, L"UTC");
        return;
    }
    int a = (int)(off < 0 ? -off : off);
    _snwprintf(out, 16, L"UTC%c%02d%02d", off < 0 ? '-' : '+', a / 60, a % 60);
}

static void fmt_year(double y, WCHAR *out, SIZE_T cap)
{
    if (y <= 0)
        _snwprintf(out, cap, L"%d B.C.", (int)(1 - y));
    else
        _snwprintf(out, cap, L"%d", (int)y);
}

static Str *date_string(Engine *e, double t, int kind)
{
    if (is_nan(t))
        return S(e, "NaN");
    WCHAR buf[160], tz[24], yr[24];
    if (kind == 3 || kind == 4) {
        /* utc string. */
        double y;
        int m, d;
        ymd_from_time(t, &y, &m, &d);
        fmt_year(y, yr, 24);
        double tw = time_within_day(t);
        _snwprintf(buf, 160, L"%S, %d %S %s %02d:%02d:%02d UTC", g_days[weekday(t)], d, g_months[m], yr, (int)(tw / 3600000),
                   (int)fmod(tw / 60000, 60), (int)fmod(tw / 1000, 60));
        return str_from_z(e, buf);
    }
    double lt = local_time(t);
    double y;
    int m, d;
    ymd_from_time(lt, &y, &m, &d);
    fmt_year(y, yr, 24);
    double tw = time_within_day(lt);
    tz_name(t, tz);
    int hh = (int)(tw / 3600000), mm = (int)fmod(tw / 60000, 60), ss = (int)fmod(tw / 1000, 60);
    if (kind == 0)
        _snwprintf(buf, 160, L"%S %S %d %02d:%02d:%02d %s %s", g_days[weekday(lt)], g_months[m], d, hh, mm, ss, tz, yr);
    else if (kind == 1)
        _snwprintf(buf, 160, L"%S %S %d %s", g_days[weekday(lt)], g_months[m], d, yr);
    else
        _snwprintf(buf, 160, L"%02d:%02d:%02d %s", hh, mm, ss, tz);
    return str_from_z(e, buf);
}

static Str *date_locale(Engine *e, double t, int kind)
{
    if (is_nan(t))
        return S(e, "NaN");
    double lt = local_time(t);
    double y;
    int m, d;
    ymd_from_time(lt, &y, &m, &d);
    double tw = time_within_day(lt);
    SYSTEMTIME st = {};
    st.wYear = (WORD)y;
    st.wMonth = (WORD)(m + 1);
    st.wDay = (WORD)d;
    st.wDayOfWeek = (WORD)weekday(lt);
    st.wHour = (WORD)(tw / 3600000);
    st.wMinute = (WORD)fmod(tw / 60000, 60);
    st.wSecond = (WORD)fmod(tw / 1000, 60);
    WCHAR date[100] = L"", time[100] = L"", out[220];
    if (kind != 2)
        GetDateFormatW(LOCALE_USER_DEFAULT, DATE_LONGDATE, &st, nullptr, date, 100);
    if (kind != 1)
        GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, time, 100);
    if (kind == 0)
        _snwprintf(out, 220, L"%s %s", date, time);
    else if (kind == 1)
        wcscpy(out, date);
    else
        wcscpy(out, time);
    return str_from_z(e, out);
}

/* ------------------------------------------------------------------ parsing */

static int month_of(const WCHAR *w, int len)
{
    static const char *const full[] = {"january", "february", "march", "april", "may", "june", "july", "august",
                                       "september", "october", "november", "december"};
    if (len < 3)
        return -1;
    for (int m = 0; m < 12; m++) {
        int fl = (int)strlen(full[m]);
        if (len > fl)
            continue;
        BOOL ok = TRUE;
        for (int i = 0; i < len; i++)
            if (towlower(w[i]) != (WCHAR)full[m][i]) {
                ok = FALSE;
                break;
            }
        if (ok)
            return m;
    }
    return -1;
}

static BOOL word_is(const WCHAR *w, int len, const char *s)
{
    if ((int)strlen(s) != len)
        return FALSE;
    for (int i = 0; i < len; i++)
        if (towlower(w[i]) != (WCHAR)s[i])
            return FALSE;
    return TRUE;
}

static double parse_date(Str *str)
{
    const WCHAR *p = str->s, *end = str->s + str->len;
    double year = -1, month = -1, day = -1;
    double hour = 0, minute = 0, second = 0, ms = 0;
    BOOL have_time = FALSE, have_tz = FALSE, pm = FALSE, am = FALSE;
    double tz_minutes = 0;
    int nums[3], nnum = 0;
    int sep_slash = 0;
    BOOL utc = FALSE;
    while (p < end) {
        WCHAR c = *p;
        if (c == ' ' || c == ',' || c == '\t') {
            p++;
            continue;
        }
        if (c == '(') {
            int depth = 1;
            p++;
            while (p < end && depth) {
                if (*p == '(')
                    depth++;
                else if (*p == ')')
                    depth--;
                p++;
            }
            continue;
        }
        if (iswalpha(c)) {
            const WCHAR *w = p;
            while (p < end && iswalpha(*p))
                p++;
            int len = (int)(p - w);
            int m = month_of(w, len);
            if (m >= 0 && month < 0) {
                month = m;
                if (p < end && *p == '.')
                    p++;
                continue;
            }
            if (word_is(w, len, "am") || word_is(w, len, "a")) {
                am = TRUE;
                continue;
            }
            if (word_is(w, len, "pm") || word_is(w, len, "p")) {
                pm = TRUE;
                continue;
            }
            if (word_is(w, len, "gmt") || word_is(w, len, "utc") || word_is(w, len, "ut") || word_is(w, len, "z")) {
                utc = TRUE;
                have_tz = TRUE;
                continue;
            }
            static const struct {
                const char *N;
                int Off;
            } zones[] = {{"est", -300}, {"edt", -240}, {"cst", -360}, {"cdt", -300}, {"mst", -420}, {"mdt", -360},
                         {"pst", -480}, {"pdt", -420}};
            BOOL known = FALSE;
            for (UINT z = 0; z < 8; z++)
                if (word_is(w, len, zones[z].N)) {
                    tz_minutes = zones[z].Off;
                    have_tz = TRUE;
                    known = TRUE;
                }
            if (known)
                continue;
            static const char *const dnames[] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat", "sunday", "monday",
                                                 "tuesday", "wednesday", "thursday", "friday", "saturday"};
            BOOL isday = FALSE;
            for (UINT z = 0; z < 14; z++)
                if (word_is(w, len, dnames[z]))
                    isday = TRUE;
            if (isday)
                continue;
            if (word_is(w, len, "bc")) {
                year = -1e9;
                continue;
            }
            return js_nan;
        }
        if ((c == '+' || c == '-') && (have_time || utc) && p + 1 < end && p[1] >= '0' && p[1] <= '9') {
            int sign = c == '-' ? -1 : 1;
            p++;
            int v = 0, digits = 0;
            while (p < end && *p >= '0' && *p <= '9') {
                v = v * 10 + (*p - '0');
                p++;
                digits++;
            }
            int hh, mm;
            if (p < end && *p == ':') {
                hh = v;
                p++;
                mm = 0;
                while (p < end && *p >= '0' && *p <= '9')
                    mm = mm * 10 + (*p++ - '0');
            } else if (digits <= 2) {
                hh = v;
                mm = 0;
            } else {
                hh = v / 100;
                mm = v % 100;
            }
            tz_minutes = sign * (hh * 60 + mm);
            have_tz = TRUE;
            utc = FALSE;
            continue;
        }
        if (c >= '0' && c <= '9') {
            int v = 0, digits = 0;
            const WCHAR *start = p;
            while (p < end && *p >= '0' && *p <= '9') {
                v = v * 10 + (*p - '0');
                p++;
                digits++;
                if (digits > 9)
                    return js_nan;
            }
            if (p < end && *p == ':' && !have_time) {
                /* a time. */
                hour = v;
                p++;
                int mm = 0, md = 0;
                while (p < end && *p >= '0' && *p <= '9') {
                    mm = mm * 10 + (*p++ - '0');
                    md++;
                }
                if (!md)
                    return js_nan;
                minute = mm;
                if (p < end && *p == ':') {
                    p++;
                    int ss = 0, sd = 0;
                    while (p < end && *p >= '0' && *p <= '9') {
                        ss = ss * 10 + (*p++ - '0');
                        sd++;
                    }
                    if (!sd)
                        return js_nan;
                    second = ss;
                    if (p < end && *p == '.') {
                        p++;
                        double f = 0.1;
                        double msv = 0;
                        while (p < end && *p >= '0' && *p <= '9') {
                            msv += (*p++ - '0') * f * 1000;
                            f /= 10;
                        }
                        ms = floor(msv);
                    }
                }
                have_time = TRUE;
                continue;
            }
            if (p < end && *p == '/' && nnum < 3) {
                nums[nnum++] = v;
                sep_slash = 1;
                p++;
                continue;
            }
            (void)start;
            if (nnum && sep_slash && nnum < 3) {
                nums[nnum++] = v;
                if (digits >= 3)
                    nums[nnum - 1] = v | 0x40000000;
                sep_slash = 0;
                continue;
            }
            if (digits >= 3 || v > 31) {
                if (year >= 0 && year != -1e9)
                    return js_nan;
                year = year == -1e9 ? -v : v;
                if (digits < 3 && v > 31)
                    year = v < 50 ? 2000 + v : v < 100 ? 1900 + v : v;
            } else if (day < 0 && month >= 0) {
                day = v;
            } else if (month >= 0 && year < 0) {
                year = v < 50 ? 2000 + v : v < 100 ? 1900 + v : v;
            } else if (day < 0) {
                day = v;
            } else if (year < 0)
                year = v < 50 ? 2000 + v : v < 100 ? 1900 + v : v;
            else
                return js_nan;
            continue;
        }
        if (c == '.' || c == '-' || c == '/') {
            p++;
            continue;
        }
        return js_nan;
    }
    if (nnum == 3 || (nnum == 2 && sep_slash == 0)) {
        int a = nums[0], b = nums[1] & 0x3FFFFFFF, cc = nnum == 3 ? nums[2] : -1;
        if (nnum == 3) {
            int yv = nums[2] & 0x3FFFFFFF;
            if (nums[0] > 31) {
                /* y/m/d */
                year = a;
                month = b - 1;
                day = yv;
            } else {
                month = a - 1;
                day = b;
                year = yv < 50 ? 2000 + yv : yv < 100 ? 1900 + yv : yv;
            }
        } else {
            month = a - 1;
            day = b;
            (void)cc;
        }
    } else if (nnum == 2) {
        month = nums[0] - 1;
        day = nums[1] & 0x3FFFFFFF;
    } else if (nnum == 1) {
        return js_nan;
    }
    if (year == -1e9)
        year = 1;
    if (year < 0 || month < 0 || day < 0) {
        if (year < 0 && month >= 0 && day >= 0) {
            /* a date without a year takes the current one. */
            SYSTEMTIME st;
            GetLocalTime(&st);
            year = st.wYear;
        } else
            return js_nan;
    }
    if (month > 11 || day < 1 || day > 31 || hour > 24 || minute > 59 || second > 59)
        return js_nan;
    if (pm && hour < 12)
        hour += 12;
    if (am && hour == 12)
        hour = 0;
    double dayv = make_day(year, month, day);
    double t = make_date(dayv, make_time(hour, minute, second, ms));
    if (have_tz)
        t -= tz_minutes * 60000.0;
    else
        t = utc_time(t);
    return time_clip(t);
}

/* ------------------------------------------------------------------ the object */

static Obj *date_new(Engine *e, double t)
{
    Obj *o = obj_new(e, C_DATE, e->DateProto);
    o->u.prim.prim = v_num(t);
    return o;
}

static double now_ms(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULONGLONG v = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return floor((double)v / 10000.0 - EPOCH_DIFF);
}

static int date_from_components(Engine *e, Val *args, int argc, BOOL utc, double *out)
{
    double n[7] = {js_nan, 0, 1, 0, 0, 0, 0};
    for (int i = 0; i < 7 && i < argc; i++) {
        int rc = to_number(e, &args[i], &n[i]);
        if (rc)
            return rc;
    }
    double y = n[0];
    if (!is_nan(y)) {
        double yi = to_integer_d(y);
        if (yi >= 0 && yi <= 99)
            y = 1900 + yi;
    }
    double t = make_date(make_day(y, n[1], n[2]), make_time(n[3], n[4], n[5], n[6]));
    *out = time_clip(utc ? t : utc_time(t));
    return RC_OK;
}

static int date_ctor(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    if (!construct) {
        *ret = v_str(date_string(e, now_ms(), 0));
        return RC_OK;
    }
    double t;
    int rc;
    if (argc == 0)
        t = now_ms();
    else if (argc == 1) {
        Val p;
        if ((rc = to_primitive(e, &args[0], 0, &p)))
            return rc;
        if (args[0].t == T_VAR && args[0].u.v->v.vt == VT_DATE)
            t = time_clip(utc_time(floor((args[0].u.v->v.date - 25569.0) * MS_DAY + 0.5)));
        else if (p.t == T_STR)
            t = parse_date(p.u.s);
        else {
            double d;
            if ((rc = to_number(e, &p, &d)))
                return rc;
            t = time_clip(d);
        }
    } else if ((rc = date_from_components(e, args, argc, FALSE, &t)))
        return rc;
    *ret = v_obj(date_new(e, t));
    return RC_OK;
}

static int date_parse(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    Str *s;
    Val a = ARG(0);
    int rc = to_string(e, &a, &s);
    if (rc)
        return rc;
    *ret = v_dbl(parse_date(s));
    return RC_OK;
}

static int date_utc(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    double t;
    int rc = date_from_components(e, args, argc, TRUE, &t);
    if (rc)
        return rc;
    *ret = v_dbl(t);
    return RC_OK;
}

static int date_to_string(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    double *t;
    int rc = this_time(e, self, &t);
    if (rc)
        return rc;
    int k = e->Magic;
    *ret = v_str(k >= 5 ? date_locale(e, *t, k - 5) : date_string(e, *t, k));
    return RC_OK;
}

static int date_value_of(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    double *t;
    int rc = this_time(e, self, &t);
    if (rc)
        return rc;
    *ret = v_dbl(*t);
    return RC_OK;
}

enum { F_YEAR, F_MONTH, F_DATE, F_DAY, F_HOURS, F_MINUTES, F_SECONDS, F_MS, F_TZ, F_GETYEAR };

static int date_get(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    double *tp;
    int rc = this_time(e, self, &tp);
    if (rc)
        return rc;
    double t = *tp;
    int magic = e->Magic;
    BOOL utc = magic & 0x100;
    int field = magic & 0xFF;
    if (is_nan(t)) {
        *ret = v_dbl(js_nan);
        return RC_OK;
    }
    if (field == F_TZ) {
        *ret = v_dbl((t - local_time(t)) / 60000.0);
        return RC_OK;
    }
    if (!utc)
        t = local_time(t);
    double y;
    int m, d;
    ymd_from_time(t, &y, &m, &d);
    double tw = time_within_day(t);
    double r = 0;
    switch (field) {
    case F_YEAR: r = y; break;
    case F_GETYEAR: r = (y >= 1900 && y < 2000) ? y - 1900 : y; break;
    case F_MONTH: r = m; break;
    case F_DATE: r = d; break;
    case F_DAY: r = weekday(t); break;
    case F_HOURS: r = floor(tw / 3600000); break;
    case F_MINUTES: r = fmod(floor(tw / 60000), 60); break;
    case F_SECONDS: r = fmod(floor(tw / 1000), 60); break;
    case F_MS: r = fmod(tw, 1000); break;
    }
    *ret = v_dbl(r);
    return RC_OK;
}

static int date_set(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    double *tp;
    int rc = this_time(e, self, &tp);
    if (rc)
        return rc;
    int magic = e->Magic;
    BOOL utc = magic & 0x100;
    int field = magic & 0xFF;
    double a[4];
    int n = 0;
    for (; n < 4 && n < argc; n++)
        if ((rc = to_number(e, &args[n], &a[n])))
            return rc;
    double t = *tp;
    if (field == F_GETYEAR + 1) {
        /* setTime. */
        *tp = time_clip(argc ? a[0] : js_nan);
        *ret = v_dbl(*tp);
        return RC_OK;
    }
    if (is_nan(t)) {
        if (field != F_YEAR && field != F_GETYEAR) {
            *ret = v_dbl(js_nan);
            return RC_OK;
        }
        t = 0;
    } else if (!utc)
        t = local_time(t);
    double y;
    int mo, d;
    ymd_from_time(t, &y, &mo, &d);
    double tw = time_within_day(t);
    double hh = floor(tw / 3600000), mi = fmod(floor(tw / 60000), 60), ss = fmod(floor(tw / 1000), 60), ms = fmod(tw, 1000);
    double year = y, month = mo, date = d;
    if (!n) {
        *tp = js_nan;
        *ret = v_dbl(js_nan);
        return RC_OK;
    }
    switch (field) {
    case F_YEAR:
        year = a[0];
        if (n > 1) month = a[1];
        if (n > 2) date = a[2];
        break;
    case F_GETYEAR:
        year = a[0];
        if (year >= 0 && year <= 99)
            year += 1900;
        break;
    case F_MONTH:
        month = a[0];
        if (n > 1) date = a[1];
        break;
    case F_DATE:
        date = a[0];
        break;
    case F_HOURS:
        hh = a[0];
        if (n > 1) mi = a[1];
        if (n > 2) ss = a[2];
        if (n > 3) ms = a[3];
        break;
    case F_MINUTES:
        mi = a[0];
        if (n > 1) ss = a[1];
        if (n > 2) ms = a[2];
        break;
    case F_SECONDS:
        ss = a[0];
        if (n > 1) ms = a[1];
        break;
    case F_MS:
        ms = a[0];
        break;
    }
    double nt = make_date(make_day(year, month, date), make_time(hh, mi, ss, ms));
    *tp = time_clip(utc ? nt : utc_time(nt));
    *ret = v_dbl(*tp);
    return RC_OK;
}

static int date_get_var_date(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct)
{
    double *tp;
    int rc = this_time(e, self, &tp);
    if (rc)
        return rc;
    if (is_nan(*tp))
        return throw_error(e, ERR_RANGE, 5, nullptr);
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_DATE;
    v.date = local_time(*tp) / MS_DAY + 25569.0;
    return variant_to_val(e, &v, ret);
}

void init_date(Engine *e)
{
    Obj *proto = obj_new(e, C_OBJECT, e->ObjectProto);
    e->DateProto = proto;
    Obj *ctor = obj_native(e, "Date", date_ctor, 7, 0);
    obj_define_z(e, ctor, "prototype", v_obj(proto), PF_DONTENUM | PF_DONTDELETE | PF_READONLY);
    obj_define_z(e, proto, "constructor", v_obj(ctor), PF_DONTENUM);
    obj_method(e, ctor, "parse", date_parse, 1, 0);
    obj_method(e, ctor, "UTC", date_utc, 7, 0);
    obj_method(e, proto, "toString", date_to_string, 0, 0);
    obj_method(e, proto, "toDateString", date_to_string, 0, 1);
    obj_method(e, proto, "toTimeString", date_to_string, 0, 2);
    obj_method(e, proto, "toUTCString", date_to_string, 0, 3);
    obj_method(e, proto, "toGMTString", date_to_string, 0, 4);
    obj_method(e, proto, "toLocaleString", date_to_string, 0, 5);
    obj_method(e, proto, "toLocaleDateString", date_to_string, 0, 6);
    obj_method(e, proto, "toLocaleTimeString", date_to_string, 0, 7);
    obj_method(e, proto, "valueOf", date_value_of, 0, 0);
    obj_method(e, proto, "getTime", date_value_of, 0, 0);
    static const struct {
        const char *Name;
        int Field;
        BOOL Utc;
    } getters[] = {
        {"getFullYear", F_YEAR, 0}, {"getUTCFullYear", F_YEAR, 1}, {"getMonth", F_MONTH, 0}, {"getUTCMonth", F_MONTH, 1},
        {"getDate", F_DATE, 0}, {"getUTCDate", F_DATE, 1}, {"getDay", F_DAY, 0}, {"getUTCDay", F_DAY, 1},
        {"getHours", F_HOURS, 0}, {"getUTCHours", F_HOURS, 1}, {"getMinutes", F_MINUTES, 0}, {"getUTCMinutes", F_MINUTES, 1},
        {"getSeconds", F_SECONDS, 0}, {"getUTCSeconds", F_SECONDS, 1}, {"getMilliseconds", F_MS, 0},
        {"getUTCMilliseconds", F_MS, 1}, {"getTimezoneOffset", F_TZ, 0}, {"getYear", F_GETYEAR, 0},
    };
    for (UINT i = 0; i < sizeof(getters) / sizeof(getters[0]); i++)
        obj_method(e, proto, getters[i].Name, date_get, 0, getters[i].Field | (getters[i].Utc ? 0x100 : 0));
    static const struct {
        const char *Name;
        int Field;
        BOOL Utc;
        int Args;
    } setters[] = {
        {"setFullYear", F_YEAR, 0, 3}, {"setUTCFullYear", F_YEAR, 1, 3}, {"setMonth", F_MONTH, 0, 2},
        {"setUTCMonth", F_MONTH, 1, 2}, {"setDate", F_DATE, 0, 1}, {"setUTCDate", F_DATE, 1, 1}, {"setHours", F_HOURS, 0, 4},
        {"setUTCHours", F_HOURS, 1, 4}, {"setMinutes", F_MINUTES, 0, 3}, {"setUTCMinutes", F_MINUTES, 1, 3},
        {"setSeconds", F_SECONDS, 0, 2}, {"setUTCSeconds", F_SECONDS, 1, 2}, {"setMilliseconds", F_MS, 0, 1},
        {"setUTCMilliseconds", F_MS, 1, 1}, {"setYear", F_GETYEAR, 0, 1}, {"setTime", F_GETYEAR + 1, 0, 1},
    };
    for (UINT i = 0; i < sizeof(setters) / sizeof(setters[0]); i++)
        obj_method(e, proto, setters[i].Name, date_set, setters[i].Args, setters[i].Field | (setters[i].Utc ? 0x100 : 0));
    obj_method(e, proto, "getVarDate", date_get_var_date, 0, 0);
    obj_define_z(e, e->Global, "Date", v_obj(ctor), PF_DONTENUM);
}
