/*
 * locales: identifiers and names, locale information, date, time, number
 * and currency formatting, string comparison and mapping.
 */
#include "kernelbase_private.h"

/* ------------------------------------------------------------------ locale data */

typedef struct LOCALE_DATA {
    LCID Lcid;
    PCWSTR Name;
    PCWSTR Parent;
    PCWSTR EnglishLanguage;
    PCWSTR NativeLanguage;
    PCWSTR EnglishCountry;
    PCWSTR NativeCountry;
    PCWSTR DisplayName;
    PCWSTR NativeDisplayName;
    PCWSTR AbbrevLanguage;
    PCWSTR AbbrevCountry;
    PCWSTR Iso639;
    PCWSTR Iso639_2;
    PCWSTR Iso3166;
    PCWSTR Iso3166_2;
    UINT AnsiCodePage;
    UINT OemCodePage;
    UINT MacCodePage;
    UINT EbcdicCodePage;
    UINT CountryCode;
    UINT GeoId;
    PCWSTR List;
    PCWSTR Decimal;
    PCWSTR Thousand;
    PCWSTR Grouping;
    PCWSTR Currency;
    PCWSTR IntlSymbol;
    PCWSTR MonDecimal;
    PCWSTR MonThousand;
    PCWSTR CurrencyEnglish;
    PCWSTR CurrencyNative;
    UINT Digits;
    UINT LeadingZero;
    UINT NegativeNumber;
    UINT CurrencyDigits;
    UINT PositiveCurrency;
    UINT NegativeCurrency;
    UINT Measure;
    UINT PaperSize;
    PCWSTR ShortDate;
    PCWSTR LongDate;
    PCWSTR TimeFormat;
    PCWSTR ShortTime;
    PCWSTR YearMonth;
    PCWSTR MonthDay;
    PCWSTR DateSeparator;
    PCWSTR TimeSeparator;
    PCWSTR Am;
    PCWSTR Pm;
    UINT DateOrder;
    UINT TimeMarkPosition;
    UINT Hour24;
    UINT FirstDayOfWeek;
    UINT FirstWeekOfYear;
    PCWSTR Days[7];        /* monday first. */
    PCWSTR AbbrevDays[7];
    PCWSTR ShortestDays[7];
    PCWSTR Months[12];
    PCWSTR AbbrevMonths[12];
    PCWSTR GenitiveMonths[12];
    PCWSTR Keyboard;
} LOCALE_DATA;

static const LOCALE_DATA g_en_us = {
    0x0409, L"en-US", L"en", L"English", L"English", L"United States", L"United States", L"English (United States)",
    L"English (United States)", L"ENU", L"USA", L"en", L"eng", L"US", L"USA", 1252, 437, 10000, 37, 1, 244, L",",
    L".", L",", L"3;0", L"$", L"USD", L".", L",", L"US Dollar", L"US Dollar", 2, 1, 1, 2, 0, 0, 1, 1, L"M/d/yyyy",
    L"dddd, MMMM d, yyyy", L"h:mm:ss tt", L"h:mm tt", L"MMMM yyyy", L"MMMM d", L"/", L":", L"AM", L"PM", 0, 0, 0, 6, 0,
    {L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday", L"Sunday"},
    {L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun"},
    {L"Mo", L"Tu", L"We", L"Th", L"Fr", L"Sa", L"Su"},
    {L"January", L"February", L"March", L"April", L"May", L"June", L"July", L"August", L"September", L"October",
     L"November", L"December"},
    {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"},
    {nullptr},
    L"0409:00000409",
};

static const LOCALE_DATA g_en_gb = {
    0x0809, L"en-GB", L"en", L"English", L"English", L"United Kingdom", L"United Kingdom", L"English (United Kingdom)",
    L"English (United Kingdom)", L"ENG", L"GBR", L"en", L"eng", L"GB", L"GBR", 1252, 850, 10000, 20285, 44, 242, L",",
    L".", L",", L"3;0", L"£", L"GBP", L".", L",", L"British Pound", L"British Pound", 2, 1, 1, 2, 0, 1, 0, 9,
    L"dd/MM/yyyy", L"dd MMMM yyyy", L"HH:mm:ss", L"HH:mm", L"MMMM yyyy", L"d MMMM", L"/", L":", L"am", L"pm", 1, 0, 1,
    0, 2,
    {L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday", L"Sunday"},
    {L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun"},
    {L"Mo", L"Tu", L"We", L"Th", L"Fr", L"Sa", L"Su"},
    {L"January", L"February", L"March", L"April", L"May", L"June", L"July", L"August", L"September", L"October",
     L"November", L"December"},
    {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"},
    {nullptr},
    L"0809:00000809",
};

static const LOCALE_DATA g_fr_fr = {
    0x040C, L"fr-FR", L"fr", L"French", L"français", L"France", L"France", L"French (France)", L"français (France)",
    L"FRA", L"FRA", L"fr", L"fra", L"FR", L"FRA", 1252, 850, 10000, 20297, 33, 84, L";", L",", L"\x202F", L"3;0",
    L"€", L"EUR", L",", L"\x202F", L"Euro", L"euro", 2, 1, 1, 2, 3, 8, 0, 9, L"dd/MM/yyyy", L"dddd d MMMM yyyy",
    L"HH:mm:ss", L"HH:mm", L"MMMM yyyy", L"d MMMM", L"/", L":", L"", L"", 1, 0, 1, 0, 2,
    {L"lundi", L"mardi", L"mercredi", L"jeudi", L"vendredi", L"samedi", L"dimanche"},
    {L"lun.", L"mar.", L"mer.", L"jeu.", L"ven.", L"sam.", L"dim."},
    {L"lu", L"ma", L"me", L"je", L"ve", L"sa", L"di"},
    {L"janvier", L"février", L"mars", L"avril", L"mai", L"juin", L"juillet", L"août", L"septembre", L"octobre",
     L"novembre", L"décembre"},
    {L"janv.", L"févr.", L"mars", L"avr.", L"mai", L"juin", L"juil.", L"août", L"sept.", L"oct.", L"nov.", L"déc."},
    {nullptr},
    L"040c:0000040c",
};

static const LOCALE_DATA g_de_de = {
    0x0407, L"de-DE", L"de", L"German", L"Deutsch", L"Germany", L"Deutschland", L"German (Germany)",
    L"Deutsch (Deutschland)", L"DEU", L"DEU", L"de", L"deu", L"DE", L"DEU", 1252, 850, 10000, 20273, 49, 94, L";",
    L",", L".", L"3;0", L"€", L"EUR", L",", L".", L"Euro", L"Euro", 2, 1, 1, 2, 3, 8, 0, 9, L"dd.MM.yyyy",
    L"dddd, d. MMMM yyyy", L"HH:mm:ss", L"HH:mm", L"MMMM yyyy", L"d. MMMM", L".", L":", L"", L"", 1, 0, 1, 0, 2,
    {L"Montag", L"Dienstag", L"Mittwoch", L"Donnerstag", L"Freitag", L"Samstag", L"Sonntag"},
    {L"Mo", L"Di", L"Mi", L"Do", L"Fr", L"Sa", L"So"},
    {L"Mo", L"Di", L"Mi", L"Do", L"Fr", L"Sa", L"So"},
    {L"Januar", L"Februar", L"März", L"April", L"Mai", L"Juni", L"Juli", L"August", L"September", L"Oktober",
     L"November", L"Dezember"},
    {L"Jan", L"Feb", L"Mär", L"Apr", L"Mai", L"Jun", L"Jul", L"Aug", L"Sep", L"Okt", L"Nov", L"Dez"},
    {nullptr},
    L"0407:00000407",
};

static const LOCALE_DATA g_invariant = {
    0x007F, L"", L"", L"Invariant Language", L"Invariant Language", L"Invariant Country", L"Invariant Country",
    L"Invariant Language (Invariant Country)", L"Invariant Language (Invariant Country)", L"IVL", L"IVC", L"iv",
    L"ivl", L"IV", L"ivc", 1252, 437, 10000, 37, 1, 244, L",", L".", L",", L"3;0", L"¤", L"XDR", L".", L",",
    L"International Monetary Fund", L"International Monetary Fund", 2, 1, 1, 2, 0, 0, 0, 9, L"MM/dd/yyyy",
    L"dddd, dd MMMM yyyy", L"HH:mm:ss", L"HH:mm", L"yyyy MMMM", L"MMMM dd", L"/", L":", L"AM", L"PM", 0, 0, 1, 0, 0,
    {L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday", L"Sunday"},
    {L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun"},
    {L"Mo", L"Tu", L"We", L"Th", L"Fr", L"Sa", L"Su"},
    {L"January", L"February", L"March", L"April", L"May", L"June", L"July", L"August", L"September", L"October",
     L"November", L"December"},
    {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"},
    {nullptr},
    L"0409:00000409",
};

static const LOCALE_DATA *const g_locales[] = {&g_en_us, &g_en_gb, &g_fr_fr, &g_de_de, &g_invariant};

/* names and identifiers of locales without data, for the identifier conversions. */
typedef struct LOCALE_ID {
    LCID Lcid;
    PCWSTR Name;
} LOCALE_ID;

static const LOCALE_ID g_locale_ids[] = {
    {0x0401, L"ar-SA"}, {0x0402, L"bg-BG"}, {0x0403, L"ca-ES"}, {0x0404, L"zh-TW"}, {0x0405, L"cs-CZ"},
    {0x0406, L"da-DK"}, {0x0407, L"de-DE"}, {0x0408, L"el-GR"}, {0x0409, L"en-US"}, {0x040A, L"es-ES_tradnl"},
    {0x040B, L"fi-FI"}, {0x040C, L"fr-FR"}, {0x040D, L"he-IL"}, {0x040E, L"hu-HU"}, {0x040F, L"is-IS"},
    {0x0410, L"it-IT"}, {0x0411, L"ja-JP"}, {0x0412, L"ko-KR"}, {0x0413, L"nl-NL"}, {0x0414, L"nb-NO"},
    {0x0415, L"pl-PL"}, {0x0416, L"pt-BR"}, {0x0418, L"ro-RO"}, {0x0419, L"ru-RU"}, {0x041A, L"hr-HR"},
    {0x041B, L"sk-SK"}, {0x041D, L"sv-SE"}, {0x041E, L"th-TH"}, {0x041F, L"tr-TR"}, {0x0420, L"ur-PK"},
    {0x0421, L"id-ID"}, {0x0422, L"uk-UA"}, {0x0424, L"sl-SI"}, {0x0425, L"et-EE"}, {0x0426, L"lv-LV"},
    {0x0427, L"lt-LT"}, {0x042A, L"vi-VN"}, {0x0439, L"hi-IN"}, {0x0804, L"zh-CN"}, {0x0807, L"de-CH"},
    {0x0809, L"en-GB"}, {0x080A, L"es-MX"}, {0x080C, L"fr-BE"}, {0x0810, L"it-CH"}, {0x0813, L"nl-BE"},
    {0x0816, L"pt-PT"}, {0x0C07, L"de-AT"}, {0x0C09, L"en-AU"}, {0x0C0A, L"es-ES"}, {0x0C0C, L"fr-CA"},
    {0x1009, L"en-CA"}, {0x100C, L"fr-CH"}, {0x1409, L"en-NZ"}, {0x1809, L"en-IE"}, {0x4009, L"en-IN"},
    {0x0001, L"ar"},    {0x0004, L"zh-Hans"}, {0x0007, L"de"},  {0x0009, L"en"},    {0x000A, L"es"},
    {0x000C, L"fr"},    {0x0010, L"it"},    {0x0011, L"ja"},    {0x0012, L"ko"},    {0x0013, L"nl"},
    {0x0016, L"pt"},    {0x0019, L"ru"},    {0x007F, L""},
};

static LCID g_user_lcid;
static LCID g_system_lcid;
static LANGID g_ui_language;
static LANGID g_thread_ui_language;

static LCID user_lcid(void)
{
    if (!g_user_lcid) {
        LCID l = 0x0409;
        NtQueryDefaultLocale(TRUE, &l);
        g_user_lcid = l;
    }
    return g_user_lcid;
}

static LCID system_lcid(void)
{
    if (!g_system_lcid) {
        LCID l = 0x0409;
        NtQueryDefaultLocale(FALSE, &l);
        g_system_lcid = l;
    }
    return g_system_lcid;
}

LCID KbResolveLcid(LCID lcid)
{
    LANGID lang = LANGIDFROMLCID(lcid);
    switch (lang) {
    case LANG_USER_DEFAULT:
    case 0x0000:
    case 0x0C00: /* LOCALE_CUSTOM_DEFAULT. */
    case 0x1000: /* LOCALE_CUSTOM_UNSPECIFIED. */
    case 0x1400: /* LOCALE_CUSTOM_UI_DEFAULT. */
        return user_lcid();
    case LANG_SYSTEM_DEFAULT:
        return system_lcid();
    default:
        return lang;
    }
}

static const LOCALE_DATA *find_locale(LCID lcid)
{
    lcid = KbResolveLcid(lcid);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_locales); i++)
        if (g_locales[i]->Lcid == lcid)
            return g_locales[i];
    /* a neutral language resolves to its default locale. */
    if (SUBLANGID(lcid) == SUBLANG_NEUTRAL) {
        for (ULONG i = 0; i < RTL_NUMBER_OF(g_locales); i++)
            if (PRIMARYLANGID(g_locales[i]->Lcid) == PRIMARYLANGID(lcid) && SUBLANGID(g_locales[i]->Lcid) == 1)
                return g_locales[i];
    }
    return nullptr;
}

BOOL KbIsSupportedLcid(LCID lcid)
{
    return find_locale(lcid) != nullptr;
}

static const LOCALE_DATA *locale_or_error(LCID lcid)
{
    const LOCALE_DATA *l = find_locale(lcid);
    if (!l)
        KbSetError(ERROR_INVALID_PARAMETER);
    return l;
}

static LCID lcid_from_name(LPCWSTR name, BOOL allow_neutral)
{
    if (!name)
        return user_lcid();
    if (!*name)
        return LOCALE_INVARIANT;
    if (!_wcsicmp(name, LOCALE_NAME_SYSTEM_DEFAULT))
        return system_lcid();
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_locale_ids); i++) {
        if (!_wcsicmp(g_locale_ids[i].Name, name)) {
            LCID lcid = g_locale_ids[i].Lcid;
            if (SUBLANGID(lcid) == SUBLANG_NEUTRAL && lcid != LOCALE_INVARIANT && !allow_neutral) {
                /* without neutral names, a language maps to its main locale. */
                for (ULONG k = 0; k < RTL_NUMBER_OF(g_locale_ids); k++)
                    if (PRIMARYLANGID(g_locale_ids[k].Lcid) == PRIMARYLANGID(lcid) &&
                        SUBLANGID(g_locale_ids[k].Lcid) == 1)
                        return g_locale_ids[k].Lcid;
            }
            return lcid;
        }
    }
    return 0;
}

static PCWSTR name_from_lcid(LCID lcid, BOOL allow_neutral)
{
    lcid = KbResolveLcid(lcid);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_locale_ids); i++) {
        if (g_locale_ids[i].Lcid == lcid) {
            if (SUBLANGID(lcid) == SUBLANG_NEUTRAL && lcid != LOCALE_INVARIANT && !allow_neutral)
                break;
            return g_locale_ids[i].Name;
        }
    }
    if (SUBLANGID(lcid) == SUBLANG_NEUTRAL) {
        for (ULONG i = 0; i < RTL_NUMBER_OF(g_locale_ids); i++)
            if (PRIMARYLANGID(g_locale_ids[i].Lcid) == PRIMARYLANGID(lcid) && SUBLANGID(g_locale_ids[i].Lcid) == 1)
                return g_locale_ids[i].Name;
    }
    return nullptr;
}

/* ------------------------------------------------------------------ default locales */

LCID WINAPI GetUserDefaultLCID(void)
{
    return user_lcid();
}

LANGID WINAPI GetUserDefaultLangID(void)
{
    return LANGIDFROMLCID(user_lcid());
}

LCID WINAPI GetSystemDefaultLCID(void)
{
    return system_lcid();
}

LANGID WINAPI GetSystemDefaultLangID(void)
{
    return LANGIDFROMLCID(system_lcid());
}

LANGID WINAPI GetUserDefaultUILanguage(void)
{
    if (!g_ui_language) {
        LANGID l = 0x0409;
        NtQueryDefaultUILanguage(&l);
        g_ui_language = l;
    }
    return g_ui_language;
}

LANGID WINAPI GetSystemDefaultUILanguage(void)
{
    LANGID l = 0x0409;
    NtQueryInstallUILanguage(&l);
    return l;
}

LCID WINAPI GetThreadLocale(void)
{
    LCID l = NtCurrentTeb()->CurrentLocale;
    return l ? l : user_lcid();
}

BOOL WINAPI SetThreadLocale(LCID locale)
{
    LCID resolved = KbResolveLcid(locale);
    if (!find_locale(resolved) && !name_from_lcid(resolved, TRUE)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    NtCurrentTeb()->CurrentLocale = resolved;
    return TRUE;
}

LANGID WINAPI GetThreadUILanguage(void)
{
    return g_thread_ui_language ? g_thread_ui_language : GetUserDefaultUILanguage();
}

LANGID WINAPI SetThreadUILanguage(LANGID language)
{
    if (language && !name_from_lcid(language, TRUE)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    g_thread_ui_language = language;
    return GetThreadUILanguage();
}

static int copy_locale_name(PCWSTR value, LPWSTR buffer, int size)
{
    int n = (int)wcslen(value) + 1;
    if (!size)
        return n;
    if (!buffer || size < n) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(buffer, value, (SIZE_T)n * sizeof(WCHAR));
    return n;
}

int WINAPI GetUserDefaultLocaleName(LPWSTR name, int size)
{
    PCWSTR n = name_from_lcid(user_lcid(), FALSE);
    return copy_locale_name(n ? n : L"en-US", name, size);
}

int WINAPI GetSystemDefaultLocaleName(LPWSTR name, int size)
{
    PCWSTR n = name_from_lcid(system_lcid(), FALSE);
    return copy_locale_name(n ? n : L"en-US", name, size);
}

int WINAPI LCIDToLocaleName(LCID locale, LPWSTR name, int size, DWORD flags)
{
    if (size < 0 || (size && !name)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    PCWSTR n = name_from_lcid(locale, (flags & LOCALE_ALLOW_NEUTRAL_NAMES) != 0);
    if (!n) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return copy_locale_name(n, name, size);
}

LCID WINAPI LocaleNameToLCID(LPCWSTR name, DWORD flags)
{
    LCID lcid = lcid_from_name(name, (flags & LOCALE_ALLOW_NEUTRAL_NAMES) != 0);
    if (!lcid)
        KbSetError(ERROR_INVALID_PARAMETER);
    return lcid;
}

int WINAPI ResolveLocaleName(LPCWSTR name, LPWSTR resolved, int size)
{
    LCID lcid = lcid_from_name(name, FALSE);
    PCWSTR n = lcid ? name_from_lcid(lcid, FALSE) : nullptr;
    if (!n && name) {
        /* fall back to the language part of an unknown name. */
        WCHAR language[16];
        SIZE_T i = 0;
        while (name[i] && name[i] != '-' && i < RTL_NUMBER_OF(language) - 1) {
            language[i] = name[i];
            i++;
        }
        language[i] = 0;
        lcid = lcid_from_name(language, FALSE);
        n = lcid ? name_from_lcid(lcid, FALSE) : nullptr;
    }
    if (!n) {
        if (size && resolved)
            resolved[0] = 0;
        return 0;
    }
    return copy_locale_name(n, resolved, size);
}

LCID WINAPI ConvertDefaultLocale(LCID locale)
{
    LCID resolved = KbResolveLcid(locale);
    if (SUBLANGID(resolved) == SUBLANG_NEUTRAL && resolved != LOCALE_INVARIANT) {
        const LOCALE_DATA *l = find_locale(resolved);
        if (l)
            return l->Lcid;
        return MAKELCID(MAKELANGID(PRIMARYLANGID(resolved), SUBLANG_DEFAULT), SORT_DEFAULT);
    }
    return resolved;
}

BOOL WINAPI IsValidLocale(LCID locale, DWORD flags)
{
    return find_locale(locale) != nullptr;
}

BOOL WINAPI IsValidLocaleName(LPCWSTR name)
{
    if (!name)
        return FALSE;
    LCID lcid = lcid_from_name(name, TRUE);
    return lcid && find_locale(lcid);
}

/* the nation SetUserGeoID kept in the international settings, 0 when none was. */
static GEOID stored_nation(void)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\International\\Geo", 0, KEY_QUERY_VALUE, &key))
        return 0;
    WCHAR text[16] = {};
    DWORD size = sizeof(text) - sizeof(WCHAR), type = 0;
    LSTATUS status = RegQueryValueExW(key, L"Nation", nullptr, &type, (LPBYTE)text, &size);
    RegCloseKey(key);
    if (status || type != REG_SZ)
        return 0;
    GEOID id = 0;
    for (const WCHAR *c = text; *c >= '0' && *c <= '9'; c++)
        id = id * 10 + (*c - '0');
    return id;
}

GEOID WINAPI GetUserGeoID(GEOTYPE geo_class)
{
    if (geo_class == GEOCLASS_NATION) {
        GEOID stored = stored_nation();
        if (stored)
            return stored;
        return find_locale(user_lcid()) ? find_locale(user_lcid())->GeoId : 244;
    }
    if (geo_class == GEOCLASS_REGION)
        return 39070;
    return -1;
}

int WINAPI GetUserDefaultGeoName(LPWSTR name, int size)
{
    const LOCALE_DATA *l = find_locale(user_lcid());
    return copy_locale_name(l ? l->Iso3166 : L"US", name, size);
}

/* ------------------------------------------------------------------ locale information */

static constexpr BYTE g_font_signature[32] = {0xAF, 0x00, 0x00, 0xA0, 0x7B, 0x20, 0x00, 0x80, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x9F, 0x01, 0x00, 0x00, 0x00, 0x00,
                                          0xDF, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF};

typedef struct LOCALE_VALUE {
    PCWSTR Text;       /* string value, or NULL when numeric. */
    DWORD Number;      /* numeric value. */
    BOOL Hex;          /* numeric values printed as four hex digits. */
    BOOL Binary;       /* LOCALE_FONTSIGNATURE. */
} LOCALE_VALUE;

/* looks up one item; returns FALSE for unknown types. */
static BOOL locale_value(const LOCALE_DATA *l, LCTYPE type, BOOL genitive, LOCALE_VALUE *v)
{
    memset(v, 0, sizeof(*v));
    if (type >= LOCALE_SDAYNAME1 && type <= LOCALE_SDAYNAME7) {
        v->Text = l->Days[type - LOCALE_SDAYNAME1];
        return TRUE;
    }
    if (type >= LOCALE_SABBREVDAYNAME1 && type <= LOCALE_SABBREVDAYNAME7) {
        v->Text = l->AbbrevDays[type - LOCALE_SABBREVDAYNAME1];
        return TRUE;
    }
    if (type >= LOCALE_SSHORTESTDAYNAME1 && type <= LOCALE_SSHORTESTDAYNAME7) {
        v->Text = l->ShortestDays[type - LOCALE_SSHORTESTDAYNAME1];
        return TRUE;
    }
    if (type >= LOCALE_SMONTHNAME1 && type <= LOCALE_SMONTHNAME12) {
        UINT m = type - LOCALE_SMONTHNAME1;
        v->Text = genitive && l->GenitiveMonths[m] ? l->GenitiveMonths[m] : l->Months[m];
        return TRUE;
    }
    if (type >= LOCALE_SABBREVMONTHNAME1 && type <= LOCALE_SABBREVMONTHNAME12) {
        v->Text = l->AbbrevMonths[type - LOCALE_SABBREVMONTHNAME1];
        return TRUE;
    }
#define TEXT_VALUE(t, value)                                                                                      \
    case t:                                                                                                       \
        v->Text = (value);                                                                                        \
        return TRUE
#define NUMBER_VALUE(t, value)                                                                                    \
    case t:                                                                                                       \
        v->Number = (value);                                                                                      \
        return TRUE
    switch (type) {
    case LOCALE_ILANGUAGE:
    case LOCALE_IDEFAULTLANGUAGE:
        v->Number = LANGIDFROMLCID(l->Lcid);
        v->Hex = TRUE;
        return TRUE;
        TEXT_VALUE(LOCALE_SLANGUAGE, l->DisplayName);
        TEXT_VALUE(LOCALE_SENGLISHDISPLAYNAME, l->DisplayName);
        TEXT_VALUE(LOCALE_SNATIVEDISPLAYNAME, l->NativeDisplayName);
        TEXT_VALUE(LOCALE_SLOCALIZEDLANGUAGENAME, l->EnglishLanguage);
        TEXT_VALUE(LOCALE_SENGLANGUAGE, l->EnglishLanguage);
        TEXT_VALUE(LOCALE_SABBREVLANGNAME, l->AbbrevLanguage);
        TEXT_VALUE(LOCALE_SNATIVELANGNAME, l->NativeLanguage);
        NUMBER_VALUE(LOCALE_ICOUNTRY, l->CountryCode);
        NUMBER_VALUE(LOCALE_IDEFAULTCOUNTRY, l->CountryCode);
        TEXT_VALUE(LOCALE_SCOUNTRY, l->EnglishCountry);
        TEXT_VALUE(LOCALE_SENGCOUNTRY, l->EnglishCountry);
        TEXT_VALUE(LOCALE_SABBREVCTRYNAME, l->AbbrevCountry);
        TEXT_VALUE(LOCALE_SNATIVECTRYNAME, l->NativeCountry);
        NUMBER_VALUE(LOCALE_IDEFAULTCODEPAGE, l->OemCodePage);
        NUMBER_VALUE(LOCALE_IDEFAULTANSICODEPAGE, l->AnsiCodePage);
        NUMBER_VALUE(LOCALE_IDEFAULTMACCODEPAGE, l->MacCodePage);
        NUMBER_VALUE(LOCALE_IDEFAULTEBCDICCODEPAGE, l->EbcdicCodePage);
        TEXT_VALUE(LOCALE_SLIST, l->List);
        NUMBER_VALUE(LOCALE_IMEASURE, l->Measure);
        TEXT_VALUE(LOCALE_SDECIMAL, l->Decimal);
        TEXT_VALUE(LOCALE_STHOUSAND, l->Thousand);
        TEXT_VALUE(LOCALE_SGROUPING, l->Grouping);
        NUMBER_VALUE(LOCALE_IDIGITS, l->Digits);
        NUMBER_VALUE(LOCALE_ILZERO, l->LeadingZero);
        NUMBER_VALUE(LOCALE_INEGNUMBER, l->NegativeNumber);
        TEXT_VALUE(LOCALE_SNATIVEDIGITS, L"0123456789");
        TEXT_VALUE(LOCALE_SCURRENCY, l->Currency);
        TEXT_VALUE(LOCALE_SINTLSYMBOL, l->IntlSymbol);
        TEXT_VALUE(LOCALE_SMONDECIMALSEP, l->MonDecimal);
        TEXT_VALUE(LOCALE_SMONTHOUSANDSEP, l->MonThousand);
        TEXT_VALUE(LOCALE_SMONGROUPING, l->Grouping);
        NUMBER_VALUE(LOCALE_ICURRDIGITS, l->CurrencyDigits);
        NUMBER_VALUE(LOCALE_IINTLCURRDIGITS, l->CurrencyDigits);
        NUMBER_VALUE(LOCALE_ICURRENCY, l->PositiveCurrency);
        NUMBER_VALUE(LOCALE_INEGCURR, l->NegativeCurrency);
        TEXT_VALUE(LOCALE_SDATE, l->DateSeparator);
        TEXT_VALUE(LOCALE_STIME, l->TimeSeparator);
        TEXT_VALUE(LOCALE_SSHORTDATE, l->ShortDate);
        TEXT_VALUE(LOCALE_SLONGDATE, l->LongDate);
        TEXT_VALUE(LOCALE_STIMEFORMAT, l->TimeFormat);
        TEXT_VALUE(LOCALE_SSHORTTIME, l->ShortTime);
        TEXT_VALUE(LOCALE_SYEARMONTH, l->YearMonth);
        TEXT_VALUE(LOCALE_SMONTHDAY, l->MonthDay);
        TEXT_VALUE(LOCALE_SRELATIVELONGDATE, l->MonthDay);
        NUMBER_VALUE(LOCALE_IDATE, l->DateOrder);
        NUMBER_VALUE(LOCALE_ILDATE, l->DateOrder);
        NUMBER_VALUE(LOCALE_ITIME, l->Hour24);
        NUMBER_VALUE(LOCALE_ITIMEMARKPOSN, l->TimeMarkPosition);
        NUMBER_VALUE(LOCALE_ICENTURY, 1);
        NUMBER_VALUE(LOCALE_ITLZERO, l->Hour24);
        NUMBER_VALUE(LOCALE_IDAYLZERO, l->ShortDate[0] == 'd' && l->ShortDate[1] == 'd');
        NUMBER_VALUE(LOCALE_IMONLZERO, wcsstr(l->ShortDate, L"MM") != nullptr);
        TEXT_VALUE(LOCALE_S1159, l->Am);
        TEXT_VALUE(LOCALE_S2359, l->Pm);
        TEXT_VALUE(LOCALE_SSHORTESTAM, l->Am[0] ? L"a" : L"");
        TEXT_VALUE(LOCALE_SSHORTESTPM, l->Pm[0] ? L"p" : L"");
        NUMBER_VALUE(LOCALE_ICALENDARTYPE, CAL_GREGORIAN);
        NUMBER_VALUE(LOCALE_IOPTIONALCALENDAR, 0);
        NUMBER_VALUE(LOCALE_IFIRSTDAYOFWEEK, l->FirstDayOfWeek);
        NUMBER_VALUE(LOCALE_IFIRSTWEEKOFYEAR, l->FirstWeekOfYear);
        TEXT_VALUE(LOCALE_SMONTHNAME13, L"");
        TEXT_VALUE(LOCALE_SABBREVMONTHNAME13, L"");
        TEXT_VALUE(LOCALE_SPOSITIVESIGN, L"");
        TEXT_VALUE(LOCALE_SNEGATIVESIGN, L"-");
        NUMBER_VALUE(LOCALE_IPOSSIGNPOSN, 3);
        NUMBER_VALUE(LOCALE_INEGSIGNPOSN, l->NegativeCurrency == 0 ? 0 : 1);
        NUMBER_VALUE(LOCALE_IPOSSYMPRECEDES, l->PositiveCurrency == 0 || l->PositiveCurrency == 2);
        NUMBER_VALUE(LOCALE_IPOSSEPBYSPACE, l->PositiveCurrency >= 2);
        NUMBER_VALUE(LOCALE_INEGSYMPRECEDES, l->PositiveCurrency == 0 || l->PositiveCurrency == 2);
        NUMBER_VALUE(LOCALE_INEGSEPBYSPACE, l->PositiveCurrency >= 2);
    case LOCALE_FONTSIGNATURE:
        v->Binary = TRUE;
        return TRUE;
        TEXT_VALUE(LOCALE_SISO639LANGNAME, l->Iso639);
        TEXT_VALUE(LOCALE_SISO639LANGNAME2, l->Iso639_2);
        TEXT_VALUE(LOCALE_SISO3166CTRYNAME, l->Iso3166);
        TEXT_VALUE(LOCALE_SISO3166CTRYNAME2, l->Iso3166_2);
        NUMBER_VALUE(LOCALE_IGEOID, l->GeoId);
        TEXT_VALUE(LOCALE_SNAME, l->Name);
        TEXT_VALUE(LOCALE_SDURATION, L"h:mm:ss");
        TEXT_VALUE(LOCALE_SKEYBOARDSTOINSTALL, l->Keyboard);
        NUMBER_VALUE(LOCALE_IPAPERSIZE, l->PaperSize);
        TEXT_VALUE(LOCALE_SENGCURRNAME, l->CurrencyEnglish);
        TEXT_VALUE(LOCALE_SNATIVECURRNAME, l->CurrencyNative);
        TEXT_VALUE(LOCALE_SSORTNAME, L"Default");
        NUMBER_VALUE(LOCALE_IDIGITSUBSTITUTION, 1);
        TEXT_VALUE(LOCALE_SNAN, L"NaN");
        TEXT_VALUE(LOCALE_SPOSINFINITY, L"\x221E");
        TEXT_VALUE(LOCALE_SNEGINFINITY, L"-\x221E");
        TEXT_VALUE(LOCALE_SSCRIPTS, L"Latn;");
        TEXT_VALUE(LOCALE_SPARENT, l->Parent);
        TEXT_VALUE(LOCALE_SCONSOLEFALLBACKNAME, l->Name);
        NUMBER_VALUE(LOCALE_IREADINGLAYOUT, 0);
        NUMBER_VALUE(LOCALE_INEUTRAL, 0);
        NUMBER_VALUE(LOCALE_INEGATIVEPERCENT, l->Lcid == 0x0409 ? 1 : 0);
        NUMBER_VALUE(LOCALE_IPOSITIVEPERCENT, l->Lcid == 0x0409 ? 1 : 0);
        TEXT_VALUE(LOCALE_SPERCENT, L"%");
        TEXT_VALUE(LOCALE_SPERMILLE, L"\x2030");
        TEXT_VALUE(LOCALE_SOPENTYPELANGUAGETAG, L"dflt");
        TEXT_VALUE(LOCALE_SSORTLOCALE, l->Name);
    default:
        return FALSE;
    }
#undef TEXT_VALUE
#undef NUMBER_VALUE
}

/* the neutral locale "en" is reported with its own name and neutral flag. */
static int get_locale_info(const LOCALE_DATA *l, BOOL neutral, LCTYPE lctype, LPWSTR data, int size)
{
    BOOL want_number = (lctype & LOCALE_RETURN_NUMBER) != 0;
    BOOL genitive = (lctype & LOCALE_RETURN_GENITIVE_NAMES) != 0;
    LCTYPE type = lctype & ~(LOCALE_NOUSEROVERRIDE | LOCALE_USE_CP_ACP | LOCALE_RETURN_NUMBER |
                             LOCALE_RETURN_GENITIVE_NAMES | LOCALE_ALLOW_NEUTRAL_NAMES);
    if (size < 0 || (size && !data)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    LOCALE_VALUE v;
    if (!locale_value(l, type, genitive, &v)) {
        KbSetError(ERROR_INVALID_FLAGS);
        return 0;
    }
    if (neutral) {
        if (type == LOCALE_SNAME || type == LOCALE_SCONSOLEFALLBACKNAME)
            v.Text = l->Parent;
        else if (type == LOCALE_INEUTRAL)
            v.Number = 1;
        else if (type == LOCALE_ILANGUAGE)
            v.Number = PRIMARYLANGID(l->Lcid);
    }
    if (v.Binary) {
        if (want_number) {
            KbSetError(ERROR_INVALID_FLAGS);
            return 0;
        }
        int n = sizeof(g_font_signature) / sizeof(WCHAR);
        if (!size)
            return n;
        if (size < n) {
            KbSetError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        memcpy(data, g_font_signature, sizeof(g_font_signature));
        return n;
    }
    if (want_number) {
        if (v.Text) {
            KbSetError(ERROR_INVALID_FLAGS);
            return 0;
        }
        int n = sizeof(DWORD) / sizeof(WCHAR);
        if (!size)
            return n;
        if (size < n) {
            KbSetError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        memcpy(data, &v.Number, sizeof(DWORD));
        return n;
    }
    WCHAR number[16];
    PCWSTR text = v.Text;
    if (!text) {
        KbFormat(number, RTL_NUMBER_OF(number), v.Hex ? L"%04x" : L"%u", v.Number);
        text = number;
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

static BOOL is_neutral_lcid(LCID lcid)
{
    lcid = KbResolveLcid(lcid);
    return SUBLANGID(lcid) == SUBLANG_NEUTRAL && lcid != LOCALE_INVARIANT;
}

int WINAPI GetLocaleInfoW(LCID locale, LCTYPE type, LPWSTR data, int size)
{
    const LOCALE_DATA *l = locale_or_error(locale);
    if (!l)
        return 0;
    return get_locale_info(l, is_neutral_lcid(locale), type, data, size);
}

int WINAPI GetLocaleInfoEx(LPCWSTR name, LCTYPE type, LPWSTR data, int size)
{
    LCID lcid = lcid_from_name(name, TRUE);
    const LOCALE_DATA *l = lcid ? find_locale(lcid) : nullptr;
    if (!l) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return get_locale_info(l, is_neutral_lcid(lcid), type, data, size);
}

int WINAPI GetLocaleInfoA(LCID locale, LCTYPE type, LPSTR data, int size)
{
    const LOCALE_DATA *l = locale_or_error(locale);
    if (!l)
        return 0;
    if (size < 0 || (size && !data)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    LCTYPE base = type & ~(LOCALE_NOUSEROVERRIDE | LOCALE_USE_CP_ACP | LOCALE_RETURN_NUMBER |
                           LOCALE_RETURN_GENITIVE_NAMES);
    if (base == LOCALE_FONTSIGNATURE || (type & LOCALE_RETURN_NUMBER)) {
        WCHAR raw[32];
        int n = get_locale_info(l, is_neutral_lcid(locale), type, raw, RTL_NUMBER_OF(raw));
        if (!n)
            return 0;
        int bytes = n * (int)sizeof(WCHAR);
        if (!size)
            return bytes;
        if (size < bytes) {
            KbSetError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        memcpy(data, raw, (SIZE_T)bytes);
        return bytes;
    }
    WCHAR w[128];
    if (!get_locale_info(l, is_neutral_lcid(locale), type, w, RTL_NUMBER_OF(w)))
        return 0;
    UINT cp = (type & LOCALE_USE_CP_ACP) ? CP_ACP : l->AnsiCodePage;
    int need = WideCharToMultiByte(cp, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (!size)
        return need;
    if (size < need) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    return WideCharToMultiByte(cp, 0, w, -1, data, size, nullptr, nullptr);
}

BOOL WINAPI SetLocaleInfoW(LCID locale, LCTYPE type, LPCWSTR data)
{
    KbSetError(ERROR_ACCESS_DENIED);
    return FALSE;
}

BOOL WINAPI SetLocaleInfoA(LCID locale, LCTYPE type, LPCSTR data)
{
    KbSetError(ERROR_ACCESS_DENIED);
    return FALSE;
}

/* ------------------------------------------------------------------ calendar */

static int calendar_info(const LOCALE_DATA *l, CALID calendar, CALTYPE type, LPWSTR data, int size, LPDWORD value)
{
    BOOL want_number = (type & CAL_RETURN_NUMBER) != 0;
    CALTYPE base = type & ~(CAL_RETURN_NUMBER | CAL_NOUSEROVERRIDE | CAL_USE_CP_ACP | LOCALE_RETURN_GENITIVE_NAMES);
    if (calendar != CAL_GREGORIAN && calendar != CAL_GREGORIAN_US && calendar != ENUM_ALL_CALENDARS) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    PCWSTR text = nullptr;
    DWORD number = 0;
    BOOL numeric = FALSE;
    if (base >= CAL_SDAYNAME1 && base < CAL_SDAYNAME1 + 7)
        text = l->Days[base - CAL_SDAYNAME1];
    else if (base >= CAL_SABBREVDAYNAME1 && base < CAL_SABBREVDAYNAME1 + 7)
        text = l->AbbrevDays[base - CAL_SABBREVDAYNAME1];
    else if (base >= CAL_SMONTHNAME1 && base < CAL_SMONTHNAME1 + 12)
        text = l->Months[base - CAL_SMONTHNAME1];
    else if (base >= CAL_SABBREVMONTHNAME1 && base < CAL_SABBREVMONTHNAME1 + 12)
        text = l->AbbrevMonths[base - CAL_SABBREVMONTHNAME1];
    else if (base >= CAL_SSHORTESTDAYNAME1 && base < CAL_SSHORTESTDAYNAME1 + 7)
        text = l->ShortestDays[base - CAL_SSHORTESTDAYNAME1];
    else {
        switch (base) {
        case CAL_ICALINTVALUE:
            numeric = TRUE;
            number = calendar == CAL_GREGORIAN_US ? 2 : 1;
            break;
        case CAL_SCALNAME:
            text = L"Gregorian Calendar";
            break;
        case CAL_IYEAROFFSETRANGE:
            numeric = TRUE;
            break;
        case CAL_SERASTRING:
            text = L"A.D.";
            break;
        case CAL_SABBREVERASTRING:
            text = L"AD";
            break;
        case CAL_SSHORTDATE:
            text = l->ShortDate;
            break;
        case CAL_SLONGDATE:
            text = l->LongDate;
            break;
        case CAL_SYEARMONTH:
            text = l->YearMonth;
            break;
        case CAL_SMONTHDAY:
            text = l->MonthDay;
            break;
        case CAL_SMONTHNAME13:
        case CAL_SABBREVMONTHNAME13:
            text = L"";
            break;
        case CAL_ITWODIGITYEARMAX:
            numeric = TRUE;
            number = 2049;
            break;
        default:
            KbSetError(ERROR_INVALID_FLAGS);
            return 0;
        }
    }
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
    if (!data || size < n) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(data, text, (SIZE_T)n * sizeof(WCHAR));
    return n;
}

int WINAPI GetCalendarInfoW(LCID locale, CALID calendar, CALTYPE type, LPWSTR data, int size, LPDWORD value)
{
    const LOCALE_DATA *l = locale_or_error(locale);
    return l ? calendar_info(l, calendar, type, data, size, value) : 0;
}

int WINAPI GetCalendarInfoEx(LPCWSTR name, CALID calendar, LPCWSTR reserved, CALTYPE type, LPWSTR data, int size,
                             LPDWORD value)
{
    LCID lcid = lcid_from_name(name, TRUE);
    const LOCALE_DATA *l = lcid ? find_locale(lcid) : nullptr;
    if (!l) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return calendar_info(l, calendar, type, data, size, value);
}

/* ------------------------------------------------------------------ date and time formatting */

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

/* copies a quoted literal; returns the characters consumed. */
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

static int format_date(const LOCALE_DATA *l, const SYSTEMTIME *st, PCWSTR format, LPWSTR buffer, int size, BOOL genitive_ok)
{
    TEXT_OUT o = {buffer, size, 0};
    for (PCWSTR p = format; *p;) {
        WCHAR c = *p;
        if (c == '\'') {
            if (p[1] == '\'') {
                put(&o, '\'');
                p += 2;
            } else {
                p += put_quoted(&o, p);
            }
            continue;
        }
        if (c != 'd' && c != 'M' && c != 'y' && c != 'g') {
            put(&o, c);
            p++;
            continue;
        }
        SIZE_T n = run_length(p);
        switch (c) {
        case 'd':
            if (n == 1)
                put_number(&o, st->wDay, 1);
            else if (n == 2)
                put_number(&o, st->wDay, 2);
            else if (n == 3)
                put_text(&o, l->AbbrevDays[(st->wDayOfWeek + 6) % 7]);
            else
                put_text(&o, l->Days[(st->wDayOfWeek + 6) % 7]);
            break;
        case 'M':
            if (n == 1)
                put_number(&o, st->wMonth, 1);
            else if (n == 2)
                put_number(&o, st->wMonth, 2);
            else if (n == 3)
                put_text(&o, l->AbbrevMonths[st->wMonth - 1]);
            else {
                /* the genitive form is used when a day number precedes the month. */
                BOOL day_before = FALSE;
                for (PCWSTR q = format; q < p; q++)
                    if (*q == 'd' && (q + 1 == p || q[1] != 'd' || q[2] != 'd'))
                        day_before = TRUE;
                PCWSTR name = genitive_ok && day_before && l->GenitiveMonths[st->wMonth - 1]
                                  ? l->GenitiveMonths[st->wMonth - 1]
                                  : l->Months[st->wMonth - 1];
                put_text(&o, name);
            }
            break;
        case 'y':
            if (n <= 2)
                put_number(&o, st->wYear % 100, n == 1 ? 1 : 2);
            else
                put_number(&o, st->wYear, n == 3 ? 1 : 4);
            break;
        case 'g':
            put_text(&o, L"A.D.");
            break;
        }
        p += n;
    }
    return finish_text(&o);
}

static BOOL resolve_date(const SYSTEMTIME *date, SYSTEMTIME *out)
{
    if (!date) {
        GetLocalTime(out);
        return TRUE;
    }
    LONGLONG t;
    if (!KbSystemTimeToFileTime(&(SYSTEMTIME){date->wYear, date->wMonth, 0, date->wDay, 0, 0, 0, 0}, &t)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    KbFileTimeToSystemTime(t, out);
    out->wHour = date->wHour;
    out->wMinute = date->wMinute;
    out->wSecond = date->wSecond;
    out->wMilliseconds = date->wMilliseconds;
    return TRUE;
}

static int date_format(const LOCALE_DATA *l, DWORD flags, const SYSTEMTIME *date, LPCWSTR format, LPWSTR buffer,
                       int size)
{
    if (size < 0 || (size && !buffer)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    DWORD kinds = flags & (DATE_SHORTDATE | DATE_LONGDATE | DATE_YEARMONTH | DATE_MONTHDAY);
    if ((format && kinds) || (kinds & (kinds - 1)) ||
        (flags & ~(DWORD)(DATE_SHORTDATE | DATE_LONGDATE | DATE_YEARMONTH | DATE_MONTHDAY | DATE_USE_ALT_CALENDAR |
                          DATE_LTRREADING | DATE_RTLREADING | DATE_AUTOLAYOUT | LOCALE_NOUSEROVERRIDE |
                          LOCALE_USE_CP_ACP))) {
        KbSetError(ERROR_INVALID_FLAGS);
        return 0;
    }
    SYSTEMTIME st;
    if (!resolve_date(date, &st))
        return 0;
    if (!format) {
        if (flags & DATE_LONGDATE)
            format = l->LongDate;
        else if (flags & DATE_YEARMONTH)
            format = l->YearMonth;
        else if (flags & DATE_MONTHDAY)
            format = l->MonthDay;
        else
            format = l->ShortDate;
    }
    return format_date(l, &st, format, buffer, size, TRUE);
}

int WINAPI GetDateFormatW(LCID locale, DWORD flags, const SYSTEMTIME *date, LPCWSTR format, LPWSTR buffer, int size)
{
    const LOCALE_DATA *l = locale_or_error(locale);
    return l ? date_format(l, flags, date, format, buffer, size) : 0;
}

int WINAPI GetDateFormatEx(LPCWSTR name, DWORD flags, const SYSTEMTIME *date, LPCWSTR format, LPWSTR buffer, int size,
                           LPCWSTR calendar)
{
    LCID lcid = lcid_from_name(name, TRUE);
    const LOCALE_DATA *l = lcid ? find_locale(lcid) : nullptr;
    if (!l) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return date_format(l, flags, date, format, buffer, size);
}

static int format_time(const LOCALE_DATA *l, DWORD flags, const SYSTEMTIME *st, PCWSTR format, LPWSTR buffer, int size)
{
    TEXT_OUT o = {buffer, size, 0};
    BOOL force24 = (flags & TIME_FORCE24HOURFORMAT) != 0;
    BOOL skip_minutes = (flags & TIME_NOMINUTESORSECONDS) != 0;
    BOOL skip_seconds = skip_minutes || (flags & TIME_NOSECONDS);
    BOOL skip_marker = (flags & TIME_NOTIMEMARKER) != 0;
    int separator_start = -1; /* where literal text after the last emitted field begins. */
    BOOL emitted = FALSE;
    BOOL drop_literals = FALSE; /* set when a leading field was skipped. */
    for (PCWSTR p = format; *p;) {
        WCHAR c = *p;
        if (c != 'h' && c != 'H' && c != 'm' && c != 's' && c != 't') {
            int mark = o.Length;
            if (c == '\'') {
                if (p[1] == '\'') {
                    put(&o, '\'');
                    p += 2;
                } else {
                    p += put_quoted(&o, p);
                }
            } else {
                put(&o, c);
                p++;
            }
            if (drop_literals)
                o.Length = mark;
            else if (separator_start < 0)
                separator_start = mark;
            continue;
        }
        SIZE_T n = run_length(p);
        BOOL skip = (c == 'm' && skip_minutes) || (c == 's' && skip_seconds) || (c == 't' && (skip_marker || force24));
        if (skip) {
            /* a skipped field takes the separator before it along, or the one after it when it leads. */
            if (emitted && separator_start >= 0)
                o.Length = separator_start;
            else if (!emitted)
                drop_literals = TRUE;
            separator_start = -1;
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
        case 't': {
            PCWSTR marker = st->wHour < 12 ? l->Am : l->Pm;
            if (n == 1) {
                if (marker[0])
                    put(&o, marker[0]);
            } else {
                put_text(&o, marker);
            }
            break;
        }
        }
        separator_start = -1;
        emitted = TRUE;
        drop_literals = FALSE;
        p += n;
    }
    return finish_text(&o);
}

static int time_format(const LOCALE_DATA *l, DWORD flags, const SYSTEMTIME *time, LPCWSTR format, LPWSTR buffer,
                       int size)
{
    if (size < 0 || (size && !buffer)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (flags & ~(DWORD)(TIME_NOMINUTESORSECONDS | TIME_NOSECONDS | TIME_NOTIMEMARKER | TIME_FORCE24HOURFORMAT |
                         LOCALE_NOUSEROVERRIDE | LOCALE_USE_CP_ACP)) {
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
    return format_time(l, flags, &st, format ? format : l->TimeFormat, buffer, size);
}

int WINAPI GetTimeFormatW(LCID locale, DWORD flags, const SYSTEMTIME *time, LPCWSTR format, LPWSTR buffer, int size)
{
    const LOCALE_DATA *l = locale_or_error(locale);
    return l ? time_format(l, flags, time, format, buffer, size) : 0;
}

int WINAPI GetTimeFormatEx(LPCWSTR name, DWORD flags, const SYSTEMTIME *time, LPCWSTR format, LPWSTR buffer, int size)
{
    LCID lcid = lcid_from_name(name, TRUE);
    const LOCALE_DATA *l = lcid ? find_locale(lcid) : nullptr;
    if (!l) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return time_format(l, flags, time, format, buffer, size);
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
    const LOCALE_DATA *l = locale_or_error(locale);
    if (!l)
        return 0;
    if (size < 0 || (size && !buffer)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UINT cp = (flags & LOCALE_USE_CP_ACP) ? CP_ACP : l->AnsiCodePage;
    PWSTR wformat = KbAnsiToWideCp(cp, format);
    if (format && !wformat)
        return 0;
    WCHAR w[256];
    int r = date_format(l, flags, date, wformat, w, RTL_NUMBER_OF(w));
    KbFree(wformat);
    return narrow_result(cp, r, w, buffer, size);
}

int WINAPI GetTimeFormatA(LCID locale, DWORD flags, const SYSTEMTIME *time, LPCSTR format, LPSTR buffer, int size)
{
    const LOCALE_DATA *l = locale_or_error(locale);
    if (!l)
        return 0;
    if (size < 0 || (size && !buffer)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UINT cp = (flags & LOCALE_USE_CP_ACP) ? CP_ACP : l->AnsiCodePage;
    PWSTR wformat = KbAnsiToWideCp(cp, format);
    if (format && !wformat)
        return 0;
    WCHAR w[256];
    int r = time_format(l, flags, time, wformat, w, RTL_NUMBER_OF(w));
    KbFree(wformat);
    return narrow_result(cp, r, w, buffer, size);
}

BOOL WINAPI EnumDateFormatsW(DATEFMT_ENUMPROCW proc, LCID locale, DWORD flags)
{
    const LOCALE_DATA *l = locale_or_error(locale);
    if (!l || !proc) {
        if (!proc)
            KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PCWSTR format = (flags & DATE_LONGDATE) ? l->LongDate : (flags & DATE_YEARMONTH) ? l->YearMonth : l->ShortDate;
    proc((LPWSTR)format);
    return TRUE;
}

BOOL WINAPI EnumTimeFormatsW(TIMEFMT_ENUMPROCW proc, LCID locale, DWORD flags)
{
    const LOCALE_DATA *l = locale_or_error(locale);
    if (!l || !proc) {
        if (!proc)
            KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    proc((LPWSTR)((flags & TIME_NOSECONDS) ? l->ShortTime : l->TimeFormat));
    return TRUE;
}

BOOL WINAPI EnumCalendarInfoW(CALINFO_ENUMPROCW proc, LCID locale, CALID calendar, CALTYPE type)
{
    WCHAR buffer[128];
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!GetCalendarInfoW(locale, calendar == ENUM_ALL_CALENDARS ? CAL_GREGORIAN : calendar, type, buffer,
                          RTL_NUMBER_OF(buffer), nullptr))
        return FALSE;
    proc(buffer);
    return TRUE;
}

/* ------------------------------------------------------------------ number and currency formatting */

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
    if (*p || (!ilen && !flen))
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

/* grouping such as 3 or 32: digits per group from the right, the last one repeating. */
static void put_grouped(TEXT_OUT *o, PCWSTR integer, UINT grouping, PCWSTR separator)
{
    UINT groups[8];
    UINT count = 0;
    BOOL repeat = TRUE;
    if (grouping) {
        WCHAR spec[16];
        KbFormat(spec, RTL_NUMBER_OF(spec), L"%u", grouping);
        SIZE_T len = wcslen(spec);
        if (len > 1 && spec[len - 1] == '0') {
            repeat = FALSE;
            len--;
        }
        for (SIZE_T i = 0; i < len && count < RTL_NUMBER_OF(groups); i++)
            groups[count++] = (UINT)(spec[i] - '0');
    }
    SIZE_T n = wcslen(integer);
    /* mark the positions, counted from the right, after which a separator goes. */
    BOOL separate[128] = {};
    SIZE_T pos = 0;
    for (UINT g = 0; count && g < 64; g++) {
        UINT size = g < count ? groups[g] : (repeat ? groups[count - 1] : 0);
        if (!size)
            break;
        pos += size;
        if (pos >= n)
            break;
        separate[pos] = TRUE;
    }
    for (SIZE_T i = 0; i < n; i++) {
        put(o, integer[i]);
        SIZE_T from_right = n - 1 - i;
        if (from_right && from_right < RTL_NUMBER_OF(separate) && separate[from_right])
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
    if (wcscmp(n->Integer, L"0") || leading_zero)
        put_grouped(o, n->Integer, grouping, thousand);
    if (digits) {
        put_text(o, decimal);
        put_text(o, n->Fraction);
    }
}

static int number_format(const LOCALE_DATA *l, DWORD flags, LPCWSTR value, const NUMBERFMTW *format, LPWSTR buffer,
                         int size)
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
        if (!format->lpDecimalSep || !format->lpThousandSep || format->NegativeOrder > 4) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        f = *format;
    } else {
        f.NumDigits = l->Digits;
        f.LeadingZero = l->LeadingZero;
        f.Grouping = grouping_value(l->Grouping);
        f.lpDecimalSep = (LPWSTR)l->Decimal;
        f.lpThousandSep = (LPWSTR)l->Thousand;
        f.NegativeOrder = l->NegativeNumber;
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
            put(&o, '-');
            break;
        case 2:
            put_text(&o, L"- ");
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
            put(&o, '-');
            break;
        case 4:
            put_text(&o, L" -");
            break;
        }
    }
    return finish_text(&o);
}

int WINAPI GetNumberFormatW(LCID locale, DWORD flags, LPCWSTR value, const NUMBERFMTW *format, LPWSTR buffer, int size)
{
    const LOCALE_DATA *l = locale_or_error(locale);
    return l ? number_format(l, flags, value, format, buffer, size) : 0;
}

int WINAPI GetNumberFormatEx(LPCWSTR name, DWORD flags, LPCWSTR value, const NUMBERFMTW *format, LPWSTR buffer, int size)
{
    LCID lcid = lcid_from_name(name, TRUE);
    const LOCALE_DATA *l = lcid ? find_locale(lcid) : nullptr;
    if (!l) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return number_format(l, flags, value, format, buffer, size);
}

int WINAPI GetNumberFormatA(LCID locale, DWORD flags, LPCSTR value, const NUMBERFMTA *format, LPSTR buffer, int size)
{
    const LOCALE_DATA *l = locale_or_error(locale);
    if (!l)
        return 0;
    if (!value || size < 0 || (size && !buffer)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UINT cp = (flags & LOCALE_USE_CP_ACP) ? CP_ACP : l->AnsiCodePage;
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

static int currency_format(const LOCALE_DATA *l, DWORD flags, LPCWSTR value, const CURRENCYFMTW *format, LPWSTR buffer,
                           int size)
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
            format->PositiveOrder > 3) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        f = *format;
    } else {
        f.NumDigits = l->CurrencyDigits;
        f.LeadingZero = l->LeadingZero;
        f.Grouping = grouping_value(l->Grouping);
        f.lpDecimalSep = (LPWSTR)l->MonDecimal;
        f.lpThousandSep = (LPWSTR)l->MonThousand;
        f.NegativeOrder = l->NegativeCurrency;
        f.PositiveOrder = l->PositiveCurrency;
        f.lpCurrencySymbol = (LPWSTR)l->Currency;
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
        else
            put(&o, *p);
    }
    return finish_text(&o);
}

int WINAPI GetCurrencyFormatW(LCID locale, DWORD flags, LPCWSTR value, const CURRENCYFMTW *format, LPWSTR buffer,
                              int size)
{
    const LOCALE_DATA *l = locale_or_error(locale);
    return l ? currency_format(l, flags, value, format, buffer, size) : 0;
}

int WINAPI GetCurrencyFormatEx(LPCWSTR name, DWORD flags, LPCWSTR value, const CURRENCYFMTW *format, LPWSTR buffer,
                               int size)
{
    LCID lcid = lcid_from_name(name, TRUE);
    const LOCALE_DATA *l = lcid ? find_locale(lcid) : nullptr;
    if (!l) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return currency_format(l, flags, value, format, buffer, size);
}

/* ------------------------------------------------------------------ enumeration */

BOOL WINAPI EnumSystemLocalesW(LOCALE_ENUMPROCW proc, DWORD flags)
{
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_locales); i++) {
        if (g_locales[i]->Lcid == LOCALE_INVARIANT)
            continue;
        WCHAR buf[16];
        KbFormat(buf, RTL_NUMBER_OF(buf), L"%08x", g_locales[i]->Lcid);
        if (!proc(buf))
            break;
    }
    return TRUE;
}

BOOL WINAPI EnumSystemLocalesA(LOCALE_ENUMPROCA proc, DWORD flags)
{
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_locales); i++) {
        if (g_locales[i]->Lcid == LOCALE_INVARIANT)
            continue;
        char buf[16];
        KbFormatA(buf, sizeof(buf), "%08x", g_locales[i]->Lcid);
        if (!proc(buf))
            break;
    }
    return TRUE;
}

BOOL WINAPI EnumSystemLocalesEx(LOCALE_ENUMPROCEX proc, DWORD flags, LPARAM param, LPVOID reserved)
{
    if (!proc || reserved) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL want_neutral = !(flags & ~(DWORD)LOCALE_REPLACEMENT) || (flags & LOCALE_NEUTRALDATA);
    BOOL want_specific = !(flags & ~(DWORD)LOCALE_REPLACEMENT) || (flags & (LOCALE_SPECIFICDATA | LOCALE_WINDOWS));
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_locales); i++) {
        const LOCALE_DATA *l = g_locales[i];
        if (l->Lcid == LOCALE_INVARIANT)
            continue;
        if (want_neutral && (i == 0 || wcscmp(g_locales[i - 1]->Parent, l->Parent))) {
            if (!proc((LPWSTR)l->Parent, LOCALE_WINDOWS | LOCALE_NEUTRALDATA, param))
                return TRUE;
        }
        if (want_specific && !proc((LPWSTR)l->Name, LOCALE_WINDOWS | LOCALE_SPECIFICDATA, param))
            return TRUE;
    }
    return TRUE;
}

BOOL WINAPI EnumUILanguagesW(UILANGUAGE_ENUMPROCW proc, DWORD flags, LONG_PTR param)
{
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    proc((flags & MUI_LANGUAGE_NAME) ? L"en-US" : L"0409", param);
    return TRUE;
}

static BOOL ui_language_list(DWORD flags, PULONG count, PZZWSTR buffer, PULONG size)
{
    if (!count || !size || (flags & MUI_LANGUAGE_ID && flags & MUI_LANGUAGE_NAME)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PCWSTR value = (flags & MUI_LANGUAGE_ID) ? L"0409" : L"en-US";
    ULONG need = (ULONG)wcslen(value) + 2;
    *count = 1;
    if (!buffer) {
        *size = need;
        return TRUE;
    }
    if (*size < need) {
        *size = need;
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(buffer, value, (need - 1) * sizeof(WCHAR));
    buffer[need - 1] = 0;
    *size = need;
    return TRUE;
}

BOOL WINAPI GetUserPreferredUILanguages(DWORD flags, PULONG count, PZZWSTR buffer, PULONG size)
{
    return ui_language_list(flags, count, buffer, size);
}

BOOL WINAPI GetSystemPreferredUILanguages(DWORD flags, PULONG count, PZZWSTR buffer, PULONG size)
{
    return ui_language_list(flags, count, buffer, size);
}

BOOL WINAPI GetThreadPreferredUILanguages(DWORD flags, PULONG count, PZZWSTR buffer, PULONG size)
{
    return ui_language_list(flags & (MUI_LANGUAGE_ID | MUI_LANGUAGE_NAME), count, buffer, size);
}

BOOL WINAPI GetProcessPreferredUILanguages(DWORD flags, PULONG count, PZZWSTR buffer, PULONG size)
{
    return ui_language_list(flags, count, buffer, size);
}

BOOL WINAPI SetThreadPreferredUILanguages(DWORD flags, PCZZWSTR languages, PULONG count)
{
    if (count)
        *count = 1;
    return TRUE;
}

BOOL WINAPI SetProcessPreferredUILanguages(DWORD flags, PCZZWSTR languages, PULONG count)
{
    if (count)
        *count = 1;
    return TRUE;
}

BOOL WINAPI GetFileMUIPath(DWORD flags, PCWSTR file, PWSTR language, PULONG language_size, PWSTR path,
                           PULONG path_size, PULONGLONG enumerator)
{
    KbSetError(ERROR_FILE_NOT_FOUND);
    return FALSE;
}

/* ------------------------------------------------------------------ language groups */

/*
 * language groups 1 to 17 are valid. Only the first has locales here, all of those mime has data for, and it is
 * both installed and supported.
 */
BOOL WINAPI IsValidLanguageGroup(LGRPID group, DWORD flags)
{
    return !(flags & ~(DWORD)(LGRPID_INSTALLED | LGRPID_SUPPORTED)) &&
           (flags & (LGRPID_INSTALLED | LGRPID_SUPPORTED)) != (LGRPID_INSTALLED | LGRPID_SUPPORTED) && group - 1 <= 0x10;
}

BOOL WINAPI EnumSystemLanguageGroupsW(LANGUAGEGROUP_ENUMPROCW proc, DWORD flags, LONG_PTR param)
{
    if (!proc || (flags & ~(DWORD)(LGRPID_INSTALLED | LGRPID_SUPPORTED)) ||
        (flags & (LGRPID_INSTALLED | LGRPID_SUPPORTED)) == (LGRPID_INSTALLED | LGRPID_SUPPORTED)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR id[16];
    KbFormat(id, RTL_NUMBER_OF(id), L"%08x", LGRPID_WESTERN_EUROPE);
    WCHAR name[] = L"Western Europe and United States";
    proc(LGRPID_WESTERN_EUROPE, id, name, LGRPID_INSTALLED, param);
    return TRUE;
}

BOOL WINAPI EnumLanguageGroupLocalesW(LANGGROUPLOCALE_ENUMPROCW proc, LGRPID group, DWORD flags, LONG_PTR param)
{
    if (!proc || flags || group - 1 > 0x10) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (group != LGRPID_WESTERN_EUROPE)
        return TRUE;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_locales); i++) {
        if (g_locales[i]->Lcid == LOCALE_INVARIANT)
            continue;
        WCHAR text[16];
        KbFormat(text, RTL_NUMBER_OF(text), L"%08x", g_locales[i]->Lcid);
        if (!proc(group, g_locales[i]->Lcid, text, param))
            break;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ script lists */

static BOOL script_list_valid(PCWSTR s, int n)
{
    for (int i = 0; i + 5 <= n; i += 5) {
        if (s[i] < 'A' || s[i] > 'Z' || s[i + 1] < 'a' || s[i + 1] > 'z' || s[i + 2] < 'a' || s[i + 2] > 'z' ||
            s[i + 3] < 'a' || s[i + 3] > 'z' || s[i + 4] != ';')
            return FALSE;
    }
    return n % 5 == 0;
}

/*
 * whether the scripts of a text are all among those of a locale. Both are lists of four letter names, each followed
 * by a semicolon. With the flag 1 the common script Latn is let through. FALSE with no error is a script that is not
 * in the locale's list, with the error 87 for lists that are not in the form.
 */
BOOL WINAPI VerifyScripts(DWORD flags, LPCWSTR locale_scripts, int locale_length, LPCWSTR test_scripts, int test_length)
{
    if (flags > 1) {
        KbSetError(ERROR_INVALID_FLAGS);
        return FALSE;
    }
    if (!locale_scripts || locale_length < -1 || !test_scripts || test_length < -1) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (locale_length == -1)
        locale_length = (int)wcslen(locale_scripts);
    if (test_length == -1)
        test_length = (int)wcslen(test_scripts);
    if (locale_length > 0 && !locale_scripts[locale_length - 1])
        locale_length--;
    if (test_length > 0 && !test_scripts[test_length - 1])
        test_length--;
    if (!script_list_valid(locale_scripts, locale_length) || !script_list_valid(test_scripts, test_length)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (int i = 0; i < test_length; i += 5) {
        if ((flags & 1) && !memcmp(test_scripts + i, L"Latn;", 5 * sizeof(WCHAR)))
            continue;
        BOOL found = FALSE;
        for (int j = 0; j < locale_length && !found; j += 5)
            found = !memcmp(test_scripts + i, locale_scripts + j, 5 * sizeof(WCHAR));
        if (!found) {
            KbSetError(ERROR_SUCCESS);
            return FALSE;
        }
    }
    return TRUE;
}
