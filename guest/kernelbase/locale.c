/*
 * locales: identifiers, names, locale information and the default locales. The tables are built from Unicode CLDR and
 * Windows constants.
 */
#include "kernelbase_private.h"
#include "nls_locslots.h"
#include "nls_locdata.h"

#define NO_LOCALE 0xFFFFFFFFu

/* ------------------------------------------------------------------ the tables */

static PCWSTR pool_text(UINT offset)
{
    return (PCWSTR)&g_nls_pool[offset];
}

PCWSTR KbLocaleText(UINT locale, UINT slot)
{
    return pool_text(g_nls_strings[locale * NLS_STRING_ITEMS + slot]);
}

INT KbLocaleNumber(UINT locale, UINT slot)
{
    return g_nls_numbers[locale * NLS_NUMBER_ITEMS + slot];
}

PCWSTR KbLocaleName(UINT locale)
{
    return pool_text(g_nls_heads[locale].Name);
}

static BOOL is_neutral(UINT locale)
{
    return g_nls_heads[locale].Neutral != 0;
}

BOOL KbLocaleIsNeutral(UINT locale)
{
    return is_neutral(locale);
}

/* the specific locale a neutral one stands for. */
UINT KbLocaleSpecific(UINT locale)
{
    return g_nls_heads[locale].Default;
}

/* ------------------------------------------------------------------ names and identifiers */

/* names are almost all ascii, so only ascii letters are folded. */
static int compare_names(PCWSTR a, PCWSTR b)
{
    for (;; a++, b++) {
        WCHAR x = *a, y = *b;
        if (x >= 'a' && x <= 'z')
            x -= 32;
        if (y >= 'a' && y <= 'z')
            y -= 32;
        if (x != y)
            return x < y ? -1 : 1;
        if (!x)
            return 0;
    }
}

static UINT find_name(PCWSTR name)
{
    LONG lo = 0, hi = (LONG)RTL_NUMBER_OF(g_nls_by_name) - 1;
    while (lo <= hi) {
        LONG mid = (lo + hi) / 2;
        UINT locale = g_nls_by_name[mid];
        int c = compare_names(name, KbLocaleName(locale));
        if (!c)
            return locale;
        if (c < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return NO_LOCALE;
}

static UINT find_lcid(LCID lcid)
{
    LONG lo = 0, hi = (LONG)RTL_NUMBER_OF(g_nls_lcids) - 1;
    while (lo <= hi) {
        LONG mid = (lo + hi) / 2;
        if (g_nls_lcids[mid] == lcid)
            return g_nls_lcid_locale[mid];
        if (g_nls_lcids[mid] < lcid)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NO_LOCALE;
}

static UINT find_neutral_lcid(LCID lcid)
{
    LONG lo = 0, hi = (LONG)RTL_NUMBER_OF(g_nls_neutral_lcids) - 1;
    while (lo <= hi) {
        LONG mid = (lo + hi) / 2;
        if (g_nls_neutral_lcids[mid] == lcid)
            return g_nls_neutral_locale[mid];
        if (g_nls_neutral_lcids[mid] < lcid)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NO_LOCALE;
}

/* alternate sorts: the name has an underscore, the identifier has the sort in its high word. */
typedef NLS_ALTERNATE ALTERNATE_SORT;
#define g_alternate_sorts g_nls_alternates

/* names Windows still knows for identifiers that have no locale data. */
static const ALTERNATE_SORT g_legacy_names[] = {
    {0x0000081A, L"sr-Latn-CS"}, {0x00000C1A, L"sr-Cyrl-CS"}, {0x00000501, L"qps-ploc"},
    {0x000005FE, L"qps-ploca"},  {0x00000901, L"qps-Latn-x-sh"}, {0x000009FF, L"qps-plocm"},
};

static LCID g_user_lcid;
static LCID g_system_lcid;
static LANGID g_ui_language;
static LANGID g_thread_ui_language;

static UINT g_en_us = NO_LOCALE;

static UINT english(void)
{
    if (g_en_us == NO_LOCALE)
        g_en_us = find_name(L"en-US");
    return g_en_us;
}

static LCID user_lcid(void)
{
    if (!g_user_lcid) {
        LCID l = 0x0409;
        NtQueryDefaultLocale(TRUE, &l);
        if (find_lcid(l) == NO_LOCALE)
            l = 0x0409;
        g_user_lcid = l;
    }
    return g_user_lcid;
}

static LCID system_lcid(void)
{
    if (!g_system_lcid) {
        LCID l = 0x0409;
        NtQueryDefaultLocale(FALSE, &l);
        if (find_lcid(l) == NO_LOCALE)
            l = 0x0409;
        g_system_lcid = l;
    }
    return g_system_lcid;
}

/* maps the identifiers that stand for a default to a real locale. */
LCID KbResolveLcid(LCID lcid)
{
    LANGID lang = LANGIDFROMLCID(lcid);
    switch (lang) {
    case LANG_USER_DEFAULT:
    case 0x0000:
    case 0x0C00: /* LOCALE_CUSTOM_DEFAULT. */
    case 0x1400: /* LOCALE_CUSTOM_UI_DEFAULT. */
        return user_lcid();
    case LANG_SYSTEM_DEFAULT:
        return system_lcid();
    default:
        return lcid;
    }
}

/*
 * only the language part picks the locale, the sort order in the high word is ignored. A neutral identifier gives a
 * neutral locale only when allow_neutral is set.
 */
UINT KbLocaleFromLcid(LCID lcid, BOOL allow_neutral)
{
    lcid = KbResolveLcid(lcid);
    LCID lang = lcid & 0xFFFF;
    UINT locale = find_lcid(lang);
    if (locale != NO_LOCALE)
        return locale;
    locale = find_neutral_lcid(lang);
    if (locale == NO_LOCALE) {
        /* a bare neutral language means its default region. */
        LCID primary = lang & 0x3FF;
        if (lang == primary) {
            for (ULONG i = 0; i < RTL_NUMBER_OF(g_nls_neutral_lcids); i++)
                if (g_nls_neutral_lcids[i] == primary) {
                    locale = g_nls_neutral_locale[i];
                    break;
                }
        }
    }
    if (locale == NO_LOCALE)
        return NO_LOCALE;
    return allow_neutral ? locale : KbLocaleSpecific(locale);
}

BOOL KbIsSupportedLcid(LCID lcid)
{
    return KbLocaleFromLcid(lcid, TRUE) != NO_LOCALE;
}

/* kind is 0 for ansi, 1 for oem, 2 for mac. */
UINT KbLocaleCodePage(LCID lcid, UINT kind)
{
    UINT locale = KbLocaleFromLcid(lcid, FALSE);
    if (locale == NO_LOCALE)
        locale = english();
    INT cp = KbLocaleNumber(locale, kind == 0 ? NLS_N_IDEFAULTANSICODEPAGE : kind == 1 ? NLS_N_IDEFAULTCODEPAGE : NLS_N_IDEFAULTMACCODEPAGE);
    /* locales with no code page of their own, like the Indian ones, use utf-8. */
    return cp > 1 ? (UINT)cp : CP_UTF8;
}

/* the empty name is the invariant locale, NULL the user's. */
UINT KbLocaleFromName(LPCWSTR name)
{
    if (!name)
        return KbLocaleFromLcid(user_lcid(), TRUE);
    if (!_wcsicmp(name, LOCALE_NAME_SYSTEM_DEFAULT))
        return KbLocaleFromLcid(system_lcid(), TRUE);
    return find_name(name);
}

static LCID locale_lcid(UINT locale, BOOL allow_neutral)
{
    if (is_neutral(locale) && !allow_neutral)
        locale = KbLocaleSpecific(locale);
    return is_neutral(locale) ? g_nls_heads[locale].NeutralLcid : g_nls_heads[locale].Lcid;
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
    if (KbLocaleFromLcid(resolved, TRUE) == NO_LOCALE) {
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
    if (language && KbLocaleFromLcid(language, TRUE) == NO_LOCALE) {
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
    if (size < 0 || (size && !name)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return copy_locale_name(KbLocaleName(KbLocaleFromLcid(user_lcid(), FALSE)), name, size);
}

int WINAPI GetSystemDefaultLocaleName(LPWSTR name, int size)
{
    if (size < 0 || (size && !name)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return copy_locale_name(KbLocaleName(KbLocaleFromLcid(system_lcid(), FALSE)), name, size);
}

int WINAPI LCIDToLocaleName(LCID locale, LPWSTR name, int size, DWORD flags)
{
    if (size < 0 || (size && !name) || (flags & ~(DWORD)(LOCALE_ALLOW_NEUTRAL_NAMES | LOCALE_RETURN_GENITIVE_NAMES))) {
        KbSetError(flags & ~(DWORD)(LOCALE_ALLOW_NEUTRAL_NAMES | LOCALE_RETURN_GENITIVE_NAMES) ? ERROR_INVALID_FLAGS
                                                                                                : ERROR_INVALID_PARAMETER);
        return 0;
    }
    LCID resolved = KbResolveLcid(locale);
    /* Dari maps to the Persian locale of Iran. */
    if ((resolved & 0xFFFF) == 0x008C)
        resolved = (resolved & ~0xFFFFu) | 0x0429;
    /* alternate sorts have names of their own. */
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_alternate_sorts); i++)
        if (g_alternate_sorts[i].Lcid == resolved)
            return copy_locale_name(g_alternate_sorts[i].Name, name, size);
    UINT l = KbLocaleFromLcid(resolved, (flags & LOCALE_ALLOW_NEUTRAL_NAMES) != 0);
    if (l == NO_LOCALE) {
        for (ULONG i = 0; i < RTL_NUMBER_OF(g_legacy_names); i++)
            if (g_legacy_names[i].Lcid == (resolved & 0xFFFF))
                return copy_locale_name(g_legacy_names[i].Name, name, size);
        /* identifiers with no language, such as 0x2000, mean the user's locale. */
        if (PRIMARYLANGID(resolved) == 0 && (resolved & 0xFFFF) >= 0x2000 && (resolved & 0xFFFF) < 0x5000)
            l = KbLocaleFromLcid(user_lcid(), (flags & LOCALE_ALLOW_NEUTRAL_NAMES) != 0);
    }
    if (l == NO_LOCALE) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return copy_locale_name(KbLocaleName(l), name, size);
}

/*
 * a well formed tag: a 2 or 3 letter language, a 4 letter script, a region of 2 letters or 3 digits, variants,
 * extensions after a single letter and private use after x, separated by hyphens or underscores.
 */
static BOOL well_formed_tag(PCWSTR name)
{
    WCHAR subtag[LOCALE_NAME_MAX_LENGTH + 1];
    PCWSTR p = name;
    int position = 0; /* 0 language, 1 script, 2 region, 3 variants, 4 extensions, 5 private use. */
    BOOL first = TRUE;
    BOOL extension_open = FALSE;
    SIZE_T total = 0;
    for (;;) {
        SIZE_T n = 0;
        while (p[n] && p[n] != '-' && p[n] != '_') {
            WCHAR c = p[n];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) || n >= 8)
                return FALSE;
            subtag[n] = c;
            n++;
        }
        total += n + 1;
        if (!n || total > LOCALE_NAME_MAX_LENGTH)
            return FALSE;
        subtag[n] = 0;
        BOOL alpha = TRUE, digits = TRUE;
        for (SIZE_T i = 0; i < n; i++) {
            alpha &= (subtag[i] | 0x20) >= 'a' && (subtag[i] | 0x20) <= 'z';
            digits &= subtag[i] >= '0' && subtag[i] <= '9';
        }
        if (first) {
            /* a lone i starts a legacy tag, a lone x a private use one. */
            if (!alpha || (n < 2 && !((subtag[0] | 0x20) == 'i' || (subtag[0] | 0x20) == 'x')) || n > 3)
                return FALSE;
            if ((subtag[0] | 0x20) == 'x' && n == 1)
                position = 5;
            first = FALSE;
            position = position == 5 ? 5 : 1;
        } else if (position == 5) {
            /* anything goes after the private use mark. */
        } else if (n == 1) {
            if ((subtag[0] | 0x20) == 'x')
                position = 5;
            else
                position = 4, extension_open = TRUE;
        } else if (position == 4) {
            if (n < 2)
                return FALSE;
            extension_open = FALSE;
        } else if (position <= 1 && n == 4 && alpha) {
            position = 2;
        } else if (position <= 2 && ((n == 2 && alpha) || (n == 3 && digits))) {
            position = 3;
        } else if (position <= 3 && ((n >= 5 && n <= 8) || (n == 4 && subtag[0] >= '0' && subtag[0] <= '9'))) {
            position = 3;
        } else {
            return FALSE;
        }
        p += n;
        if (!*p)
            break;
        p++;
    }
    UNREFERENCED_PARAMETER(extension_open);
    return TRUE;
}

/* tags are looked up with underscores turned into hyphens. */
static BOOL normalized_name(PCWSTR name, WCHAR *out)
{
    SIZE_T n = 0;
    for (; name[n]; n++) {
        if (n >= LOCALE_NAME_MAX_LENGTH)
            return FALSE;
        out[n] = name[n] == '_' ? '-' : name[n];
    }
    out[n] = 0;
    return TRUE;
}

/*
 * a tag whose script is its region's default resolves without the script: zh-Hans-CN is zh-CN. NO_LOCALE when there
 * is no such short form.
 */
static UINT find_without_script(PCWSTR name)
{
    PCWSTR dash = wcschr(name, '-');
    if (!dash || wcslen(dash) < 8 || dash[5] != '-')
        return NO_LOCALE;
    for (int i = 1; i <= 4; i++)
        if (!((dash[i] >= 'a' && dash[i] <= 'z') || (dash[i] >= 'A' && dash[i] <= 'Z')))
            return NO_LOCALE;
    WCHAR shorter[LOCALE_NAME_MAX_LENGTH + 1];
    SIZE_T prefix = (SIZE_T)(dash - name);
    memcpy(shorter, name, prefix * sizeof(WCHAR));
    wcscpy(shorter + prefix, dash + 5);
    UINT locale = find_name(shorter);
    if (locale == NO_LOCALE)
        return NO_LOCALE;
    /* the script is redundant when it is the locale's default one. */
    PCWSTR scripts = KbLocaleText(locale, NLS_S_SSCRIPTS);
    WCHAR script[6] = {dash[1], dash[2], dash[3], dash[4], ';', 0};
    script[0] = (WCHAR)(script[0] & ~0x20);
    for (int i = 1; i < 4; i++)
        script[i] = (WCHAR)(script[i] | 0x20);
    PCWSTR first_script = scripts;
    return !wcsncmp(first_script, script, 5) || wcsstr(scripts, script) ? locale : NO_LOCALE;
}

static UINT find_tag(PCWSTR name)
{
    WCHAR work[LOCALE_NAME_MAX_LENGTH + 1];
    if (!normalized_name(name, work))
        return NO_LOCALE;
    UINT locale = find_name(work);
    return locale != NO_LOCALE ? locale : find_without_script(work);
}

LCID WINAPI LocaleNameToLCID(LPCWSTR name, DWORD flags)
{
    if (!name && !(flags & ~(DWORD)LOCALE_ALLOW_NEUTRAL_NAMES))
        return locale_lcid(KbLocaleFromLcid(user_lcid(), FALSE), FALSE);
    UINT locale = name ? find_tag(name) : NO_LOCALE;
    if (locale == NO_LOCALE && name && !_wcsicmp(name, LOCALE_NAME_SYSTEM_DEFAULT))
        locale = KbLocaleFromLcid(system_lcid(), TRUE);
    if (locale == NO_LOCALE) {
        for (ULONG i = 0; name && i < RTL_NUMBER_OF(g_alternate_sorts); i++)
            if (!compare_names(name, g_alternate_sorts[i].Name))
                return g_alternate_sorts[i].Lcid;
        for (ULONG i = 0; name && i < RTL_NUMBER_OF(g_legacy_names); i++)
            if (!compare_names(name, g_legacy_names[i].Name))
                return g_legacy_names[i].Lcid;
        if (name && well_formed_tag(name))
            return LOCALE_CUSTOM_UNSPECIFIED;
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return locale_lcid(locale, (flags & LOCALE_ALLOW_NEUTRAL_NAMES) != 0);
}

/*
 * a name resolves to itself when Windows has it, else to the closest locale found by dropping trailing subtags. A
 * neutral locale becomes its default region, and a name that matches nothing resolves to the empty name.
 */
int WINAPI ResolveLocaleName(LPCWSTR name, LPWSTR resolved, int size)
{
    if (!name || size < 0 || (size && !resolved)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    WCHAR work[LOCALE_NAME_MAX_LENGTH];
    SIZE_T length = 0;
    /* the sort after an underscore, extensions after a singleton and private use are dropped first. */
    for (; name[length] && length < RTL_NUMBER_OF(work) - 1; length++) {
        WCHAR c = name[length];
        if (c == '_')
            c = '-';
        work[length] = c;
    }
    if (name[length]) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    work[length] = 0;
    for (SIZE_T i = 0; i + 2 < length; i++)
        if (work[i] == '-' && work[i + 2] == '-' && (work[i + 1] | 0x20) >= 'a' && (work[i + 1] | 0x20) <= 'z' &&
            (work[i + 1] == 'x' || work[i + 1] == 'u' || work[i + 1] == 'X' || work[i + 1] == 'U' || work[i + 1] == 't' ||
             work[i + 1] == 'T')) {
            work[i] = 0;
            length = i;
            break;
        }
    PCWSTR result = L"";
    if (!_wcsicmp(name, LOCALE_NAME_SYSTEM_DEFAULT))
        return copy_locale_name(KbLocaleName(KbLocaleFromLcid(system_lcid(), FALSE)), resolved, size);
    for (;;) {
        UINT locale = find_tag(work);
        if (locale != NO_LOCALE) {
            result = KbLocaleName(KbLocaleSpecific(locale));
            break;
        }
        PWSTR dash = nullptr;
        for (PWSTR p = work; *p; p++)
            if (*p == '-')
                dash = p;
        if (!dash)
            break;
        *dash = 0;
    }
    return copy_locale_name(result, resolved, size);
}

LCID WINAPI ConvertDefaultLocale(LCID locale)
{
    if ((locale & 0xFFFF) == 0x1400 || (locale & 0xFFFF) == 0x1800 || (locale & 0xFFFF) == 0x1C00)
        return locale;
    LCID resolved = KbResolveLcid(locale);
    if (PRIMARYLANGID(resolved) == 0 && (resolved & 0xFFFF) >= 0x2000 && (resolved & 0xFFFF) < 0x5000)
        resolved = user_lcid();
    if ((resolved & 0xFFFF) == 0x008C)
        return 0x0429;
    UINT l = KbLocaleFromLcid(resolved, FALSE);
    if (l == NO_LOCALE)
        return resolved;
    if (locale_lcid(l, FALSE) == 0x1000 || (resolved & 0xFFFF) == 0x1000)
        return 0x0C00;
    /* a neutral identifier becomes its language's main region, a specific one keeps its sort order. */
    if (find_lcid(resolved & 0xFFFF) != NO_LOCALE)
        return resolved;
    return locale_lcid(l, FALSE);
}

BOOL WINAPI IsValidLocale(LCID locale, DWORD flags)
{
    if (flags & ~(DWORD)(LCID_INSTALLED | LCID_SUPPORTED)) {
        KbSetError(ERROR_INVALID_FLAGS);
        return FALSE;
    }
    LANGID lang = LANGIDFROMLCID(locale);
    if (find_lcid(lang) != NO_LOCALE)
        return TRUE;
    /* neutral identifiers with a script are locales, bare languages are not. */
    return lang > 0x3FF && find_neutral_lcid(lang) != NO_LOCALE;
}

BOOL WINAPI IsValidLocaleName(LPCWSTR name)
{
    if (!name)
        return FALSE;
    return find_name(name) != NO_LOCALE || well_formed_tag(name);
}

/* ------------------------------------------------------------------ geographic location */

/* the nation SetUserGeoID stored in the international settings, or 0. */
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
        UINT l = KbLocaleFromLcid(user_lcid(), FALSE);
        return l != NO_LOCALE ? KbLocaleNumber(l, NLS_N_IGEOID) : 244;
    }
    if (geo_class == GEOCLASS_REGION)
        return 39070;
    return -1;
}

int WINAPI GetUserDefaultGeoName(LPWSTR name, int size)
{
    UINT l = KbLocaleFromLcid(user_lcid(), FALSE);
    return copy_locale_name(l != NO_LOCALE ? KbLocaleText(l, NLS_S_SISO3166CTRYNAME) : L"US", name, size);
}

/* ------------------------------------------------------------------ locale information */

typedef struct LOCALE_VALUE {
    PCWSTR Text;  /* string value, or NULL when numeric. */
    DWORD Number;
    BOOL Hex;     /* printed as four hex digits. */
    BOOL Width3;  /* printed with at least three digits. */
    BOOL Binary;  /* LOCALE_FONTSIGNATURE. */
} LOCALE_VALUE;

static const NLS_ITEM *find_item(LCTYPE type)
{
    LONG lo = 0, hi = (LONG)RTL_NUMBER_OF(g_nls_items) - 1;
    while (lo <= hi) {
        LONG mid = (lo + hi) / 2;
        if (g_nls_items[mid].Type == type)
            return &g_nls_items[mid];
        if (g_nls_items[mid].Type < type)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return nullptr;
}

/* FALSE for unknown types. */
static BOOL locale_value(UINT locale, LCTYPE type, BOOL genitive, LOCALE_VALUE *v)
{
    memset(v, 0, sizeof(*v));
    if (type == LOCALE_FONTSIGNATURE) {
        v->Binary = TRUE;
        return TRUE;
    }
    /* no built-in locale is a custom one. */
    if (type == 0x7D)
        return TRUE;
    if (genitive && type >= LOCALE_SMONTHNAME1 && type <= LOCALE_SMONTHNAME12) {
        v->Text = KbLocaleText(locale, NLS_S_GENMONTH1 + (type - LOCALE_SMONTHNAME1));
        return TRUE;
    }
    if (genitive && type >= LOCALE_SABBREVMONTHNAME1 && type <= LOCALE_SABBREVMONTHNAME12) {
        v->Text = KbLocaleText(locale, NLS_S_GENABBREV1 + (type - LOCALE_SABBREVMONTHNAME1));
        return TRUE;
    }
    /* types from 0x3000 up are the system's private data, not Windows types. */
    const NLS_ITEM *item = type >= 0x3000 ? nullptr : find_item(type);
    if (!item)
        return FALSE;
    if (item->Kind == 0)
        v->Text = KbLocaleText(locale, item->Slot);
    else {
        v->Number = (DWORD)KbLocaleNumber(locale, item->Slot);
        v->Hex = item->Kind == 2;
        v->Width3 = item->Kind == 3;
    }
    return TRUE;
}

static int get_locale_info(UINT locale, LCTYPE lctype, LPWSTR data, int size)
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
    /* only alternate sorts have a sort name. */
    if (type == LOCALE_SSORTNAME) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!locale_value(locale, type, genitive, &v)) {
        KbSetError(ERROR_INVALID_FLAGS);
        return 0;
    }
    if (v.Binary) {
        if (want_number) {
            KbSetError(ERROR_INVALID_FLAGS);
            return 0;
        }
        int n = 16;
        if (!size)
            return n;
        if (size < n) {
            KbSetError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        memcpy(data, &g_nls_fontsig[locale * 16], 16 * sizeof(WCHAR));
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
        KbFormat(number, RTL_NUMBER_OF(number), v.Hex ? L"%04x" : v.Width3 ? L"%03u" : L"%u", v.Number);
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

static UINT locale_or_error(LCID lcid)
{
    UINT l = KbLocaleFromLcid(lcid, TRUE);
    if (l == NO_LOCALE)
        KbSetError(ERROR_INVALID_PARAMETER);
    return l;
}

int WINAPI GetLocaleInfoW(LCID locale, LCTYPE type, LPWSTR data, int size)
{
    UINT l = locale_or_error(locale);
    return l == NO_LOCALE ? 0 : get_locale_info(l, type, data, size);
}

int WINAPI GetLocaleInfoEx(LPCWSTR name, LCTYPE type, LPWSTR data, int size)
{
    UINT l = KbLocaleFromName(name);
    if (l == NO_LOCALE) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return get_locale_info(l, type, data, size);
}

int WINAPI GetLocaleInfoA(LCID locale, LCTYPE type, LPSTR data, int size)
{
    UINT l = locale_or_error(locale);
    if (l == NO_LOCALE)
        return 0;
    if (size < 0 || (size && !data)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    LCTYPE base = type & ~(LOCALE_NOUSEROVERRIDE | LOCALE_USE_CP_ACP | LOCALE_RETURN_NUMBER |
                           LOCALE_RETURN_GENITIVE_NAMES);
    if (base == LOCALE_FONTSIGNATURE || (type & LOCALE_RETURN_NUMBER)) {
        WCHAR raw[32];
        int n = get_locale_info(l, type, raw, RTL_NUMBER_OF(raw));
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
    WCHAR w[256];
    if (!get_locale_info(l, type, w, RTL_NUMBER_OF(w)))
        return 0;
    UINT cp = (type & LOCALE_USE_CP_ACP) ? CP_ACP : KbLocaleCodePage(locale, 0);
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

/* ------------------------------------------------------------------ enumeration */

/* locale and alternate sort identifiers in increasing order, filtered by flags. */
static BOOL enumerate_identifiers(DWORD flags, BOOL (*emit)(LCID, PVOID), PVOID context)
{
    DWORD kind = flags & (LCID_INSTALLED | LCID_SUPPORTED);
    if ((flags & ~(DWORD)(LCID_INSTALLED | LCID_SUPPORTED | LCID_ALTERNATE_SORTS)) || kind == (LCID_INSTALLED | LCID_SUPPORTED) ||
        !flags) {
        KbSetError(ERROR_INVALID_FLAGS);
        return FALSE;
    }
    /* traditional Spanish sort is among the locales, it has its own identifier. */
    ULONG i = 0, k = 0;
    while (kind && (i < RTL_NUMBER_OF(g_nls_lcids) || k < RTL_NUMBER_OF(g_alternate_sorts))) {
        LCID next = 0xFFFFFFFFu;
        if (i < RTL_NUMBER_OF(g_nls_lcids))
            next = g_nls_lcids[i];
        while (k < RTL_NUMBER_OF(g_alternate_sorts) && (g_alternate_sorts[k].Lcid >> 16))
            k++;
        if (k < RTL_NUMBER_OF(g_alternate_sorts) && g_alternate_sorts[k].Lcid < next) {
            next = g_alternate_sorts[k].Lcid;
            k++;
        } else if (i < RTL_NUMBER_OF(g_nls_lcids)) {
            i++;
        } else {
            break;
        }
        if (next == LOCALE_INVARIANT)
            continue;
        if (!emit(next, context))
            return TRUE;
    }
    if (flags & LCID_ALTERNATE_SORTS) {
        for (k = 0; k < RTL_NUMBER_OF(g_alternate_sorts); k++)
            if ((g_alternate_sorts[k].Lcid >> 16) && !emit(g_alternate_sorts[k].Lcid, context))
                return TRUE;
    }
    return TRUE;
}

typedef struct ENUM_WIDE {
    LOCALE_ENUMPROCW Proc;
} ENUM_WIDE;

static BOOL emit_wide(LCID lcid, PVOID context)
{
    WCHAR buf[16];
    KbFormat(buf, RTL_NUMBER_OF(buf), L"%08x", lcid);
    return ((ENUM_WIDE *)context)->Proc(buf);
}

BOOL WINAPI EnumSystemLocalesW(LOCALE_ENUMPROCW proc, DWORD flags)
{
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ENUM_WIDE context = {proc};
    return enumerate_identifiers(flags, emit_wide, &context);
}

typedef struct ENUM_NARROW {
    LOCALE_ENUMPROCA Proc;
} ENUM_NARROW;

static BOOL emit_narrow(LCID lcid, PVOID context)
{
    char buf[16];
    KbFormatA(buf, sizeof(buf), "%08x", lcid);
    return ((ENUM_NARROW *)context)->Proc(buf);
}

BOOL WINAPI EnumSystemLocalesA(LOCALE_ENUMPROCA proc, DWORD flags)
{
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ENUM_NARROW context = {proc};
    return enumerate_identifiers(flags, emit_narrow, &context);
}

BOOL WINAPI EnumSystemLocalesEx(LOCALE_ENUMPROCEX proc, DWORD flags, LPARAM param, LPVOID reserved)
{
    if (!proc || reserved) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (flags & ~(DWORD)(LOCALE_WINDOWS | LOCALE_SUPPLEMENTAL | LOCALE_ALTERNATE_SORTS | LOCALE_REPLACEMENT |
                         LOCALE_NEUTRALDATA | LOCALE_SPECIFICDATA)) {
        KbSetError(ERROR_INVALID_FLAGS);
        return FALSE;
    }
    BOOL all = !(flags & (LOCALE_NEUTRALDATA | LOCALE_SPECIFICDATA));
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_nls_heads); i++) {
        BOOL neutral = is_neutral(i);
        if (neutral ? !(all || (flags & LOCALE_NEUTRALDATA)) : !(all || (flags & LOCALE_SPECIFICDATA)))
            continue;
        if (flags && !(flags & (LOCALE_WINDOWS | LOCALE_NEUTRALDATA | LOCALE_SPECIFICDATA | LOCALE_ALTERNATE_SORTS)))
            continue;
        if (!proc((LPWSTR)KbLocaleName(i), LOCALE_WINDOWS | (neutral ? LOCALE_NEUTRALDATA : LOCALE_SPECIFICDATA), param))
            return TRUE;
    }
    if (flags & LOCALE_ALTERNATE_SORTS)
        for (ULONG i = 0; i < RTL_NUMBER_OF(g_alternate_sorts); i++)
            if (!proc((LPWSTR)g_alternate_sorts[i].Name, LOCALE_ALTERNATE_SORTS | LOCALE_SPECIFICDATA, param))
                return TRUE;
    return TRUE;
}

UINT KbLocaleCount(void)
{
    return RTL_NUMBER_OF(g_nls_heads);
}

/* the locale's own calendar first, then the others it supports, ended by a zero. */
const UINT8 *KbLocaleCalendars(UINT locale)
{
    return &g_nls_callist[locale * 8];
}

/* NULL when the locale has no such calendar. */
PCWSTR KbLocaleCalendarText(UINT locale, UINT kind, UINT slot)
{
    UINT16 key = (UINT16)(locale * 8 + kind);
    LONG lo = 0, hi = (LONG)RTL_NUMBER_OF(g_nls_calkeys) - 1;
    while (lo <= hi) {
        LONG mid = (lo + hi) / 2;
        if (g_nls_calkeys[mid] == key)
            return pool_text(g_nls_calrec[mid * NLS_CALREC_SIZE + slot]);
        if (g_nls_calkeys[mid] < key)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return nullptr;
}

/* ------------------------------------------------------------------ preferred languages */

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

/* groups 1 to 17 are valid. A locale belongs to the group of its ansi code page. */
BOOL WINAPI IsValidLanguageGroup(LGRPID group, DWORD flags)
{
    return !(flags & ~(DWORD)(LGRPID_INSTALLED | LGRPID_SUPPORTED)) &&
           (flags & (LGRPID_INSTALLED | LGRPID_SUPPORTED)) != (LGRPID_INSTALLED | LGRPID_SUPPORTED) && group - 1 <= 0x10;
}

static const struct {
    LGRPID Id;
    PCWSTR Name;
} g_language_groups[] = {
    {LGRPID_WESTERN_EUROPE, L"Western Europe and United States"},
    {LGRPID_CENTRAL_EUROPE, L"Central Europe"},
    {LGRPID_BALTIC, L"Baltic"},
    {LGRPID_GREEK, L"Greek"},
    {LGRPID_CYRILLIC, L"Cyrillic"},
    {LGRPID_TURKIC, L"Turkic"},
    {LGRPID_JAPANESE, L"Japanese"},
    {LGRPID_KOREAN, L"Korean"},
    {LGRPID_TRADITIONAL_CHINESE, L"Traditional Chinese"},
    {LGRPID_SIMPLIFIED_CHINESE, L"Simplified Chinese"},
    {LGRPID_THAI, L"Thai"},
    {LGRPID_HEBREW, L"Hebrew"},
    {LGRPID_ARABIC, L"Arabic"},
    {LGRPID_VIETNAMESE, L"Vietnamese"},
    {LGRPID_INDIC, L"Indic"},
    {LGRPID_GEORGIAN, L"Georgian"},
    {LGRPID_ARMENIAN, L"Armenian"},
};

static LGRPID language_group_of(UINT locale)
{
    switch (g_nls_heads[locale].Lcid & 0xFFFF) {
    case 0x0437:
        return LGRPID_GEORGIAN;
    case 0x042B:
        return LGRPID_ARMENIAN;
    }
    switch (KbLocaleNumber(locale, NLS_N_IDEFAULTANSICODEPAGE)) {
    case 1250:
        return LGRPID_CENTRAL_EUROPE;
    case 1251:
        return LGRPID_CYRILLIC;
    case 1253:
        return LGRPID_GREEK;
    case 1254:
        return LGRPID_TURKIC;
    case 1255:
        return LGRPID_HEBREW;
    case 1256:
        return LGRPID_ARABIC;
    case 1257:
        return LGRPID_BALTIC;
    case 1258:
        return LGRPID_VIETNAMESE;
    case 874:
        return LGRPID_THAI;
    case 932:
        return LGRPID_JAPANESE;
    case 936:
        return LGRPID_SIMPLIFIED_CHINESE;
    case 949:
        return LGRPID_KOREAN;
    case 950:
        return LGRPID_TRADITIONAL_CHINESE;
    case 1252:
        return LGRPID_WESTERN_EUROPE;
    default:
        return LGRPID_INDIC;
    }
}

BOOL WINAPI EnumSystemLanguageGroupsW(LANGUAGEGROUP_ENUMPROCW proc, DWORD flags, LONG_PTR param)
{
    if (!proc || (flags & ~(DWORD)(LGRPID_INSTALLED | LGRPID_SUPPORTED)) ||
        (flags & (LGRPID_INSTALLED | LGRPID_SUPPORTED)) == (LGRPID_INSTALLED | LGRPID_SUPPORTED)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* only the first group is installed, the others are supported. */
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_language_groups); i++) {
        WCHAR id[16], name[80];
        if (!(flags & LGRPID_SUPPORTED) && g_language_groups[i].Id != LGRPID_WESTERN_EUROPE)
            continue;
        KbFormat(id, RTL_NUMBER_OF(id), L"%08x", g_language_groups[i].Id);
        wcscpy(name, g_language_groups[i].Name);
        if (!proc(g_language_groups[i].Id, id, name, g_language_groups[i].Id == LGRPID_WESTERN_EUROPE ? LGRPID_INSTALLED : LGRPID_SUPPORTED, param))
            break;
    }
    return TRUE;
}

BOOL WINAPI EnumLanguageGroupLocalesW(LANGGROUPLOCALE_ENUMPROCW proc, LGRPID group, DWORD flags, LONG_PTR param)
{
    if (!proc || flags || group - 1 > 0x10) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_nls_lcids); i++) {
        if (g_nls_lcids[i] == LOCALE_INVARIANT || language_group_of(g_nls_lcid_locale[i]) != group)
            continue;
        WCHAR text[16];
        KbFormat(text, RTL_NUMBER_OF(text), L"%08x", g_nls_lcids[i]);
        if (!proc(group, g_nls_lcids[i], text, param))
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
 * both lists are four letter script names, each followed by a semicolon. Flag 1 lets the common script Latn through.
 * FALSE with no error means a script is missing from the locale's list, error 87 means a malformed list.
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
