/*
 * VarFormat and its relatives: the pictures of Format in Visual Basic. A picture has up to four sections split at
 * semicolons. The first character that says so gives the picture its kind: number, date or text. The value is
 * turned into digits, a date or a string and the elements of the chosen section are written one after the other.
 */
#include "oleaut32_private.h"
#include "dtoa.h"
#include <mime/win32/winnls.h>

#define OUT_MAX 1000
#define MAX_ELEMS 300
#define MAX_SECTIONS 4
#define FLAG_ERRORS 0x20

/* ------------------------------------------------------------------ the locale */

typedef struct {
    LCID Lcid;
    ULONG Override;
    WCHAR Decimal[8], Thousand[8], DateSep[8], TimeSep[8], Am[16], Pm[16], Currency[16], Positive[8], Negative[8];
    WCHAR MonDecimal[8], MonThousand[8];
    /* Sunday first. */
    WCHAR DayLong[7][48], DayShort[7][24], MonthLong[12][48], MonthShort[12][24];
    WCHAR ShortDate[80], LongDate[120], TimeFormat[80];
    int Grouping, MonGrouping, Digits, LeadingZero, NegativeNumber, CurrencyDigits, CurrencyPos, CurrencyNeg, FirstDay, FirstWeek, Hour24;
} LOCALE;

static void lstr(const LOCALE *l, LCTYPE type, WCHAR *out, int size, const WCHAR *fallback)
{
    if (!GetLocaleInfoW(l->Lcid, type | l->Override, out, size))
        lstrcpynW(out, fallback, size);
}

static int lnum(const LOCALE *l, LCTYPE type, int fallback)
{
    DWORD v;
    if (!GetLocaleInfoW(l->Lcid, type | l->Override | LOCALE_RETURN_NUMBER, (WCHAR *)&v, 2))
        return fallback;
    return (int)v;
}

static int first_group(const LOCALE *l, LCTYPE type)
{
    WCHAR grouping[24];
    int n = 0;
    lstr(l, type, grouping, 24, L"3;0");
    for (const WCHAR *c = grouping; *c >= L'0' && *c <= L'9'; c++)
        n = n * 10 + (*c - L'0');
    return n;
}

static void load_locale(LOCALE *l, LCID lcid, ULONG flags)
{
    memset(l, 0, sizeof(*l));
    l->Lcid = lcid;
    l->Override = flags & LOCALE_NOUSEROVERRIDE;
    lstr(l, LOCALE_SDECIMAL, l->Decimal, 8, L".");
    lstr(l, LOCALE_STHOUSAND, l->Thousand, 8, L",");
    lstr(l, LOCALE_SMONDECIMALSEP, l->MonDecimal, 8, L".");
    lstr(l, LOCALE_SMONTHOUSANDSEP, l->MonThousand, 8, L",");
    lstr(l, LOCALE_SDATE, l->DateSep, 8, L"/");
    lstr(l, LOCALE_STIME, l->TimeSep, 8, L":");
    lstr(l, LOCALE_S1159, l->Am, 16, L"AM");
    lstr(l, LOCALE_S2359, l->Pm, 16, L"PM");
    lstr(l, LOCALE_SCURRENCY, l->Currency, 16, L"$");
    lstr(l, LOCALE_SPOSITIVESIGN, l->Positive, 8, L"");
    lstr(l, LOCALE_SNEGATIVESIGN, l->Negative, 8, L"-");
    for (int i = 0; i < 7; i++) {
        int k = (i + 6) % 7;
        lstr(l, LOCALE_SDAYNAME1 + k, l->DayLong[i], 48, L"");
        lstr(l, LOCALE_SABBREVDAYNAME1 + k, l->DayShort[i], 24, L"");
    }
    for (int i = 0; i < 12; i++) {
        lstr(l, i < 9 ? LOCALE_SMONTHNAME1 + i : LOCALE_SMONTHNAME10 + i - 9, l->MonthLong[i], 48, L"");
        lstr(l, i < 9 ? LOCALE_SABBREVMONTHNAME1 + i : LOCALE_SABBREVMONTHNAME10 + i - 9, l->MonthShort[i], 24, L"");
        size_t len = wcslen(l->MonthShort[i]);
        if (len > 1 && l->MonthShort[i][len - 1] == L'.')
            l->MonthShort[i][len - 1] = 0;
    }
    lstr(l, LOCALE_SSHORTDATE, l->ShortDate, 80, L"M/d/yyyy");
    lstr(l, LOCALE_SLONGDATE, l->LongDate, 120, L"dddd, MMMM dd, yyyy");
    lstr(l, LOCALE_STIMEFORMAT, l->TimeFormat, 80, L"h:mm:ss tt");
    l->Grouping = first_group(l, LOCALE_SGROUPING);
    l->MonGrouping = first_group(l, LOCALE_SMONGROUPING);
    l->Digits = lnum(l, LOCALE_IDIGITS, 2);
    l->LeadingZero = lnum(l, LOCALE_ILZERO, 1);
    l->NegativeNumber = lnum(l, LOCALE_INEGNUMBER, 1);
    l->CurrencyDigits = lnum(l, LOCALE_ICURRDIGITS, 2);
    l->CurrencyPos = lnum(l, LOCALE_ICURRENCY, 0);
    l->CurrencyNeg = lnum(l, LOCALE_INEGCURR, 0);
    /* Monday is 0 in the locale, Sunday is 1 for Visual Basic. */
    l->FirstDay = (lnum(l, LOCALE_IFIRSTDAYOFWEEK, 6) + 1) % 7 + 1;
    static const int week_rule[3] = {1, 3, 2};
    int week = lnum(l, LOCALE_IFIRSTWEEKOFYEAR, 0);
    l->FirstWeek = week_rule[week >= 0 && week < 3 ? week : 0];
    l->Hour24 = lnum(l, LOCALE_ITIME, 0);
}

/* Polish and Russian name the month in the genitive when the day came first in the picture. */
static const WCHAR *const g_polish_genitive[12] = {L"stycznia", L"lutego", L"marca", L"kwietnia", L"maja", L"czerwca", L"lipca", L"sierpnia",
                                                   L"wrze\u015bnia", L"pa\u017adziernika", L"listopada", L"grudnia"};
static const WCHAR *const g_russian_genitive[12] = {
    L"\u044f\u043d\u0432\u0430\u0440\u044f", L"\u0444\u0435\u0432\u0440\u0430\u043b\u044f", L"\u043c\u0430\u0440\u0442\u0430",
    L"\u0430\u043f\u0440\u0435\u043b\u044f", L"\u043c\u0430\u044f", L"\u0438\u044e\u043d\u044f", L"\u0438\u044e\u043b\u044f",
    L"\u0430\u0432\u0433\u0443\u0441\u0442\u0430", L"\u0441\u0435\u043d\u0442\u044f\u0431\u0440\u044f",
    L"\u043e\u043a\u0442\u044f\u0431\u0440\u044f", L"\u043d\u043e\u044f\u0431\u0440\u044f", L"\u0434\u0435\u043a\u0430\u0431\u0440\u044f"};

static const WCHAR *month_long(const LOCALE *l, int month, BOOL day_before)
{
    if (day_before) {
        if ((l->Lcid & 0x3FF) == 0x15)
            return g_polish_genitive[month - 1];
        if ((l->Lcid & 0x3FF) == 0x19)
            return g_russian_genitive[month - 1];
    }
    return l->MonthLong[month - 1];
}

/* ------------------------------------------------------------------ output */

typedef struct {
    WCHAR Text[OUT_MAX + 4];
    int Length;
    BOOL Over;
} TXT;

static void put(TXT *o, WCHAR c)
{
    if (o->Length >= OUT_MAX)
        o->Over = TRUE;
    else
        o->Text[o->Length++] = c;
}

static void put_text(TXT *o, const WCHAR *s, int n)
{
    for (int i = 0; i < n; i++)
        put(o, s[i]);
}

static void put_str(TXT *o, const WCHAR *s)
{
    put_text(o, s, (int)wcslen(s));
}

static void put_number(TXT *o, int v, int width)
{
    WCHAR t[12];
    int n = 0;
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
    do
        t[n++] = (WCHAR)(L'0' + u % 10);
    while (u /= 10);
    if (v < 0)
        put(o, L'-');
    for (int i = n; i < width; i++)
        put(o, L'0');
    while (n)
        put(o, t[--n]);
}

/* ------------------------------------------------------------------ pictures */

typedef enum {
    EL_LIT,
    EL_ZERO,
    EL_HASH,
    EL_DECIMAL,
    EL_PERCENT,
    EL_EXPONENT,
    EL_DAY,
    EL_WEEKDAY,
    EL_WEEK,
    EL_MONTH,
    EL_MINUTE,
    EL_QUARTER,
    EL_DOY,
    EL_YEAR,
    EL_HOUR,
    EL_SECOND,
    EL_LONGTIME,
    EL_AMPM,
    EL_DATESEP,
    EL_TIMESEP,
    EL_GENERAL,
    EL_AT,
    EL_AMP
} ELKIND;

typedef struct {
    BYTE Kind;
    BYTE Count;
    BYTE Flag;
    /* the start of a literal in the pool. */
    USHORT At;
} ELEM;

typedef enum { SK_NONE, SK_NUMBER, SK_DATE, SK_TEXT } SKIND;

typedef struct {
    int Count;
    ELEM El[MAX_ELEMS];
    int IntPlaces, FracPlaces, Percents, Scale, ExpMin;
    BOOL Thousands, HasDecimal, HasExponent, Twelve, Lower, Upper, Bang;
} SECTION;

typedef struct {
    SKIND Kind;
    SECTION Sec[MAX_SECTIONS];
    int Count;
    WCHAR Pool[800];
    int PoolLength;
} PICTURE;

static WCHAR up(WCHAR c)
{
    return c >= L'a' && c <= L'z' ? (WCHAR)(c - 32) : c;
}

static void add(SECTION *s, ELKIND kind, int count, int flag, int at)
{
    if (s->Count >= MAX_ELEMS)
        return;
    ELEM *e = &s->El[s->Count++];
    e->Kind = (BYTE)kind;
    e->Count = (BYTE)count;
    e->Flag = (BYTE)flag;
    e->At = (USHORT)at;
}

static void add_literal(PICTURE *p, SECTION *s, const WCHAR *text, int n)
{
    if (n <= 0 || p->PoolLength + n > (int)RTL_NUMBER_OF(p->Pool))
        return;
    ELEM *last = s->Count ? &s->El[s->Count - 1] : nullptr;
    if (last && last->Kind == EL_LIT && last->At + last->Count == p->PoolLength && last->Count + n < 250) {
        memcpy(p->Pool + p->PoolLength, text, n * sizeof(WCHAR));
        p->PoolLength += n;
        last->Count = (BYTE)(last->Count + n);
        return;
    }
    memcpy(p->Pool + p->PoolLength, text, n * sizeof(WCHAR));
    add(s, EL_LIT, n, 0, p->PoolLength);
    p->PoolLength += n;
}

/* the kind a character gives the picture, outside the ones that say nothing. */
static SKIND char_kind(const WCHAR *c, const WCHAR *end)
{
    switch (up(*c)) {
    case L'0':
    case L'#':
    case L'.':
        return SK_NUMBER;
    case L'D':
    case L'M':
    case L'Y':
    case L'H':
    case L'N':
    case L'S':
    case L'W':
    case L'Q':
    case L'C':
    case L'/':
    case L':':
        return SK_DATE;
    case L'T':
        return end - c >= 5 && up(c[1]) == L'T' && up(c[2]) == L'T' && up(c[3]) == L'T' && up(c[4]) == L'T' ? SK_DATE : SK_NONE;
    case L'A':
        if (end - c >= 5 && up(c[1]) == L'M' && c[2] == L'/' && up(c[3]) == L'P' && up(c[4]) == L'M')
            return SK_DATE;
        if (end - c >= 3 && c[1] == L'/' && up(c[2]) == L'P')
            return SK_DATE;
        if (end - c >= 4 && up(c[1]) == L'M' && up(c[2]) == L'P' && up(c[3]) == L'M')
            return SK_DATE;
        return SK_NONE;
    case L'@':
    case L'&':
    case L'<':
    case L'>':
    case L'!':
        return SK_TEXT;
    default:
        return SK_NONE;
    }
}

/* how long a character stays together with the ones after it in a date picture. */
static int run_of(const WCHAR *c, const WCHAR *end)
{
    int n = 1;
    while (c + n < end && up(c[n]) == up(*c))
        n++;
    return n;
}

static void parse_date_char(PICTURE *p, SECTION *s, const WCHAR **cp, const WCHAR *end)
{
    const WCHAR *c = *cp;
    WCHAR u = up(*c);
    int run = run_of(c, end), left;
    switch (u) {
    case L'D':
        for (left = run; left > 0; left -= 6)
            add(s, EL_DAY, left > 6 ? 6 : left, 0, 0);
        break;
    case L'W':
        for (left = run; left > 0; left -= 2)
            add(s, left >= 2 ? EL_WEEK : EL_WEEKDAY, 1, 0, 0);
        break;
    case L'M':
        for (left = run; left > 0; left -= 4)
            add(s, EL_MONTH, left > 4 ? 4 : left, 0, 0);
        break;
    case L'N':
        for (left = run; left > 0; left -= 2)
            add(s, EL_MINUTE, left > 2 ? 2 : left, 0, 0);
        break;
    case L'Q':
        for (left = run; left > 0; left--)
            add(s, EL_QUARTER, 1, 0, 0);
        break;
    case L'Y':
        for (left = run; left > 0;) {
            int n = left >= 4 ? 4 : left >= 2 ? 2 : 1;
            add(s, n == 1 ? EL_DOY : EL_YEAR, n, 0, 0);
            left -= n;
        }
        break;
    case L'H':
        for (left = run; left > 0; left -= 2)
            add(s, EL_HOUR, left > 2 ? 2 : left, 0, 0);
        break;
    case L'S':
        for (left = run; left > 0; left -= 2)
            add(s, EL_SECOND, left > 2 ? 2 : left, 0, 0);
        break;
    case L'C':
        for (left = run; left > 0; left--)
            add(s, EL_GENERAL, 1, 0, 0);
        break;
    case L'/':
        add(s, EL_DATESEP, 1, 0, 0);
        run = 1;
        break;
    case L':':
        add(s, EL_TIMESEP, 1, 0, 0);
        run = 1;
        break;
    case L'T':
        if (char_kind(c, end) == SK_DATE) {
            add(s, EL_LONGTIME, 1, 0, 0);
            run = 5;
            break;
        }
        add_literal(p, s, c, 1);
        run = 1;
        break;
    case L'A':
        if (char_kind(c, end) == SK_DATE) {
            int style = end - c >= 5 && c[2] == L'/' ? 0 : c[1] == L'/' ? 1 : 2;
            run = style == 0 ? 5 : style == 1 ? 3 : 4;
            s->Twelve = TRUE;
            add(s, EL_AMPM, style, *c >= L'a' && *c <= L'z', 0);
            break;
        }
        add_literal(p, s, c, 1);
        run = 1;
        break;
    default:
        add_literal(p, s, c, 1);
        run = 1;
        break;
    }
    *cp = c + run;
}

static void parse_number_char(PICTURE *p, SECTION *s, const WCHAR **cp, const WCHAR *end)
{
    const WCHAR *c = *cp;
    WCHAR ch = *c;
    if (ch == L'0' || ch == L'#') {
        add(s, ch == L'0' ? EL_ZERO : EL_HASH, 1, 0, 0);
        if (s->HasDecimal)
            s->FracPlaces++;
        else
            s->IntPlaces++;
        *cp = c + 1;
        return;
    }
    if (ch == L'.') {
        if (!s->HasDecimal) {
            s->HasDecimal = TRUE;
            add(s, EL_DECIMAL, 1, 0, 0);
        } else {
            add_literal(p, s, c, 1);
        }
        *cp = c + 1;
        return;
    }
    if (ch == L',') {
        int digits_before = s->IntPlaces;
        const WCHAR *q = c;
        while (q < end && *q == L',')
            q++;
        if (s->HasDecimal) {
            /* after the point it means nothing. */
        } else if (digits_before && q < end && (*q == L'0' || *q == L'#')) {
            s->Thousands = TRUE;
        } else if (digits_before) {
            s->Scale += (int)(q - c);
        } else {
            add_literal(p, s, c, (int)(q - c));
        }
        *cp = q;
        return;
    }
    if (ch == L'%') {
        if (s->IntPlaces + s->FracPlaces > 0) {
            s->Percents++;
            add(s, EL_PERCENT, 1, 0, 0);
        } else {
            add_literal(p, s, c, 1);
        }
        *cp = c + 1;
        return;
    }
    if ((ch == L'E' || ch == L'e') && s->IntPlaces + s->FracPlaces > 0 && !s->HasExponent) {
        if (c + 1 < end && (c[1] == L'+' || c[1] == L'-')) {
            const WCHAR *q = c + 2;
            int zeros = 0;
            while (q < end && (*q == L'0' || *q == L'#')) {
                if (*q == L'0')
                    zeros++;
                q++;
            }
            s->HasExponent = TRUE;
            s->ExpMin = zeros > 0 ? zeros : 1;
            add(s, EL_EXPONENT, ch == L'e', c[1] == L'+', 0);
            *cp = q;
            return;
        }
        if (c + 1 >= end) {
            *cp = c + 1;
            return;
        }
    }
    add_literal(p, s, c, 1);
    *cp = c + 1;
}

static void parse_text_char(PICTURE *p, SECTION *s, const WCHAR **cp)
{
    const WCHAR *c = *cp;
    switch (*c) {
    case L'@': add(s, EL_AT, 1, 0, 0); break;
    case L'&': add(s, EL_AMP, 1, 0, 0); break;
    case L'<': s->Lower = TRUE; break;
    case L'>': s->Upper = TRUE; break;
    case L'!': s->Bang = TRUE; break;
    default: add_literal(p, s, c, 1); break;
    }
    *cp = c + 1;
}

/* one section, the kind being the one of the picture so far. */
static void parse_section(PICTURE *p, SECTION *s, const WCHAR *start, const WCHAR *end)
{
    if (p->Kind == SK_NONE) {
        for (const WCHAR *c = start; c < end; c++) {
            if (*c == L'"') {
                for (c++; c < end && *c != L'"'; c++)
                    ;
                continue;
            }
            if (*c == L'\\' || *c == L'*') {
                c++;
                continue;
            }
            if (*c == L'[') {
                while (c < end && *c != L']')
                    c++;
                continue;
            }
            SKIND k = char_kind(c, end);
            if (k != SK_NONE) {
                p->Kind = k;
                break;
            }
        }
    }
    for (const WCHAR *c = start; c < end;) {
        WCHAR ch = *c;
        if (ch == L'"') {
            const WCHAR *q = c + 1;
            while (q < end && *q != L'"')
                q++;
            if (q == c + 1)
                add(s, EL_LIT, 0, 0, p->PoolLength);
            else
                add_literal(p, s, c + 1, (int)(q - c - 1));
            c = q < end ? q + 1 : q;
        } else if (ch == L'\\') {
            if (c + 1 < end)
                add_literal(p, s, c + 1, 1);
            c += 2;
        } else if (ch == L'*') {
            c += 2;
        } else if (ch == L'[') {
            while (c < end && *c != L']')
                c++;
            if (c < end)
                c++;
        } else if (p->Kind == SK_NUMBER) {
            parse_number_char(p, s, &c, end);
        } else if (p->Kind == SK_DATE) {
            parse_date_char(p, s, &c, end);
        } else if (p->Kind == SK_TEXT) {
            parse_text_char(p, s, &c);
        } else {
            add_literal(p, s, c, 1);
            c++;
        }
    }
}

static void parse_picture(PICTURE *p, const WCHAR *pic)
{
    memset(p, 0, sizeof(*p));
    const WCHAR *start = pic;
    while (TRUE) {
        const WCHAR *end = start;
        while (*end && *end != L';') {
            if (*end == L'"') {
                const WCHAR *q = end + 1;
                while (*q && *q != L'"')
                    q++;
                if (!*q) {
                    end = q;
                    break;
                }
                end = q;
            } else if ((*end == L'\\' || *end == L'*') && end[1]) {
                end++;
            }
            end++;
        }
        if (p->Count < MAX_SECTIONS)
            parse_section(p, &p->Sec[p->Count++], start, end);
        if (!*end)
            break;
        start = end + 1;
    }
}

/* ------------------------------------------------------------------ digits */

typedef struct {
    BOOL Neg;
    /* the value is 0.d1d2...dn times ten to the power Exp. */
    char Dig[64];
    int N;
    int Exp;
    /* the text of an infinity, which the 32 bit library writes as it is. */
    WCHAR Infinity[12];
} DECNUM;

static void decnum_set(DECNUM *d, const char *digits, int n, int exp, BOOL neg)
{
    int lead = 0;
    while (lead < n && digits[lead] == '0')
        lead++;
    n -= lead;
    digits += lead;
    exp -= lead;
    if (n > 60)
        n = 60;
    memcpy(d->Dig, digits, n);
    d->N = n;
    d->Exp = n ? exp : 0;
    d->Neg = neg && n;
}

static void decnum_ascii(DECNUM *d, const char *text)
{
    char a[64];
    int n = 0, point = -1;
    BOOL neg = FALSE;
    const char *c = text;
    if (*c == '-') {
        neg = TRUE;
        c++;
    }
    for (; *c && *c != 'E' && *c != 'e' && n < 62; c++) {
        if (*c == '.')
            point = n;
        else
            a[n++] = *c;
    }
    int exp = 0;
    if (*c == 'E' || *c == 'e') {
        c++;
        BOOL en = *c == '-';
        if (*c == '-' || *c == '+')
            c++;
        for (; *c >= '0' && *c <= '9'; c++)
            exp = exp * 10 + (*c - '0');
        if (en)
            exp = -exp;
    }
    decnum_set(d, a, n, (point < 0 ? n : point) + exp, neg);
}

/* ------------------------------------------------------------------ values */

typedef enum { V_NUMBER, V_STRING } VKIND;

typedef struct {
    VARIANT Var;
    VARTYPE Vt;
    BOOL Null;
    BOOL Empty;
} VALUE;

/* a string that is a number, a date or a boolean, as a variant of that type. */
static HRESULT string_value(BSTR text, LCID lcid, VARIANT *out)
{
    NUMPARSE np;
    BYTE digits[40];
    np.cDig = 30;
    np.dwInFlags = 0xFFF;
    VariantInit(out);
    if (SUCCEEDED(VarParseNumFromStr(text, lcid, 0, &np, digits))) {
        const WCHAR *rest = text + np.cchUsed;
        BOOL ok = !*rest;
        if (!ok && np.nBaseShift == 0 && *rest == L'%') {
            const WCHAR *r = rest + 1;
            while (*r && (*r == L' ' || (*r >= 9 && *r <= 13)))
                r++;
            if (!*r) {
                np.nPwr10 -= 2;
                ok = TRUE;
            }
        }
        if (ok && SUCCEEDED(VarNumFromParseNum(&np, digits, 0x4020, out)))
            return S_OK;
    }
    VariantClear(out);
    DATE date;
    if (SUCCEEDED(VarDateFromStr(text, lcid, 0, &date))) {
        out->vt = VT_DATE;
        out->date = date;
        return S_OK;
    }
    VARIANT_BOOL b;
    HRESULT hr = VarBoolFromStr(text, lcid, 0, &b);
    if (SUCCEEDED(hr)) {
        out->vt = VT_BOOL;
        out->boolVal = b;
        return S_OK;
    }
    return hr;
}

/* the digits of a numeric variant, written as the library converts it to text for the English locale. */
static HRESULT number_digits(const VARIANT *v, DECNUM *d)
{
    VARIANT text;
    VariantInit(&text);
    HRESULT hr;
    if (v->vt == VT_DATE) {
        VARIANT r8;
        r8.vt = VT_R8;
        r8.dblVal = v->date;
        hr = VariantChangeTypeEx(&text, &r8, 0x409, 4, VT_BSTR);
    } else {
        hr = VariantChangeTypeEx(&text, (VARIANT *)v, 0x409, 4, VT_BSTR);
    }
    if (FAILED(hr))
        return hr;
    char a[80];
    int n = 0;
    for (UINT i = 0; i < SysStringLen(text.bstrVal) && n < 78; i++)
        a[n++] = (char)text.bstrVal[i];
    a[n] = 0;
    d->Infinity[0] = 0;
#ifndef _WIN64
    if ((n == 6 && a[0] == '1' && a[2] == '#') || (n == 7 && a[0] == '-' && a[1] == '1' && a[3] == '#'))
        for (int i = 0; i <= n; i++)
            d->Infinity[i] = (WCHAR)a[i];
#endif
    VariantClear(&text);
    decnum_ascii(d, a);
    if (d->Infinity[0])
        d->Neg = a[0] == '-';
    return S_OK;
}

/* rounds to a count of places after the point, ties away from zero. */
static void round_places(DECNUM *d, int places)
{
    int keep = d->Exp + places;
    if (keep >= d->N)
        return;
    if (keep < 0) {
        d->N = 0;
        d->Exp = 0;
        d->Neg = FALSE;
        return;
    }
    BOOL carry = d->Dig[keep] >= '5';
    d->N = keep;
    if (carry) {
        int i = keep - 1;
        while (i >= 0 && d->Dig[i] == '9')
            d->Dig[i--] = '0';
        if (i >= 0) {
            d->Dig[i]++;
        } else {
            memmove(d->Dig + 1, d->Dig, d->N);
            d->Dig[0] = '1';
            d->N++;
            d->Exp++;
        }
    }
    while (d->N > 0 && d->Dig[d->N - 1] == '0')
        d->N--;
    if (!d->N) {
        d->Exp = 0;
        d->Neg = FALSE;
    }
}

static char dig_at(const DECNUM *d, int i)
{
    return i >= 0 && i < d->N ? d->Dig[i] : '0';
}

/* ------------------------------------------------------------------ rendering numbers */

typedef struct {
    const PICTURE *P;
    const SECTION *S;
    const LOCALE *L;
    TXT *O;
    DECNUM D;
    int IntDigits, Exponent;
    BOOL Negative;
} NUMCTX;

static void number_prepare(NUMCTX *x)
{
    const SECTION *s = x->S;
    DECNUM *d = &x->D;
    if (d->N)
        d->Exp += 2 * s->Percents - 3 * s->Scale;
    x->Exponent = 0;
    if (s->HasExponent) {
        int lead = s->IntPlaces ? s->IntPlaces : 1;
        if (d->N) {
            x->Exponent = d->Exp - lead;
            d->Exp = lead;
            round_places(d, s->FracPlaces);
            if (d->N && d->Exp > lead) {
                x->Exponent += d->Exp - lead;
                d->Exp = lead;
            }
            if (!d->N)
                x->Exponent = 0;
        }
    } else {
        round_places(d, s->FracPlaces);
    }
    x->IntDigits = d->Exp > 0 ? d->Exp : 0;
    if (s->HasExponent && !d->N)
        x->IntDigits = s->IntPlaces ? s->IntPlaces : 1;
}

static void emit_int_digit(NUMCTX *x, int remaining)
{
    /* remaining counts this digit, the one before the separator rule. */
    TXT *o = x->O;
    put(o, remaining <= x->IntDigits ? dig_at(&x->D, x->IntDigits - remaining) : L'0');
    int group = x->L->Grouping;
    if (x->S->Thousands && remaining > 1 && group > 0 && (remaining % group == 1 || group == 1))
        put_str(o, x->L->Thousand);
}

static void render_number(NUMCTX *x)
{
    const SECTION *s = x->S;
    TXT *o = x->O;
    int ip = s->IntPlaces, ni = x->IntDigits;
    int total = ip > ni ? ip : ni;
    int remaining = total, frac = 0, pos = 0;
    BOOL first_int = TRUE, started = FALSE;
    int last_needed = x->D.N - x->D.Exp - 1;
    {
        int idx = 0;
        BOOL after = FALSE;
        for (int i = 0; i < s->Count; i++) {
            const ELEM *e = &s->El[i];
            if (e->Kind == EL_DECIMAL)
                after = TRUE;
            else if (after && e->Kind == EL_ZERO && idx > last_needed)
                last_needed = idx;
            if (after && (e->Kind == EL_ZERO || e->Kind == EL_HASH))
                idx++;
        }
    }
    for (int i = 0; i < s->Count; i++) {
        const ELEM *e = &s->El[i];
        switch (e->Kind) {
        case EL_LIT: put_text(o, x->P->Pool + e->At, e->Count); break;
        case EL_PERCENT: put(o, L'%'); break;
        case EL_ZERO:
        case EL_HASH:
            if (!frac) {
                if (first_int) {
                    first_int = FALSE;
                    /* the digits that find no place go in front of the first one. */
                    for (; remaining > ip; remaining--) {
                        started = TRUE;
                        emit_int_digit(x, remaining);
                    }
                }
                if (remaining > 0) {
                    BOOL has_digit = remaining <= ni;
                    if (has_digit || e->Kind == EL_ZERO || started) {
                        started = TRUE;
                        emit_int_digit(x, remaining);
                    }
                    remaining--;
                }
            } else {
                int j = pos++;
                if (e->Kind == EL_ZERO || j <= last_needed)
                    put(o, dig_at(&x->D, x->D.Exp + j));
            }
            break;
        case EL_DECIMAL:
            /* digits with no place in the integer part come out before the point. */
            if (first_int) {
                first_int = FALSE;
                for (; remaining > 0; remaining--)
                    emit_int_digit(x, remaining);
            }
            put_str(o, x->L->Decimal);
            frac = 1;
            break;
        case EL_EXPONENT: {
            put(o, e->Count ? L'e' : L'E');
            if (x->Exponent < 0)
                put(o, L'-');
            else if (e->Flag)
                put(o, L'+');
            put_number(o, x->Exponent < 0 ? -x->Exponent : x->Exponent, s->ExpMin);
            break;
        }
        default: break;
        }
    }
}

/* ------------------------------------------------------------------ rendering dates */

typedef struct {
    UDATE U;
    DATE Date;
    const LOCALE *L;
    int FirstDay, FirstWeek;
} DATECTX;

/* every fourth year is a leap year, the centuries too. */
static int days_in_year(int y)
{
    return y % 4 ? 365 : 366;
}

/* days from the first of January to the first day of the first week, negative when it starts in the year before. */
static int first_week_start(DATECTX *x, int year, int rule)
{
    SYSTEMTIME st;
    memset(&st, 0, sizeof(st));
    st.wYear = (WORD)year;
    st.wMonth = 1;
    st.wDay = 1;
    DATE date;
    UDATE u;
    UDATE in;
    memset(&in, 0, sizeof(in));
    in.st = st;
    VarDateFromUdateEx(&in, 0x409, 4, &date);
    VarUdateFromDate(date, 0, &u);
    int into = (u.st.wDayOfWeek - x->FirstDay + 8) % 7;
    int next = (7 - into) % 7;
    if (rule != 3 && into != 0) {
        if (rule != 2)
            return next - 7;
        if (into <= 3)
            return next - 7;
    }
    return next;
}

static int week_of_year(DATECTX *x)
{
    int year = x->U.st.wYear, doy = x->U.wDayOfYear;
    int start = first_week_start(x, year, x->FirstWeek);
    if (doy - start <= 0) {
        DATECTX prev = *x;
        prev.U.st.wYear = (WORD)(year - 1);
        prev.U.wDayOfYear = (USHORT)(days_in_year(year - 1) + doy);
        return week_of_year(&prev);
    }
    int week = (doy - start + 6) / 7;
    if (x->FirstWeek == 2 && week > 52) {
        int next = first_week_start(x, year + 1, 2);
        if (next < 0 && doy > days_in_year(year + 1) + next)
            return 1;
    }
    return week;
}

static void put_hour(TXT *o, const DATECTX *x, int count, BOOL twelve)
{
    int h = x->U.st.wHour;
    if (twelve) {
        h %= 12;
        if (!h)
            h = 12;
    }
    put_number(o, h, count == 2 ? 2 : 1);
}

static void render_nls_pattern(TXT *o, const WCHAR *pat, DATECTX *x);

static void render_short_date(TXT *o, DATECTX *x)
{
    render_nls_pattern(o, x->L->ShortDate, x);
}

static void render_long_date(TXT *o, DATECTX *x)
{
    render_nls_pattern(o, x->L->LongDate, x);
}

static void render_long_time(TXT *o, DATECTX *x)
{
    render_nls_pattern(o, x->L->TimeFormat, x);
}

static void render_year(TXT *o, int year, int count)
{
    if (count == 2)
        put_number(o, year % 100, 2);
    else
        put_number(o, year, 4);
}

/* a pattern of the locale: the letters are those of GetDateFormat and GetTimeFormat. */
static void render_nls_pattern(TXT *o, const WCHAR *pat, DATECTX *x)
{
    const LOCALE *l = x->L;
    const UDATE *u = &x->U;
    for (const WCHAR *c = pat; *c;) {
        int run = 1;
        while (c[run] == *c)
            run++;
        switch (*c) {
        case L'\'': {
            c++;
            while (*c) {
                if (*c == L'\'') {
                    if (c[1] == L'\'') {
                        put(o, L'\'');
                        c += 2;
                        continue;
                    }
                    c++;
                    break;
                }
                put(o, *c++);
            }
            continue;
        }
        case L'd':
            if (run == 1)
                put_number(o, u->st.wDay, 1);
            else if (run == 2)
                put_number(o, u->st.wDay, 2);
            else
                put_str(o, run == 3 ? l->DayShort[u->st.wDayOfWeek] : l->DayLong[u->st.wDayOfWeek]);
            break;
        case L'M':
            if (run <= 2)
                put_number(o, u->st.wMonth, run);
            else
                put_str(o, run == 3 ? l->MonthShort[u->st.wMonth - 1] : l->MonthLong[u->st.wMonth - 1]);
            break;
        case L'y':
            if (run <= 2)
                put_number(o, u->st.wYear % 100, run);
            else
                put_number(o, u->st.wYear, run == 3 ? 3 : 4);
            break;
        case L'h': put_number(o, u->st.wHour % 12 ? u->st.wHour % 12 : 12, run > 1 ? 2 : 1); break;
        case L'H': put_number(o, u->st.wHour, run > 1 ? 2 : 1); break;
        case L'm': put_number(o, u->st.wMinute, run > 1 ? 2 : 1); break;
        case L's': put_number(o, u->st.wSecond, run > 1 ? 2 : 1); break;
        case L't': {
            const WCHAR *mark = u->st.wHour < 12 ? l->Am : l->Pm;
            if (run == 1)
                put(o, mark[0]);
            else
                put_str(o, mark);
            break;
        }
        case L'g': break;
        default:
            for (int i = 0; i < run; i++)
                put(o, c[i]);
            break;
        }
        c += run;
    }
}

static BOOL is_epoch_day(const DATECTX *x)
{
    return x->U.st.wYear == 1899 && x->U.st.wMonth == 12 && x->U.st.wDay == 30;
}

static void render_general(TXT *o, DATECTX *x)
{
    BOOL has_time = x->U.st.wHour || x->U.st.wMinute || x->U.st.wSecond;
    BOOL epoch = is_epoch_day(x);
    if (!epoch) {
        render_short_date(o, x);
        if (has_time)
            put(o, L' ');
    }
    if (has_time || epoch)
        render_long_time(o, x);
}

static void render_date(const PICTURE *p, const SECTION *s, DATECTX *x, TXT *o)
{
    const LOCALE *l = x->L;
    const UDATE *u = &x->U;
    BOOL after_hour = FALSE, day_before = FALSE;
    for (int i = 0; i < s->Count; i++) {
        const ELEM *e = &s->El[i];
        BOOL keep_hour = FALSE;
        switch (e->Kind) {
        case EL_LIT:
            put_text(o, p->Pool + e->At, e->Count);
            keep_hour = TRUE;
            break;
        case EL_DATESEP:
            put_str(o, l->DateSep);
            keep_hour = TRUE;
            break;
        case EL_TIMESEP:
            put_str(o, l->TimeSep);
            keep_hour = TRUE;
            break;
        case EL_DAY:
            switch (e->Count) {
            case 1: put_number(o, u->st.wDay, 1); day_before = TRUE; break;
            case 2: put_number(o, u->st.wDay, 2); day_before = TRUE; break;
            case 3: put_str(o, l->DayShort[u->st.wDayOfWeek]); break;
            case 4: put_str(o, l->DayLong[u->st.wDayOfWeek]); break;
            case 5: render_short_date(o, x); break;
            default: render_long_date(o, x); break;
            }
            break;
        case EL_WEEKDAY: put_number(o, (u->st.wDayOfWeek - x->FirstDay + 8) % 7 + 1, 1); break;
        case EL_WEEK: put_number(o, week_of_year(x), 1); break;
        case EL_MONTH:
            if (e->Count <= 2 && after_hour) {
                put_number(o, u->st.wMinute, e->Count);
            } else if (e->Count <= 2) {
                put_number(o, u->st.wMonth, e->Count);
            } else {
                put_str(o, e->Count == 3 ? l->MonthShort[u->st.wMonth - 1] : month_long(l, u->st.wMonth, day_before));
            }
            break;
        case EL_MINUTE: put_number(o, u->st.wMinute, e->Count); break;
        case EL_QUARTER: put_number(o, (u->st.wMonth - 1) / 3 + 1, 1); break;
        case EL_DOY: put_number(o, u->wDayOfYear, 1); break;
        case EL_YEAR: render_year(o, u->st.wYear, e->Count); break;
        case EL_HOUR:
            put_hour(o, x, e->Count, s->Twelve);
            after_hour = TRUE;
            keep_hour = TRUE;
            break;
        case EL_SECOND: put_number(o, u->st.wSecond, e->Count); break;
        case EL_LONGTIME: render_long_time(o, x); break;
        case EL_AMPM: {
            const WCHAR *mark = u->st.wHour < 12 ? l->Am : l->Pm;
            if (e->Count == 0 || e->Count == 1) {
                WCHAR t[24];
                lstrcpynW(t, u->st.wHour < 12 ? L"AM" : L"PM", 24);
                if (e->Count == 1)
                    t[1] = 0;
                for (WCHAR *c = t; *c; c++)
                    *c = e->Flag ? (WCHAR)((*c >= L'A' && *c <= L'Z') ? *c + 32 : *c) : up(*c);
                put_str(o, t);
            } else {
                put_str(o, mark);
            }
            break;
        }
        case EL_GENERAL: render_general(o, x); break;
        default: break;
        }
        if (!keep_hour)
            after_hour = FALSE;
    }
}

/* ------------------------------------------------------------------ rendering text */

static void render_text(const PICTURE *p, const SECTION *s, const WCHAR *str, int len, TXT *o, LCID lcid)
{
    WCHAR text[260];
    if (len > 255)
        len = 255;
    memcpy(text, str, len * sizeof(WCHAR));
    if (s->Lower != s->Upper && len) {
        WCHAR mapped[260];
        if (LCMapStringW(lcid, s->Lower ? LCMAP_LOWERCASE : LCMAP_UPPERCASE, text, len, mapped, 260) == len)
            memcpy(text, mapped, len * sizeof(WCHAR));
    }
    int places = 0;
    for (int i = 0; i < s->Count; i++)
        if (s->El[i].Kind == EL_AT || s->El[i].Kind == EL_AMP)
            places++;
    int at = 0, pad = 0;
    if (s->Bang) {
        if (len > places) {
            at = len - places;
            len = places;
        }
    } else if (len < places) {
        pad = places - len;
    }
    int used = 0;
    for (int i = 0; i < s->Count; i++) {
        const ELEM *e = &s->El[i];
        if (e->Kind == EL_LIT) {
            put_text(o, p->Pool + e->At, e->Count);
        } else if (e->Kind == EL_AT || e->Kind == EL_AMP) {
            if (pad > 0) {
                pad--;
                if (e->Kind == EL_AT)
                    put(o, L' ');
            } else if (used < len) {
                put(o, text[at + used++]);
            } else if (e->Kind == EL_AT) {
                put(o, L' ');
            }
        }
    }
    for (; used < len; used++)
        put(o, text[at + used]);
}

/* ------------------------------------------------------------------ the picture of a value */

typedef struct {
    LCID Lcid;
    ULONG Flags;
    int FirstDay, FirstWeek;
    LOCALE L;
} FMT;

static HRESULT out_bstr(TXT *o, BSTR *result)
{
    if (o->Over)
        return E_INVALIDARG;
    *result = SysAllocStringLen(o->Text, o->Length);
    return *result ? S_OK : E_OUTOFMEMORY;
}

static HRESULT date_from_variant(const VARIANT *v, FMT *f, DATECTX *x)
{
    VARIANT dest;
    VariantInit(&dest);
    HRESULT hr = VariantChangeTypeEx(&dest, (VARIANT *)v, f->Lcid, (USHORT)f->Flags, VT_DATE);
    if (FAILED(hr))
        return hr;
    x->Date = dest.date;
    if (dest.date > 2958465.99999 || dest.date < -657434.99999)
        return DISP_E_OVERFLOW;
    hr = VarUdateFromDate(dest.date, f->Flags, &x->U);
    if (FAILED(hr))
        return DISP_E_OVERFLOW;
    x->L = &f->L;
    x->FirstDay = f->FirstDay ? f->FirstDay : f->L.FirstDay;
    x->FirstWeek = f->FirstWeek ? f->FirstWeek : f->L.FirstWeek;
    return S_OK;
}

/* the text a variant gives when it is shown as a string. */
static HRESULT text_of(const VARIANT *v, FMT *f, BSTR *text)
{
    VARIANT dest;
    VariantInit(&dest);
    HRESULT hr = VariantChangeTypeEx(&dest, (VARIANT *)v, f->Lcid, VARIANT_ALPHABOOL, VT_BSTR);
    if (FAILED(hr))
        return hr;
    *text = dest.bstrVal;
    return S_OK;
}

static const SECTION *text_section(const PICTURE *p, BOOL empty)
{
    if (!empty)
        return &p->Sec[0];
    return p->Count > 1 ? &p->Sec[1] : nullptr;
}

/* general number text, which is what a bare number shows. */
static HRESULT general_number(const VARIANT *v, FMT *f, BSTR *text)
{
    VARIANT src = *v, dest;
    VariantInit(&dest);
    if (v->vt == VT_BOOL) {
        WCHAR t[8];
        AutFormatInteger(v->boolVal, FALSE, t);
        *text = SysAllocString(t);
        return *text ? S_OK : E_OUTOFMEMORY;
    }
    if (v->vt == VT_DATE) {
        src.vt = VT_R8;
        src.dblVal = v->date;
    }
    HRESULT hr = VariantChangeTypeEx(&dest, &src, f->Lcid, 0, VT_BSTR);
    if (FAILED(hr))
        return hr;
    *text = dest.bstrVal;
    return S_OK;
}

static BOOL is_numeric_vt(VARTYPE vt)
{
    switch (vt) {
    case VT_I2: case VT_I4: case VT_R4: case VT_R8: case VT_CY: case VT_DATE: case VT_BOOL: case VT_DECIMAL:
    case VT_I1: case VT_UI1: case VT_UI2: case VT_UI4: case VT_I8: case VT_UI8: case VT_INT: case VT_UINT:
        return TRUE;
    default:
        return FALSE;
    }
}

/* whether the number is nothing once the section has scaled and rounded it. */
static BOOL rounds_to_zero(const PICTURE *p, const SECTION *s, const DECNUM *d, const LOCALE *l)
{
    NUMCTX x;
    memset(&x, 0, sizeof(x));
    x.P = p;
    x.S = s;
    x.L = l;
    x.D = *d;
    if (s->IntPlaces + s->FracPlaces == 0)
        return FALSE;
    number_prepare(&x);
    return !x.D.N;
}

static HRESULT format_number_value(const PICTURE *p, const VARIANT *num, FMT *f, TXT *o)
{
    DECNUM d;
    HRESULT hr = number_digits(num, &d);
    if (FAILED(hr))
        return hr;
    if (d.Infinity[0]) {
        put_str(o, d.Infinity);
        return S_OK;
    }
    int sign = d.N ? (d.Neg ? -1 : 1) : 0;
    const SECTION *s = &p->Sec[0];
    BOOL minus = FALSE;
    const SECTION *by_sign = sign < 0 && p->Count > 1 && p->Sec[1].Count ? &p->Sec[1] : s;
    if (sign && rounds_to_zero(p, by_sign, &d, &f->L))
        sign = 0;
    if (sign == 0) {
        if (p->Count > 2 && p->Sec[2].Count)
            s = &p->Sec[2];
    } else if (sign < 0) {
        if (by_sign != s)
            s = by_sign;
        else
            minus = TRUE;
    }
    NUMCTX x;
    memset(&x, 0, sizeof(x));
    x.P = p;
    x.S = s;
    x.L = &f->L;
    x.O = o;
    x.D = d;
    if (!s->Count)
        return S_OK;
    if (minus)
        put_str(o, f->L.Negative);
    number_prepare(&x);
    render_number(&x);
    return S_OK;
}

static HRESULT format_with(const VARIANT *in, const PICTURE *p, FMT *f, DWORD flags, BSTR *result);

/* the value of a variant after references and default properties. */
static HRESULT deref(const VARIANT *in, VARIANT *out, FMT *f)
{
    VariantInit(out);
    VARTYPE base = in->vt & ~VT_BYREF;
    if (in->vt & (VT_ARRAY | VT_VECTOR)) {
        if ((in->vt & VT_VECTOR) || (base & ~VT_ARRAY) > VT_UINT || (base & ~VT_ARRAY) == 0)
            return DISP_E_BADVARTYPE;
        return DISP_E_TYPEMISMATCH;
    }
    if (in->vt & VT_BYREF) {
        if (base == VT_VARIANT) {
            if (!in->pvarVal)
                return DISP_E_BADVARTYPE;
            return deref(in->pvarVal, out, f);
        }
        if (base == 0 || !in->byref)
            return DISP_E_BADVARTYPE;
    }
    if (base == VT_VARIANT)
        return DISP_E_TYPEMISMATCH;
    if (base == VT_RECORD)
        return DISP_E_TYPEMISMATCH;
    if (base == 15 || base > VT_UINT)
        return DISP_E_BADVARTYPE;
    HRESULT hr = VariantCopyInd(out, in);
    if (FAILED(hr))
        return hr;
    if (out->vt == VT_DISPATCH) {
        VARIANT inner;
        VariantInit(&inner);
        if (!out->pdispVal || FAILED(AutExtractValueProperty(out->pdispVal, f->Lcid, &inner))) {
            VariantClear(out);
            return DISP_E_TYPEMISMATCH;
        }
        VariantClear(out);
        return deref(&inner, out, f);
    }
    return S_OK;
}

static HRESULT format_with(const VARIANT *in, const PICTURE *p, FMT *f, DWORD flags, BSTR *result)
{
    VARIANT v;
    HRESULT hr = deref(in, &v, f);
    if (FAILED(hr))
        return hr;
    TXT *o = HeapAlloc(GetProcessHeap(), 0, sizeof(TXT));
    if (!o) {
        VariantClear(&v);
        return E_OUTOFMEMORY;
    }
    o->Length = 0;
    o->Over = FALSE;
    *result = nullptr;
    VARTYPE vt = v.vt;
    BOOL text_kind = p->Kind == SK_TEXT;
    BOOL date_kind = p->Kind == SK_DATE;
    BSTR text = nullptr;
    hr = S_OK;
    if (vt != VT_EMPTY && vt != VT_NULL && vt != VT_BSTR && !is_numeric_vt(vt)) {
        hr = vt == VT_UNKNOWN || vt == VT_ERROR || vt == VT_VARIANT || vt == VT_RECORD || vt == VT_DISPATCH ? DISP_E_TYPEMISMATCH : DISP_E_BADVARTYPE;
        goto done;
    }
    if (vt == VT_NULL) {
        if (text_kind) {
            const SECTION *s = text_section(p, TRUE);
            if (!s)
                goto done_null;
            render_text(p, s, L"", 0, o, f->Lcid);
        } else if (date_kind || p->Count < 4) {
            goto done_null;
        } else {
            /* the fourth section holds Null, written as a zero. */
            const SECTION *s = &p->Sec[3];
            VARIANT zero;
            zero.vt = VT_I4;
            zero.lVal = 0;
            if (p->Kind == SK_NUMBER && s->Count) {
                DECNUM d;
                number_digits(&zero, &d);
                NUMCTX x;
                memset(&x, 0, sizeof(x));
                x.P = p;
                x.S = s;
                x.L = &f->L;
                x.O = o;
                x.D = d;
                number_prepare(&x);
                render_number(&x);
            } else {
                for (int i = 0; i < s->Count; i++)
                    if (s->El[i].Kind == EL_LIT)
                        put_text(o, p->Pool + s->El[i].At, s->El[i].Count);
            }
        }
        goto done_text;
    }
    if (text_kind) {
        BOOL empty = FALSE;
        if (vt == VT_EMPTY) {
            empty = TRUE;
        } else if (vt == VT_BSTR) {
            empty = !v.bstrVal || !SysStringLen(v.bstrVal);
        } else {
            hr = text_of(&v, f, &text);
            if (FAILED(hr))
                goto done;
        }
        if (empty) {
            const SECTION *s = text_section(p, TRUE);
            if (!s)
                goto done_null;
            render_text(p, s, L"", 0, o, f->Lcid);
        } else {
            render_text(p, &p->Sec[0], text ? text : v.bstrVal, (int)SysStringLen(text ? text : v.bstrVal), o, f->Lcid);
        }
        goto done_text;
    }
    if (date_kind) {
        if (vt == VT_BSTR && (!v.bstrVal || !SysStringLen(v.bstrVal))) {
            if (flags & FLAG_ERRORS) {
                hr = v.bstrVal ? DISP_E_TYPEMISMATCH : E_INVALIDARG;
                goto done;
            }
            goto done_null;
        }
        DATECTX x;
        memset(&x, 0, sizeof(x));
        VARIANT src = v;
        VARIANT converted;
        VariantInit(&converted);
        hr = date_from_variant(&src, f, &x);
        if (FAILED(hr) && vt == VT_BSTR) {
            HRESULT parse = string_value(v.bstrVal, f->Lcid, &converted);
            if (FAILED(parse)) {
                if (flags & FLAG_ERRORS) {
                    hr = parse;
                    goto done;
                }
                put_text(o, v.bstrVal, (int)SysStringLen(v.bstrVal));
                hr = S_OK;
                goto done_text;
            }
            hr = date_from_variant(&converted, f, &x);
            VariantClear(&converted);
        }
        if (FAILED(hr))
            goto done;
        render_date(p, &p->Sec[0], &x, o);
        goto done_text;
    }
    /* number or no picture */
    {
        VARIANT num = v;
        VARIANT parsed;
        VariantInit(&parsed);
        if (vt == VT_EMPTY)
            goto done_null;
        if (vt == VT_BSTR) {
            if (!v.bstrVal || !SysStringLen(v.bstrVal)) {
                if (flags & FLAG_ERRORS) {
                    hr = v.bstrVal ? DISP_E_TYPEMISMATCH : E_INVALIDARG;
                    goto done;
                }
                goto done_null;
            }
            HRESULT parse = string_value(v.bstrVal, f->Lcid, &parsed);
            if (FAILED(parse)) {
                if (flags & FLAG_ERRORS) {
                    hr = parse;
                    goto done;
                }
                put_text(o, v.bstrVal, (int)SysStringLen(v.bstrVal));
                goto done_text;
            }
            num = parsed;
        }
        hr = format_number_value(p, &num, f, o);
        VariantClear(&parsed);
        if (FAILED(hr))
            goto done;
        goto done_text;
    }
done_null:
    HeapFree(GetProcessHeap(), 0, o);
    VariantClear(&v);
    SysFreeString(text);
    *result = nullptr;
    return S_OK;
done_text:
    hr = out_bstr(o, result);
done:
    HeapFree(GetProcessHeap(), 0, o);
    VariantClear(&v);
    SysFreeString(text);
    return hr;
}

/* ------------------------------------------------------------------ named formats */

/* the words of the languages the library knows, and the pattern of the medium date. */
typedef struct {
    USHORT Lcid, Language;
    const WCHAR *Word[7];
} WORDS;

static const WORDS g_words[] = {
    {0x409, 0x9, {L"False", L"True", L"No", L"Yes", L"Off", L"On", L"dd'-'MMM'-'yy"}},
    {0x401, 0x1, {L"False", L"True", L"\u0644\u0627", L"\u0646\u0639\u0645", L"\u0625\u064a\u0642\u0627\u0641", L"\u062a\u0634\u063a\u064a\u0644", L"dd'-'MMM'-'yy"}},
    {0x804, 0x0, {L"False", L"True", L"No", L"Yes", L"Off", L"On", L"yy'-'MM'-'dd"}},
    {0x404, 0x4, {L"False", L"True", L"No", L"Yes", L"Off", L"On", L"dd'-'MMM'-'yy"}},
    {0x405, 0x5, {L"Nepravda", L"Pravda", L"Ne", L"Ano", L"Vypnuto", L"Zapnuto", L"dd'.'MMM' 'yyyy"}},
    {0x406, 0x6, {L"Falsk", L"Sand", L"Nej", L"Ja", L"Fra", L"Til", L"dd'-'MMM'-'yy"}},
    {0x407, 0x7, {L"Falsch", L"Wahr", L"Nein", L"Ja", L"Aus", L"Ein", L"dd'. 'MMM'. 'yy"}},
    {0x408, 0x8, {L"\u03a8\u03b5\u03c5\u03b4\u03ad\u03c2", L"\u0391\u03bb\u03b7\u03b8\u03ad\u03c2", L"\u038c\u03c7\u03b9", L"\u039d\u03b1\u03b9", L"\u0386\u03ba\u03c5\u03c1\u03bf", L"\u0399\u03c3\u03c7\u03cd\u03b5\u03b9", L"dd'-'MMM'-'yy"}},
    {0x40a, 0xa, {L"Falso", L"Verdadero", L"No", L"S\u00ed", L"Desactivado", L"Activado", L"dd'-'MMM'-'yy"}},
    {0x40b, 0xb, {L"Ep\u00e4tosi", L"Tosi", L"No", L"Kyll\u00e4", L"Ei k\u00e4yt\u00f6ss\u00e4", L"K\u00e4yt\u00f6ss\u00e4", L"dd'-'MMM'-'yy"}},
    {0x40c, 0xc, {L"Faux", L"Vrai", L"Non", L"Oui", L"Inactif", L"Actif", L"dd'-'MMM'-'yy"}},
    {0x40d, 0xd, {L"False", L"True", L"\u05dc\u05d0", L"\u05db\u05df", L"\u05dc\u05d0 \u05e4\u05d5\u05e2\u05dc", L"\u05e4\u05d5\u05e2\u05dc", L"dd'-'MMM'-'yy"}},
    {0x40e, 0xe, {L"Hamis", L"Igaz", L"Nem", L"Igen", L"Ki", L"Be", L"dd'-'MMM'-'yy"}},
    {0x410, 0x10, {L"Falso", L"Vero", L"No", L"S\u00ec", L"Off", L"On", L"dd'-'MMM'-'yy"}},
    {0x411, 0x11, {L"False", L"True", L"No", L"Yes", L"Off", L"On", L"yy'-'MM'-'dd"}},
    {0x412, 0x12, {L"False", L"True", L"No", L"Yes", L"Off", L"On", L"yy'\ub144 'M'\uc6d4 'd'\uc77c'"}},
    {0x413, 0x13, {L"Onwaar", L"Waar", L"Nee", L"Ja", L"Uit", L"Aan", L"dd'-'MMM'-'yy"}},
    {0x414, 0x14, {L"Usann", L"Sann", L"Nei", L"Ja", L"Av", L"P\u00e5", L"dd'.'MM'.'yyyy"}},
    {0x415, 0x15, {L"Fa\u0142sz", L"Prawda", L"Nie", L"Tak", L"Wy\u0142", L"W\u0142", L"dd'-'MMM'-'yy"}},
    {0x816, 0x0, {L"Falso", L"Verdadeiro", L"N\u00e3o", L"Sim", L"Desligado", L"Ligado", L"dd'-'MMM'-'yy"}},
    {0x416, 0x16, {L"Falso", L"Verdadeiro", L"N\u00e3o", L"Sim", L"Desativado", L"Ativado", L"dd'/'MMM'/'yy"}},
    {0x419, 0x19, {L"\u041b\u043e\u0436\u044c", L"\u0418\u0441\u0442\u0438\u043d\u0430", L"\u041d\u0435\u0442", L"\u0414\u0430", L"\u0412\u044b\u043a\u043b", L"\u0412\u043a\u043b", L"dd'-'MMM'-'yy"}},
    {0x41d, 0x1d, {L"Falskt", L"Sant", L"Nej", L"Ja", L"Av", L"P\u00e5", L"dd'/'MM' 'yyyy"}},
    {0x41f, 0x1f, {L"Yanl\u0131\u015f", L"Do\u011fru", L"Hay\u0131r", L"Evet", L"Kapal\u0131", L"A\u00e7\u0131k", L"dd'-'MMM'-'yy"}},
    {0x424, 0x24, {L"Neresni\u010dno", L"Resni\u010dno", L"Ne", L"Da", L"Izklju\u010deno", L"Vklju\u010deno", L"dd'-'MMM'-'yy"}},
};

static const WORDS *words_of(LCID lcid)
{
    for (size_t i = 0; i < RTL_NUMBER_OF(g_words); i++) {
        const WORDS *w = &g_words[i];
        if (w->Language && w->Language == (lcid & 0x3FF))
            return w;
        if (w->Lcid == lcid)
            return w;
    }
    return &g_words[0];
}

static const WCHAR *const g_named[] = {L"general number", L"long time", L"medium time", L"short time", L"general date", L"long date",
                                       L"medium date", L"short date", L"fixed", L"standard", L"percent", L"scientific", L"currency",
                                       L"true/false", L"yes/no", L"on/off"};

static int named_index(const WCHAR *pic, LCID lcid)
{
    if (!pic)
        return -1;
    int len = (int)wcslen(pic);
    for (int i = 0; i < (int)RTL_NUMBER_OF(g_named); i++)
        if ((int)wcslen(g_named[i]) == len &&
            CompareStringW(lcid, NORM_IGNORECASE | NORM_IGNOREKANATYPE, pic, len, g_named[i], len) == CSTR_EQUAL)
            return i;
    return -1;
}

static void put_quoted(WCHAR *out, int *n, const WCHAR *s)
{
    if (!*s)
        return;
    out[(*n)++] = L'"';
    while (*s)
        out[(*n)++] = *s++;
    out[(*n)++] = L'"';
}

static void put_plain(WCHAR *out, int *n, const WCHAR *s)
{
    while (*s)
        out[(*n)++] = *s++;
}

/* the digit part of a number picture. */
static void digits_picture(WCHAR *out, int *n, int digits, int places, BOOL lead, BOOL group)
{
    if (group) {
        put_plain(out, n, lead ? L"#,##0" : L"#,###");
    } else {
        put_plain(out, n, lead ? L"0" : L"#");
    }
    if (digits > 0) {
        out[(*n)++] = L'.';
        for (int i = 0; i < places; i++)
            out[(*n)++] = L'0';
    }
}

/* wraps a body in the negative number style of the locale. */
static void number_picture(WCHAR *out, const LOCALE *l, int digits, BOOL lead, BOOL group, int neg_style, const WCHAR *suffix)
{
    int n = 0;
    WCHAR body[330];
    int b = 0;
    digits_picture(body, &b, digits, digits & 255, lead, group);
    if (suffix)
        put_plain(body, &b, suffix);
    body[b] = 0;
    put_plain(out, &n, body);
    out[n++] = L';';
    switch (neg_style) {
    case 0:
        out[n++] = L'(';
        put_plain(out, &n, body);
        out[n++] = L')';
        break;
    case 2:
        put_quoted(out, &n, l->Negative);
        put_plain(out, &n, L"\" \"");
        put_plain(out, &n, body);
        break;
    case 3:
        put_plain(out, &n, body);
        put_quoted(out, &n, l->Negative);
        break;
    case 4:
        put_plain(out, &n, body);
        put_plain(out, &n, L"\" \"");
        put_quoted(out, &n, l->Negative);
        break;
    default:
        put_quoted(out, &n, l->Negative);
        put_plain(out, &n, body);
        break;
    }
    out[n] = 0;
}

/* the currency picture: the symbol and the number placed by the locale. A negative style below zero wraps the positive one. */
static void template_picture(WCHAR *out, int *n, const WCHAR *tpl, const WCHAR *symbol, const WCHAR *minus, const WCHAR *body)
{
    for (const WCHAR *c = tpl; *c; c++) {
        if (*c == L'$')
            put_quoted(out, n, symbol);
        else if (*c == L'-')
            put_quoted(out, n, minus);
        else if (*c == L'n')
            put_plain(out, n, body);
        else {
            WCHAR t[2] = {*c, 0};
            put_quoted(out, n, t);
        }
    }
}

static void currency_picture(WCHAR *out, const LOCALE *l, int digits, BOOL lead, BOOL group, int neg)
{
    static const WCHAR *const positive[4] = {L"$n", L"n$", L"$ n", L"n $"};
    static const WCHAR *const negative[16] = {L"($n)", L"-$n", L"$-n",  L"$n-",  L"(n$)",  L"-n$", L"n-$", L"n$-",
                                              L"-n $", L"-$ n", L"n $-", L"$ n-", L"$ -n", L"n- $", L"($ n)", L"(n $)"};
    WCHAR body[330];
    int b = 0, n = 0;
    digits_picture(body, &b, digits, digits & 255, lead, group);
    body[b] = 0;
    const WCHAR *pos = positive[l->CurrencyPos & 3];
    template_picture(out, &n, pos, l->Currency, l->Negative, body);
    out[n++] = L';';
    template_picture(out, &n, negative[neg & 15], l->Currency, l->Negative, body);
    out[n] = 0;
}

/* the pattern of the library for the medium date, written as a picture of Format. */
static void medium_date_picture(WCHAR *out, const WCHAR *pattern)
{
    int n = 0;
    for (const WCHAR *c = pattern; *c;) {
        if (*c == L'\'') {
            for (c++; *c && *c != L'\''; c++) {
                out[n++] = L'\\';
                out[n++] = *c;
            }
            if (*c)
                c++;
        } else if (*c == L'd' || *c == L'M' || *c == L'y') {
            out[n++] = *c == L'M' ? L'm' : *c;
            c++;
        } else {
            out[n++] = L'\\';
            out[n++] = *c++;
        }
    }
    out[n] = 0;
}

/* the picture a named format stands for, or null when it is a date or a time that the renderer knows by itself. */
static BOOL named_picture(int index, const LOCALE *l, WCHAR *out)
{
    switch (index) {
    case 0: lstrcpyW(out, L""); return TRUE;
    case 1: lstrcpyW(out, L"ttttt"); return TRUE;
    case 2: lstrcpyW(out, L"hh:nn AMPM"); return TRUE;
    case 3: lstrcpyW(out, L"hh:nn"); return TRUE;
    case 4: lstrcpyW(out, L"c"); return TRUE;
    case 5: lstrcpyW(out, L"dddddd"); return TRUE;
    case 6: medium_date_picture(out, words_of(l->Lcid)->Word[6]); return TRUE;
    case 7: lstrcpyW(out, L"ddddd"); return TRUE;
    case 8: lstrcpyW(out, L"0.00"); return TRUE;
    case 9: lstrcpyW(out, L"#,##0.00"); return TRUE;
    case 10: lstrcpyW(out, L"0.00%"); return TRUE;
    case 11: lstrcpyW(out, L"0.00E+00"); return TRUE;
    case 12: currency_picture(out, l, l->CurrencyDigits, TRUE, TRUE, l->CurrencyNeg); return TRUE;
    default: return FALSE;
    }
}

static HRESULT yes_no(const VARIANT *in, FMT *f, int index, BSTR *result, DWORD flags)
{
    const WORDS *words = words_of(f->Lcid);
    VARIANT v;
    HRESULT hr = deref(in, &v, f);
    if (FAILED(hr))
        return hr;
    *result = nullptr;
    VARIANT num = v, parsed;
    VariantInit(&parsed);
    if (v.vt == VT_NULL || v.vt == VT_EMPTY || (v.vt == VT_BSTR && (!v.bstrVal || !SysStringLen(v.bstrVal)))) {
        hr = S_OK;
        if ((flags & FLAG_ERRORS) && v.vt == VT_BSTR)
            hr = v.bstrVal ? DISP_E_TYPEMISMATCH : E_INVALIDARG;
        VariantClear(&v);
        return hr;
    }
    if (v.vt == VT_BSTR) {
        HRESULT parse = string_value(v.bstrVal, f->Lcid, &parsed);
        if (FAILED(parse)) {
            if (flags & FLAG_ERRORS) {
                VariantClear(&v);
                return parse;
            }
            *result = SysAllocStringLen(v.bstrVal, SysStringLen(v.bstrVal));
            VariantClear(&v);
            return *result ? S_OK : E_OUTOFMEMORY;
        }
        num = parsed;
    } else if (!is_numeric_vt(v.vt)) {
        VariantClear(&v);
        return DISP_E_TYPEMISMATCH;
    }
    DECNUM d;
    hr = number_digits(&num, &d);
    VariantClear(&parsed);
    VariantClear(&v);
    if (FAILED(hr))
        return hr;
    const WCHAR *s = d.Infinity[0] ? d.Infinity : words->Word[index * 2 + (d.N ? 1 : 0)];
    *result = SysAllocString(s);
    return *result ? S_OK : E_OUTOFMEMORY;
}

/* ------------------------------------------------------------------ the entry points */

static HRESULT format_picture(const VARIANT *in, const WCHAR *picture, int first_day, int first_week, DWORD flags, BSTR *result,
                              LCID lcid)
{
    FMT *f = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FMT));
    PICTURE *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PICTURE));
    if (!f || !p) {
        AutFree(f);
        AutFree(p);
        return E_OUTOFMEMORY;
    }
    f->Lcid = lcid;
    f->Flags = flags;
    f->FirstDay = first_day;
    f->FirstWeek = first_week;
    load_locale(&f->L, lcid, flags & LOCALE_NOUSEROVERRIDE);
    WCHAR built[1200];
    HRESULT hr;
    int named = picture && *picture ? named_index(picture, lcid) : -1;
    if (named >= 13) {
        hr = yes_no(in, f, named - 13, result, flags);
        goto out;
    }
    if (named > 0) {
        named_picture(named, &f->L, built);
        picture = built;
    }
    if (named != 0 && (!picture || !*picture)) {
        /* with no picture the value shows as it is. */
        VARIANT v;
        hr = deref(in, &v, f);
        if (FAILED(hr))
            goto out;
        *result = nullptr;
        VARIANT shown = v, parsed;
        VariantInit(&parsed);
        if (v.vt == VT_BSTR && v.bstrVal && SysStringLen(v.bstrVal) && SUCCEEDED(string_value(v.bstrVal, lcid, &parsed)))
            shown = parsed;
        if (shown.vt == VT_NULL || shown.vt == VT_EMPTY || (shown.vt == VT_BSTR && (!shown.bstrVal || !SysStringLen(shown.bstrVal)))) {
            hr = S_OK;
        } else if (shown.vt == VT_DATE) {
            parse_picture(p, L"c");
            hr = format_with(&shown, p, f, flags, result);
        } else if (shown.vt == VT_BSTR || shown.vt == VT_BOOL) {
            BSTR text;
            if (shown.vt == VT_BSTR) {
                text = SysAllocStringLen(shown.bstrVal, SysStringLen(shown.bstrVal));
                hr = text ? S_OK : E_OUTOFMEMORY;
            } else {
                hr = text_of(&shown, f, &text);
            }
            *result = text;
        } else if (is_numeric_vt(shown.vt)) {
            hr = general_number(&shown, f, result);
        } else {
            hr = DISP_E_TYPEMISMATCH;
        }
        VariantClear(&parsed);
        VariantClear(&v);
        goto out;
    }
    if (named == 0) {
        VARIANT v;
        hr = deref(in, &v, f);
        if (FAILED(hr))
            goto out;
        *result = nullptr;
        VARIANT shown = v, parsed;
        VariantInit(&parsed);
        HRESULT parse = S_OK;
        if (v.vt == VT_BSTR && v.bstrVal && SysStringLen(v.bstrVal)) {
            parse = string_value(v.bstrVal, lcid, &parsed);
            if (SUCCEEDED(parse))
                shown = parsed;
        }
        if ((flags & FLAG_ERRORS) && v.vt == VT_BSTR && (!v.bstrVal || !SysStringLen(v.bstrVal))) {
            hr = v.bstrVal ? DISP_E_TYPEMISMATCH : E_INVALIDARG;
        } else if ((flags & FLAG_ERRORS) && FAILED(parse)) {
            hr = parse;
        } else if (shown.vt == VT_NULL || shown.vt == VT_EMPTY || (shown.vt == VT_BSTR && (!shown.bstrVal || !SysStringLen(shown.bstrVal)))) {
            hr = S_OK;
        } else if (shown.vt == VT_BSTR) {
            *result = SysAllocStringLen(shown.bstrVal, SysStringLen(shown.bstrVal));
            hr = *result ? S_OK : E_OUTOFMEMORY;
        } else if (is_numeric_vt(shown.vt)) {
            hr = general_number(&shown, f, result);
        } else {
            hr = DISP_E_TYPEMISMATCH;
        }
        VariantClear(&parsed);
        VariantClear(&v);
        goto out;
    }
    parse_picture(p, picture);
    hr = format_with(in, p, f, flags, result);
out:
    AutFree(f);
    AutFree(p);
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI VarFormat(LPVARIANT in, LPOLESTR picture, int first_day, int first_week, ULONG flags, BSTR *out)
{
    if (!in || !out || (unsigned)first_day > 7 || (unsigned)first_week > 3)
        return E_INVALIDARG;
    return format_picture(in, picture, first_day, first_week, flags, out, GetUserDefaultLCID());
}

WINOLEAUTAPI HRESULT WINAPI VarFormatDateTime(LPVARIANT in, int named, ULONG flags, BSTR *out)
{
    static const WCHAR *const pictures[5] = {L"c", L"dddddd", L"ddddd", L"ttttt", L"hh:nn"};
    if (!in || !out || (unsigned)named > 4)
        return E_INVALIDARG;
    return format_picture(in, pictures[named], 0, 0, flags | FLAG_ERRORS, out, GetUserDefaultLCID());
}

static HRESULT format_numeric(LPVARIANT in, int digits, int lead, int parens, int group, ULONG flags, BSTR *out, int kind)
{
    LCID lcid = GetUserDefaultLCID();
    LOCALE l;
    load_locale(&l, lcid, flags & LOCALE_NOUSEROVERRIDE);
    if (!in || !out)
        return E_INVALIDARG;
    if (digits < -1 || (kind == 2 && digits > 255))
        return E_INVALIDARG;
    if (kind != 1 && in->vt == VT_BYREF)
        return DISP_E_BADVARTYPE;
    if ((lead != -2 && lead != -1 && lead != 0) || (parens != -2 && parens != -1 && parens != 0) ||
        (group != -2 && group != -1 && group != 0))
        return E_INVALIDARG;
    BOOL use_lead = lead == -2 ? l.LeadingZero != 0 : lead == -1;
    BOOL use_group = group == -2 ? l.Grouping != 0 : group == -1;
    WCHAR picture[1200];
    if (kind == 2) {
        int d = digits < 0 ? l.CurrencyDigits : digits;
        if (group == -2)
            use_group = l.MonGrouping != 0;
        /* a style with a sign turns into the one with brackets that has the symbol on the same side, and back. */
        static const BYTE to_brackets[16] = {0, 0, 0, 0, 4, 4, 4, 4, 15, 14, 15, 14, 14, 15, 14, 15};
        static const BYTE to_sign[16] = {1, 1, 2, 3, 5, 5, 6, 7, 8, 9, 10, 11, 12, 13, 9, 8};
        int neg = l.CurrencyNeg & 15;
        if (parens == -1)
            neg = to_brackets[neg];
        else if (parens == 0)
            neg = to_sign[neg];
        currency_picture(picture, &l, d, use_lead, use_group, neg);
    } else {
        int d = digits < 0 ? l.Digits : digits;
        int neg = l.NegativeNumber;
        if (parens == -1)
            neg = 0;
        else if (parens == 0 && neg == 0)
            neg = 1;
        number_picture(picture, &l, d, use_lead, use_group, neg, kind == 1 ? L"%" : nullptr);
    }
    return format_picture(in, picture, 0, 0, flags | FLAG_ERRORS, out, lcid);
}

WINOLEAUTAPI HRESULT WINAPI VarFormatNumber(LPVARIANT in, int digits, int lead, int parens, int group, ULONG flags, BSTR *out)
{
    return format_numeric(in, digits, lead, parens, group, flags, out, 0);
}

WINOLEAUTAPI HRESULT WINAPI VarFormatPercent(LPVARIANT in, int digits, int lead, int parens, int group, ULONG flags, BSTR *out)
{
    return format_numeric(in, digits, lead, parens, group, flags, out, 1);
}

WINOLEAUTAPI HRESULT WINAPI VarFormatCurrency(LPVARIANT in, int digits, int lead, int parens, int group, ULONG flags, BSTR *out)
{
    return format_numeric(in, digits, lead, parens, group, flags, out, 2);
}

WINOLEAUTAPI HRESULT WINAPI VarMonthName(int month, int abbreviate, ULONG flags, BSTR *out)
{
    if ((unsigned)(month - 1) > 11 || !out)
        return E_INVALIDARG;
    LOCALE *l = HeapAlloc(GetProcessHeap(), 0, sizeof(LOCALE));
    if (!l)
        return E_OUTOFMEMORY;
    load_locale(l, LOCALE_USER_DEFAULT, 0);
    /* the names of the Islamic months are the same for both sizes. */
    static const WCHAR *const hijri[12] = {L"\u0645\u062d\u0631\u0645", L"\u0635\u0641\u0631", L"\u0631\u0628\u064a\u0639 \u0627\u0644\u0627\u0648\u0644",
                                           L"\u0631\u0628\u064a\u0639 \u0627\u0644\u062b\u0627\u0646\u064a", L"\u062c\u0645\u0627\u062f\u0649 \u0627\u0644\u0627\u0648\u0644\u0649",
                                           L"\u062c\u0645\u0627\u062f\u0649 \u0627\u0644\u062b\u0627\u0646\u064a\u0629", L"\u0631\u062c\u0628", L"\u0634\u0639\u0628\u0627\u0646",
                                           L"\u0631\u0645\u0636\u0627\u0646", L"\u0634\u0648\u0627\u0644", L"\u0630\u0648 \u0627\u0644\u0642\u0639\u062f\u0629",
                                           L"\u0630\u0648 \u0627\u0644\u062d\u062c\u0629"};
    const WCHAR *name = (flags & VARIANT_CALENDAR_HIJRI) ? hijri[month - 1] : abbreviate ? l->MonthShort[month - 1] : l->MonthLong[month - 1];
    HRESULT hr = *name ? S_OK : E_INVALIDARG;
    if (hr == S_OK) {
        *out = SysAllocString(name);
        if (!*out)
            hr = E_OUTOFMEMORY;
    }
    AutFree(l);
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI VarWeekdayName(int weekday, int abbreviate, int first_day, ULONG flags, BSTR *out)
{
    if ((unsigned)(weekday - 1) > 6 || (unsigned)first_day > 7 || !out)
        return E_INVALIDARG;
    LOCALE *l = HeapAlloc(GetProcessHeap(), 0, sizeof(LOCALE));
    if (!l)
        return E_OUTOFMEMORY;
    load_locale(l, LOCALE_USER_DEFAULT, 0);
    if (!first_day)
        first_day = l->FirstDay;
    int index = (first_day + weekday - 2) % 7;
    const WCHAR *name = abbreviate ? l->DayShort[index] : l->DayLong[index];
    *out = SysAllocString(name);
    HRESULT hr = *out ? S_OK : E_OUTOFMEMORY;
    AutFree(l);
    return hr;
}
