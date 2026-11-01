/*
 * power schemes and settings as powrprof keeps them in the registry. windows asks its power service for each of these
 * over rpc, and the service reads and writes the keys below. here the keys are read and written in place, so the
 * schemes, the settings with their names, ranges and possible values, the active scheme and the overlay all answer
 * from the registry of the machine, which holds the defaults of an installed system.
 *
 *   Control\Power\User\PowerSchemes\<scheme>\<subgroup>\<setting>: ACSettingIndex and DCSettingIndex of a scheme
 *   Control\Power\PowerSettings\<subgroup>\<setting>: names, attributes, ranges, possible values and the defaults
 *       of each scheme in DefaultPowerSchemeValues
 */
#define POWRPROFAPI
#include <mime/win32/powrprof.h>
#include <mime/win32/objbase.h>
#include <mime/ntrtl.h>

#define USER_KEY L"System\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes"
#define SETTINGS_KEY L"System\\CurrentControlSet\\Control\\Power\\PowerSettings"

typedef enum {
    ACCESS_AC_POWER_SETTING_INDEX = 0,
    ACCESS_DC_POWER_SETTING_INDEX,
    ACCESS_FRIENDLY_NAME,
    ACCESS_DESCRIPTION,
    ACCESS_POSSIBLE_POWER_SETTING,
    ACCESS_POSSIBLE_POWER_SETTING_FRIENDLY_NAME,
    ACCESS_POSSIBLE_POWER_SETTING_DESCRIPTION,
    ACCESS_DEFAULT_AC_POWER_SETTING_INDEX,
    ACCESS_DEFAULT_DC_POWER_SETTING_INDEX,
    ACCESS_POSSIBLE_VALUE_MIN,
    ACCESS_POSSIBLE_VALUE_MAX,
    ACCESS_POSSIBLE_VALUE_INCREMENT,
    ACCESS_POSSIBLE_VALUE_UNITS,
    ACCESS_ICON_RESOURCE,
    ACCESS_DEFAULT_SECURITY_DESCRIPTOR,
    ACCESS_ATTRIBUTES,
    ACCESS_SCHEME,
    ACCESS_SUBGROUP,
    ACCESS_INDIVIDUAL_SETTING,
    ACCESS_ACTIVE_SCHEME,
    ACCESS_CREATE_SCHEME,
    ACCESS_PROFILE,
    ACCESS_OVERLAY_SCHEME,
    ACCESS_ACTIVE_OVERLAY_SCHEME,
} POWER_DATA_ACCESSOR;

static const GUID g_balanced = {0x381b4222, 0xf694, 0x41f0, {0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e}};
static const GUID g_high = {0x8c5e7fda, 0xe8bf, 0x4a96, {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};
static const GUID g_saver = {0xa1841308, 0x3541, 0x4fab, {0xbc, 0x81, 0xf7, 0x15, 0x56, 0xf2, 0x0b, 0x4a}};
static const GUID g_ultimate = {0xe9a42b02, 0xd5df, 0x448d, {0xaa, 0x00, 0x03, 0xf1, 0x47, 0x49, 0xeb, 0x61}};
static const GUID g_overlays[] = {
    {0x3af9b8d9, 0x7c97, 0x431d, {0xad, 0x78, 0x34, 0xa8, 0xbf, 0xea, 0x43, 0x9f}},
    {0x961cc777, 0x2547, 0x4f9d, {0x81, 0x74, 0x7d, 0x86, 0x18, 0x1b, 0x8a, 0x7a}},
    {0xded574b5, 0x45a0, 0x4f42, {0x87, 0x37, 0x46, 0x34, 0x5c, 0x09, 0xc2, 0x38}},
};

static BOOL same_guid(const GUID *a, const GUID *b)
{
    return !memcmp(a, b, sizeof(GUID));
}

static BOOL is_overlay(const GUID *scheme)
{
    for (unsigned i = 0; i < sizeof(g_overlays) / sizeof(g_overlays[0]); i++)
        if (same_guid(scheme, &g_overlays[i]))
            return TRUE;
    return FALSE;
}

static void guid_name(const GUID *g, WCHAR *out)
{
    static const WCHAR hex[] = L"0123456789abcdef";
    const BYTE *b = (const BYTE *)g;
    int at = 0;
    DWORD d1 = g->Data1;
    for (int i = 28; i >= 0; i -= 4)
        out[at++] = hex[(d1 >> i) & 15];
    out[at++] = L'-';
    for (int i = 12; i >= 0; i -= 4)
        out[at++] = hex[(g->Data2 >> i) & 15];
    out[at++] = L'-';
    for (int i = 12; i >= 0; i -= 4)
        out[at++] = hex[(g->Data3 >> i) & 15];
    out[at++] = L'-';
    for (int i = 0; i < 8; i++) {
        out[at++] = hex[b[8 + i] >> 4];
        out[at++] = hex[b[8 + i] & 15];
        if (i == 1)
            out[at++] = L'-';
    }
    out[at] = 0;
}

static int hex_digit(WCHAR c)
{
    if (c >= L'0' && c <= L'9')
        return c - L'0';
    if (c >= L'a' && c <= L'f')
        return c - L'a' + 10;
    if (c >= L'A' && c <= L'F')
        return c - L'A' + 10;
    return -1;
}

/* the guid a key name or string spells, with or without braces. */
static BOOL name_guid(const WCHAR *text, GUID *g)
{
    if (*text == L'{')
        text++;
    WCHAR plain[37];
    int n = 0;
    for (; n < 36 && text[n]; n++)
        plain[n] = text[n];
    if (n != 36 || (text[36] && !(text[36] == L'}' && !text[37])))
        return FALSE;
    static const int dashes[4] = {8, 13, 18, 23};
    BYTE bytes[16];
    int at = 0;
    DWORD d1 = 0;
    WORD d2 = 0, d3 = 0;
    for (int i = 0; i < 36; i++) {
        BOOL dash = i == dashes[0] || i == dashes[1] || i == dashes[2] || i == dashes[3];
        if (dash != (plain[i] == L'-'))
            return FALSE;
        if (dash)
            continue;
        int v = hex_digit(plain[i]);
        if (v < 0)
            return FALSE;
        if (i < 8)
            d1 = d1 << 4 | (DWORD)v;
        else if (i < 13)
            d2 = (WORD)(d2 << 4 | v);
        else if (i < 18)
            d3 = (WORD)(d3 << 4 | v);
        else if (at < 8) {
            int high = v, low = hex_digit(plain[++i]);
            if (low < 0)
                return FALSE;
            bytes[at++] = (BYTE)(high << 4 | low);
            if (at == 2 && plain[i + 1] == L'-')
                i++;
        }
    }
    g->Data1 = d1;
    g->Data2 = d2;
    g->Data3 = d3;
    memcpy(g->Data4, bytes, 8);
    return at == 8;
}

/* a key below the user schemes or the settings: base\a\b\c\tail, any part may be missing. */
static DWORD open_under(const WCHAR *base, const GUID *a, const GUID *b, const GUID *c, const WCHAR *tail, REGSAM access,
                        BOOL create, HKEY *key)
{
    WCHAR path[512];
    int at = 0;
    for (const WCHAR *p = base; *p;)
        path[at++] = *p++;
    const GUID *parts[3] = {a, b, c};
    for (int i = 0; i < 3; i++) {
        if (!parts[i])
            continue;
        path[at++] = L'\\';
        guid_name(parts[i], path + at);
        at += 36;
    }
    if (tail) {
        path[at++] = L'\\';
        for (const WCHAR *p = tail; *p;)
            path[at++] = *p++;
    }
    path[at] = 0;
    if (!create)
        return (DWORD)RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, access, key);
    return (DWORD)RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, access, nullptr, key, nullptr);
}

static DWORD read_value(HKEY key, const WCHAR *name, DWORD *type, BYTE *buffer, DWORD *size)
{
    DWORD actual_type = 0;
    DWORD room = size ? *size : 0;
    DWORD error = (DWORD)RegQueryValueExW(key, name, nullptr, &actual_type, buffer, size ? size : &room);
    if (type)
        *type = actual_type;
    return error;
}

/* ------------------------------------------------------------------ keys */

DWORD WINAPI PowerOpenUserPowerKey(HKEY *key, REGSAM access, BOOL open_existing)
{
    if (!key)
        return ERROR_INVALID_PARAMETER;
    if (open_existing)
        return (DWORD)RegOpenKeyExW(HKEY_LOCAL_MACHINE, USER_KEY, 0, access, key);
    return (DWORD)RegCreateKeyExW(HKEY_LOCAL_MACHINE, USER_KEY, 0, nullptr, 0, access, nullptr, key, nullptr);
}

DWORD WINAPI PowerOpenSystemPowerKey(HKEY *key, REGSAM access, BOOL open_existing)
{
    if (open_existing)
        return (DWORD)RegOpenKeyExW(HKEY_LOCAL_MACHINE, SETTINGS_KEY, 0, access, key);
    return (DWORD)RegCreateKeyExW(HKEY_LOCAL_MACHINE, SETTINGS_KEY, 0, nullptr, 0, access, nullptr, key, nullptr);
}

/* ------------------------------------------------------------------ the active scheme */

static DWORD read_guid_value(const WCHAR *name, GUID *guid)
{
    HKEY key;
    DWORD error = (DWORD)RegOpenKeyExW(HKEY_LOCAL_MACHINE, USER_KEY, 0, KEY_READ, &key);
    if (error)
        return error;
    WCHAR text[64];
    DWORD size = sizeof(text), type;
    error = read_value(key, name, &type, (BYTE *)text, &size);
    RegCloseKey(key);
    if (error)
        return error;
    if (type != REG_SZ || !name_guid(text, guid))
        return ERROR_INVALID_DATA;
    return ERROR_SUCCESS;
}

static DWORD write_guid_value(const WCHAR *name, const GUID *guid)
{
    HKEY key;
    DWORD error = (DWORD)RegCreateKeyExW(HKEY_LOCAL_MACHINE, USER_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (error)
        return error;
    WCHAR text[40];
    guid_name(guid, text);
    error = (DWORD)RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)text, 37 * sizeof(WCHAR));
    RegCloseKey(key);
    return error;
}

static BOOL scheme_exists(const GUID *scheme)
{
    HKEY key;
    if (open_under(USER_KEY, scheme, nullptr, nullptr, nullptr, KEY_READ, FALSE, &key))
        return FALSE;
    RegCloseKey(key);
    return TRUE;
}

DWORD WINAPI PowerGetActiveScheme(HKEY root, GUID **scheme)
{
    if (!scheme)
        return ERROR_INVALID_PARAMETER;
    *scheme = nullptr;
    GUID active;
    DWORD error = read_guid_value(L"ActivePowerScheme", &active);
    if (error)
        return error;
    *scheme = LocalAlloc(LMEM_ZEROINIT, sizeof(GUID));
    if (!*scheme)
        return ERROR_NOT_ENOUGH_MEMORY;
    **scheme = active;
    return ERROR_SUCCESS;
}

DWORD WINAPI PowerSetActiveScheme(HKEY root, const GUID *scheme)
{
    if (!scheme)
        return ERROR_INVALID_PARAMETER;
    if (is_overlay(scheme) || !scheme_exists(scheme))
        return ERROR_FILE_NOT_FOUND;
    return write_guid_value(L"ActivePowerScheme", scheme);
}

/* the settings of the scheme are read from the registry whenever they are asked for, so there is nothing to apply again. */
DWORD WINAPI PowerReapplyActiveScheme(void)
{
    GUID *active;
    DWORD error = PowerGetActiveScheme(nullptr, &active);
    if (!error)
        LocalFree(active);
    return error;
}

/* ------------------------------------------------------------------ setting values */

static const WCHAR *index_name(BOOL ac)
{
    return ac ? L"ACSettingIndex" : L"DCSettingIndex";
}

/* the subgroup of a setting that is asked for without one. */
static BOOL find_subgroup(const GUID *setting, GUID *subgroup)
{
    HKEY root;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, SETTINGS_KEY, 0, KEY_READ, &root))
        return FALSE;
    WCHAR name[64], path[80];
    BOOL found = FALSE;
    guid_name(setting, path);
    for (DWORD i = 0; !found; i++) {
        DWORD size = 64;
        if (RegEnumKeyExW(root, i, name, &size, nullptr, nullptr, nullptr, nullptr))
            break;
        GUID candidate;
        HKEY sub;
        WCHAR full[160];
        if (!name_guid(name, &candidate))
            continue;
        lstrcpyW(full, name);
        lstrcatW(full, L"\\");
        lstrcatW(full, path);
        if (!RegOpenKeyExW(root, full, 0, KEY_READ, &sub)) {
            RegCloseKey(sub);
            *subgroup = candidate;
            found = TRUE;
        }
    }
    RegCloseKey(root);
    return found;
}

/* the value of a scheme: what the user set, else the default of the scheme, else the default of the balanced one. */
static DWORD scheme_value(const GUID *scheme, const GUID *subgroup, const GUID *setting, BOOL ac, BOOL defaults_only,
                          DWORD *type, BYTE *buffer, DWORD *size)
{
    GUID found;
    if (!scheme || !setting || !size)
        return ERROR_INVALID_PARAMETER;
    if (!subgroup) {
        if (!find_subgroup(setting, &found))
            return ERROR_FILE_NOT_FOUND;
        subgroup = &found;
    }
    HKEY key;
    DWORD error = ERROR_FILE_NOT_FOUND;
    if (!defaults_only && !open_under(USER_KEY, scheme, subgroup, setting, nullptr, KEY_READ, FALSE, &key)) {
        error = read_value(key, index_name(ac), type, buffer, size);
        RegCloseKey(key);
        if (error != ERROR_FILE_NOT_FOUND)
            return error;
    }
    const GUID *order[2] = {scheme, &g_balanced};
    for (int i = 0; i < 2; i++) {
        if (i && same_guid(scheme, &g_balanced))
            break;
        WCHAR defaults[96];
        guid_name(order[i], defaults);
        HKEY leaf;
        WCHAR path[120];
        lstrcpyW(path, L"DefaultPowerSchemeValues\\");
        lstrcatW(path, defaults);
        if (open_under(SETTINGS_KEY, subgroup, setting, nullptr, path, KEY_READ, FALSE, &leaf))
            continue;
        error = read_value(leaf, index_name(ac), type, buffer, size);
        RegCloseKey(leaf);
        if (error != ERROR_FILE_NOT_FOUND)
            return error;
    }
    return ERROR_FILE_NOT_FOUND;
}

static DWORD read_index(const GUID *scheme, const GUID *subgroup, const GUID *setting, BOOL ac, BOOL defaults_only, DWORD *index)
{
    if (!index)
        return ERROR_INVALID_PARAMETER;
    DWORD type, size = sizeof(DWORD);
    DWORD error = scheme_value(scheme, subgroup, setting, ac, defaults_only, &type, (BYTE *)index, &size);
    if (!error && type != REG_DWORD)
        return ERROR_INVALID_DATA;
    return error;
}

static DWORD read_raw(const GUID *scheme, const GUID *subgroup, const GUID *setting, BOOL ac, PULONG type, BYTE *buffer,
                      DWORD *size)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    DWORD actual = 0;
    DWORD error = scheme_value(scheme, subgroup, setting, ac, FALSE, &actual, buffer, size);
    if (type)
        *type = actual;
    return error;
}

DWORD WINAPI PowerReadACValue(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting, PULONG type,
                              LPBYTE buffer, LPDWORD size)
{
    return read_raw(scheme, subgroup, setting, TRUE, type, buffer, size);
}

DWORD WINAPI PowerReadDCValue(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting, PULONG type,
                              PUCHAR buffer, LPDWORD size)
{
    return read_raw(scheme, subgroup, setting, FALSE, type, buffer, size);
}

DWORD WINAPI PowerReadACValueIndex(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting, LPDWORD index)
{
    return read_index(scheme, subgroup, setting, TRUE, FALSE, index);
}

DWORD WINAPI PowerReadDCValueIndex(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting, LPDWORD index)
{
    return read_index(scheme, subgroup, setting, FALSE, FALSE, index);
}

DWORD WINAPI PowerReadACDefaultIndex(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting, LPDWORD index)
{
    return read_index(scheme, subgroup, setting, TRUE, TRUE, index);
}

DWORD WINAPI PowerReadDCDefaultIndex(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting, LPDWORD index)
{
    return read_index(scheme, subgroup, setting, FALSE, TRUE, index);
}

static DWORD write_index(const GUID *scheme, const GUID *subgroup, const GUID *setting, BOOL ac, DWORD value)
{
    if (!scheme || !setting)
        return ERROR_INVALID_PARAMETER;
    GUID found;
    if (!subgroup) {
        if (!find_subgroup(setting, &found))
            return ERROR_FILE_NOT_FOUND;
        subgroup = &found;
    }
    if (!scheme_exists(scheme))
        return ERROR_FILE_NOT_FOUND;
    HKEY key;
    DWORD error = open_under(USER_KEY, scheme, subgroup, setting, nullptr, KEY_SET_VALUE, TRUE, &key);
    if (error)
        return error;
    error = (DWORD)RegSetValueExW(key, index_name(ac), 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
    RegCloseKey(key);
    return error;
}

DWORD WINAPI PowerWriteACValueIndex(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting, DWORD value)
{
    return write_index(scheme, subgroup, setting, TRUE, value);
}

DWORD WINAPI PowerWriteDCValueIndex(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting, DWORD value)
{
    return write_index(scheme, subgroup, setting, FALSE, value);
}

static DWORD write_default(const GUID *scheme, const GUID *subgroup, const GUID *setting, BOOL ac, DWORD value)
{
    if (!scheme || !subgroup || !setting)
        return ERROR_INVALID_PARAMETER;
    WCHAR defaults[96], path[120];
    guid_name(scheme, defaults);
    lstrcpyW(path, L"DefaultPowerSchemeValues\\");
    lstrcatW(path, defaults);
    HKEY key;
    DWORD error = open_under(SETTINGS_KEY, subgroup, setting, nullptr, path, KEY_SET_VALUE, TRUE, &key);
    if (error)
        return error;
    error = (DWORD)RegSetValueExW(key, index_name(ac), 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
    RegCloseKey(key);
    return error;
}

DWORD WINAPI PowerWriteACDefaultIndex(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting, DWORD value)
{
    return write_default(scheme, subgroup, setting, TRUE, value);
}

DWORD WINAPI PowerWriteDCDefaultIndex(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting, DWORD value)
{
    return write_default(scheme, subgroup, setting, FALSE, value);
}

/* the user's value is dropped, and the default of the scheme shows again. */
static DWORD restore_index(const GUID *scheme, const GUID *subgroup, const GUID *setting, BOOL ac)
{
    if (!scheme || !subgroup || !setting)
        return ERROR_INVALID_PARAMETER;
    HKEY key;
    DWORD error = open_under(USER_KEY, scheme, subgroup, setting, nullptr, KEY_SET_VALUE, FALSE, &key);
    if (error)
        return error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : error;
    error = (DWORD)RegDeleteValueW(key, index_name(ac));
    RegCloseKey(key);
    return error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : error;
}

DWORD WINAPI PowerRestoreACDefaultIndex(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting)
{
    return restore_index(scheme, subgroup, setting, TRUE);
}

DWORD WINAPI PowerRestoreDCDefaultIndex(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting)
{
    return restore_index(scheme, subgroup, setting, FALSE);
}

/* ------------------------------------------------------------------ texts */

/* a string that names a resource of a dll as "@file,-id,text" has its text after the id when the dll is not there. */
static const WCHAR *indirect_text(const WCHAR *value)
{
    if (value[0] != L'@')
        return value;
    const WCHAR *comma = wcschr(value, L',');
    if (!comma)
        return value;
    comma = wcschr(comma + 1, L',');
    return comma ? comma + 1 : value;
}

static DWORD read_text(HKEY key, const WCHAR *name, UCHAR *buffer, DWORD *size)
{
    WCHAR text[1024];
    DWORD room = sizeof(text) - sizeof(WCHAR), type;
    DWORD error = read_value(key, name, &type, (BYTE *)text, &room);
    if (error)
        return error;
    if (type != REG_SZ && type != REG_EXPAND_SZ)
        return ERROR_INVALID_DATA;
    text[room / sizeof(WCHAR)] = 0;
    const WCHAR *shown = indirect_text(text);
    DWORD bytes = (DWORD)((wcslen(shown) + 1) * sizeof(WCHAR)), capacity = size ? *size : 0;
    if (size)
        *size = bytes;
    if (!buffer || capacity < bytes)
        return ERROR_MORE_DATA;
    memcpy(buffer, shown, bytes);
    return ERROR_SUCCESS;
}

static DWORD write_text(HKEY key, const WCHAR *name, const UCHAR *buffer, DWORD size)
{
    if (!buffer || !size || (size & 1))
        return ERROR_INVALID_PARAMETER;
    return (DWORD)RegSetValueExW(key, name, 0, REG_SZ, buffer, size);
}

/* the key of a scheme, a subgroup or a setting by what is given: a scheme alone, a subgroup, or a setting of one. */
static DWORD text_key(const GUID *scheme, const GUID *subgroup, const GUID *setting, REGSAM access, BOOL create, HKEY *key)
{
    if (setting || subgroup) {
        GUID found;
        if (setting && !subgroup) {
            if (!find_subgroup(setting, &found))
                return ERROR_FILE_NOT_FOUND;
            subgroup = &found;
        }
        if (scheme && setting && !(access & KEY_SET_VALUE)) {
            /* a setting's text is the same in every scheme. */
        }
        return open_under(SETTINGS_KEY, subgroup, setting, nullptr, nullptr, access, create, key);
    }
    if (!scheme)
        return ERROR_INVALID_PARAMETER;
    return open_under(USER_KEY, scheme, nullptr, nullptr, nullptr, access, create, key);
}

static DWORD read_named(const GUID *scheme, const GUID *subgroup, const GUID *setting, const WCHAR *name, UCHAR *buffer,
                        DWORD *size)
{
    HKEY key;
    DWORD error = text_key(scheme, subgroup, setting, KEY_READ, FALSE, &key);
    if (error)
        return error;
    error = read_text(key, name, buffer, size);
    RegCloseKey(key);
    return error;
}

static DWORD write_named(const GUID *scheme, const GUID *subgroup, const GUID *setting, const WCHAR *name, const UCHAR *buffer,
                         DWORD size)
{
    HKEY key;
    DWORD error = text_key(scheme, subgroup, setting, KEY_SET_VALUE, TRUE, &key);
    if (error)
        return error;
    error = write_text(key, name, buffer, size);
    RegCloseKey(key);
    return error;
}

DWORD WINAPI PowerReadFriendlyName(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting, PUCHAR buffer,
                                   LPDWORD size)
{
    return read_named(scheme, subgroup, setting, L"FriendlyName", buffer, size);
}

DWORD WINAPI PowerReadDescription(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting, PUCHAR buffer,
                                  LPDWORD size)
{
    return read_named(scheme, subgroup, setting, L"Description", buffer, size);
}

DWORD WINAPI PowerReadIconResourceSpecifier(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting,
                                            PUCHAR buffer, LPDWORD size)
{
    return read_named(scheme, subgroup, setting, L"IconResource", buffer, size);
}

DWORD WINAPI PowerWriteFriendlyName(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting, PUCHAR buffer,
                                    DWORD size)
{
    return write_named(scheme, subgroup, setting, L"FriendlyName", buffer, size);
}

DWORD WINAPI PowerWriteDescription(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting, PUCHAR buffer,
                                   DWORD size)
{
    return write_named(scheme, subgroup, setting, L"Description", buffer, size);
}

DWORD WINAPI PowerWriteIconResourceSpecifier(HKEY root, const GUID *scheme, const GUID *subgroup, const GUID *setting,
                                             PUCHAR buffer, DWORD size)
{
    return write_named(scheme, subgroup, setting, L"IconResource", buffer, size);
}

/* ------------------------------------------------------------------ the possible values and ranges of a setting */

static DWORD possible_key(const GUID *subgroup, const GUID *setting, ULONG index, REGSAM access, BOOL create, HKEY *key)
{
    if (!subgroup || !setting)
        return ERROR_INVALID_PARAMETER;
    WCHAR number[12];
    int n = 0;
    ULONG digits[10];
    int d = 0;
    do {
        digits[d++] = index % 10;
        index /= 10;
    } while (index);
    while (d)
        number[n++] = (WCHAR)(L'0' + digits[--d]);
    number[n] = 0;
    return open_under(SETTINGS_KEY, subgroup, setting, nullptr, number, access, create, key);
}

static DWORD possible_text(const GUID *subgroup, const GUID *setting, ULONG index, const WCHAR *name, UCHAR *buffer, DWORD *size)
{
    HKEY key;
    DWORD error = possible_key(subgroup, setting, index, KEY_READ, FALSE, &key);
    if (error)
        return error;
    error = read_text(key, name, buffer, size);
    RegCloseKey(key);
    return error;
}

DWORD WINAPI PowerReadPossibleFriendlyName(HKEY root, const GUID *subgroup, const GUID *setting, ULONG index, PUCHAR buffer,
                                           LPDWORD size)
{
    return possible_text(subgroup, setting, index, L"FriendlyName", buffer, size);
}

DWORD WINAPI PowerReadPossibleDescription(HKEY root, const GUID *subgroup, const GUID *setting, ULONG index, PUCHAR buffer,
                                          LPDWORD size)
{
    return possible_text(subgroup, setting, index, L"Description", buffer, size);
}

DWORD WINAPI PowerReadPossibleValue(HKEY root, const GUID *subgroup, const GUID *setting, PULONG type, ULONG index,
                                    PUCHAR buffer, LPDWORD size)
{
    HKEY key;
    DWORD error = possible_key(subgroup, setting, index, KEY_READ, FALSE, &key);
    if (error)
        return error;
    DWORD actual = 0;
    error = read_value(key, L"SettingValue", &actual, buffer, size);
    if (type)
        *type = actual;
    RegCloseKey(key);
    return error;
}

static DWORD write_possible(const GUID *subgroup, const GUID *setting, ULONG index, const WCHAR *name, DWORD type,
                            const UCHAR *buffer, DWORD size)
{
    HKEY key;
    DWORD error = possible_key(subgroup, setting, index, KEY_SET_VALUE, TRUE, &key);
    if (error)
        return error;
    error = (DWORD)RegSetValueExW(key, name, 0, type, buffer, size);
    RegCloseKey(key);
    return error;
}

DWORD WINAPI PowerWritePossibleFriendlyName(HKEY root, const GUID *subgroup, const GUID *setting, ULONG index, PUCHAR buffer,
                                            DWORD size)
{
    return buffer && size && !(size & 1) ? write_possible(subgroup, setting, index, L"FriendlyName", REG_SZ, buffer, size)
                                         : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI PowerWritePossibleDescription(HKEY root, const GUID *subgroup, const GUID *setting, ULONG index, PUCHAR buffer,
                                           DWORD size)
{
    return buffer && size && !(size & 1) ? write_possible(subgroup, setting, index, L"Description", REG_SZ, buffer, size)
                                         : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI PowerWritePossibleValue(HKEY root, const GUID *subgroup, const GUID *setting, ULONG type, ULONG index,
                                     PUCHAR buffer, DWORD size)
{
    return buffer ? write_possible(subgroup, setting, index, L"SettingValue", type, buffer, size) : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI PowerCreatePossibleSetting(HKEY root, const GUID *subgroup, const GUID *setting, ULONG index)
{
    HKEY key;
    DWORD error = possible_key(subgroup, setting, index, KEY_READ, TRUE, &key);
    if (!error)
        RegCloseKey(key);
    return error;
}

DWORD WINAPI PowerCreateSetting(HKEY root, const GUID *subgroup, const GUID *setting)
{
    if (!subgroup || !setting)
        return ERROR_INVALID_PARAMETER;
    HKEY key;
    DWORD error = open_under(SETTINGS_KEY, subgroup, setting, nullptr, nullptr, KEY_READ, TRUE, &key);
    if (!error)
        RegCloseKey(key);
    return error;
}

/* the setting goes, with its text, its ranges and the values the schemes keep for it. */
DWORD WINAPI PowerRemovePowerSetting(const GUID *subgroup, const GUID *setting)
{
    if (!subgroup || !setting)
        return ERROR_INVALID_PARAMETER;
    WCHAR name[40];
    guid_name(setting, name);
    HKEY group;
    DWORD error = open_under(SETTINGS_KEY, subgroup, nullptr, nullptr, nullptr, KEY_ALL_ACCESS, FALSE, &group);
    if (error)
        return error;
    error = (DWORD)RegDeleteTreeW(group, name);
    RegCloseKey(group);
    if (error)
        return error;
    HKEY schemes;
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, USER_KEY, 0, KEY_ALL_ACCESS, &schemes)) {
        for (DWORD i = 0;; i++) {
            WCHAR scheme[64], path[160];
            DWORD size = 64;
            if (RegEnumKeyExW(schemes, i, scheme, &size, nullptr, nullptr, nullptr, nullptr))
                break;
            GUID guid;
            WCHAR sub[40];
            if (!name_guid(scheme, &guid))
                continue;
            guid_name(subgroup, sub);
            lstrcpyW(path, scheme);
            lstrcatW(path, L"\\");
            lstrcatW(path, sub);
            HKEY subkey;
            if (!RegOpenKeyExW(schemes, path, 0, KEY_ALL_ACCESS, &subkey)) {
                RegDeleteTreeW(subkey, name);
                RegCloseKey(subkey);
            }
        }
        RegCloseKey(schemes);
    }
    return ERROR_SUCCESS;
}

/* the attributes of a setting are the value itself, not an error. */
DWORD WINAPI PowerReadSettingAttributes(const GUID *subgroup, const GUID *setting)
{
    GUID found;
    if (!setting)
        return 0;
    if (!subgroup) {
        if (!find_subgroup(setting, &found))
            return 0;
        subgroup = &found;
    }
    HKEY key;
    if (open_under(SETTINGS_KEY, subgroup, setting, nullptr, nullptr, KEY_READ, FALSE, &key))
        return 0;
    DWORD attributes = 0, size = sizeof(attributes), type;
    if (read_value(key, L"Attributes", &type, (BYTE *)&attributes, &size) || type != REG_DWORD)
        attributes = 0;
    RegCloseKey(key);
    return attributes;
}

DWORD WINAPI PowerWriteSettingAttributes(const GUID *subgroup, const GUID *setting, DWORD attributes)
{
    if (!subgroup || !setting)
        return ERROR_INVALID_PARAMETER;
    HKEY key;
    DWORD error = open_under(SETTINGS_KEY, subgroup, setting, nullptr, nullptr, KEY_SET_VALUE, FALSE, &key);
    if (error)
        return error;
    error = (DWORD)RegSetValueExW(key, L"Attributes", 0, REG_DWORD, (const BYTE *)&attributes, sizeof(attributes));
    RegCloseKey(key);
    return error;
}

static DWORD read_range(const GUID *subgroup, const GUID *setting, const WCHAR *name, DWORD *value)
{
    if (!subgroup || !setting || !value)
        return ERROR_INVALID_PARAMETER;
    HKEY key;
    DWORD error = open_under(SETTINGS_KEY, subgroup, setting, nullptr, nullptr, KEY_READ, FALSE, &key);
    if (error)
        return error;
    DWORD type, size = sizeof(DWORD);
    error = read_value(key, name, &type, (BYTE *)value, &size);
    RegCloseKey(key);
    return !error && type != REG_DWORD ? ERROR_INVALID_DATA : error;
}

static DWORD write_range(const GUID *subgroup, const GUID *setting, const WCHAR *name, DWORD value)
{
    if (!subgroup || !setting)
        return ERROR_INVALID_PARAMETER;
    HKEY key;
    DWORD error = open_under(SETTINGS_KEY, subgroup, setting, nullptr, nullptr, KEY_SET_VALUE, TRUE, &key);
    if (error)
        return error;
    error = (DWORD)RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
    RegCloseKey(key);
    return error;
}

DWORD WINAPI PowerReadValueMin(HKEY root, const GUID *subgroup, const GUID *setting, LPDWORD value)
{
    return read_range(subgroup, setting, L"ValueMin", value);
}

DWORD WINAPI PowerReadValueMax(HKEY root, const GUID *subgroup, const GUID *setting, LPDWORD value)
{
    return read_range(subgroup, setting, L"ValueMax", value);
}

DWORD WINAPI PowerReadValueIncrement(HKEY root, const GUID *subgroup, const GUID *setting, LPDWORD value)
{
    return read_range(subgroup, setting, L"ValueIncrement", value);
}

DWORD WINAPI PowerWriteValueMin(HKEY root, const GUID *subgroup, const GUID *setting, DWORD value)
{
    return write_range(subgroup, setting, L"ValueMin", value);
}

DWORD WINAPI PowerWriteValueMax(HKEY root, const GUID *subgroup, const GUID *setting, DWORD value)
{
    return write_range(subgroup, setting, L"ValueMax", value);
}

DWORD WINAPI PowerWriteValueIncrement(HKEY root, const GUID *subgroup, const GUID *setting, DWORD value)
{
    return write_range(subgroup, setting, L"ValueIncrement", value);
}

DWORD WINAPI PowerReadValueUnitsSpecifier(HKEY root, const GUID *subgroup, const GUID *setting, PUCHAR buffer, LPDWORD size)
{
    if (!subgroup || !setting || !size)
        return ERROR_INVALID_PARAMETER;
    HKEY key;
    DWORD error = open_under(SETTINGS_KEY, subgroup, setting, nullptr, nullptr, KEY_READ, FALSE, &key);
    if (error)
        return error;
    error = read_text(key, L"ValueUnits", buffer, size);
    RegCloseKey(key);
    return error;
}

DWORD WINAPI PowerWriteValueUnitsSpecifier(HKEY root, const GUID *subgroup, const GUID *setting, PUCHAR buffer, DWORD size)
{
    if (!subgroup || !setting)
        return ERROR_INVALID_PARAMETER;
    HKEY key;
    DWORD error = open_under(SETTINGS_KEY, subgroup, setting, nullptr, nullptr, KEY_SET_VALUE, TRUE, &key);
    if (error)
        return error;
    error = write_text(key, L"ValueUnits", buffer, size);
    RegCloseKey(key);
    return error;
}

BOOLEAN WINAPI PowerIsSettingRangeDefined(const GUID *subgroup, const GUID *setting)
{
    DWORD min, max;
    return !read_range(subgroup, setting, L"ValueMin", &min) && !read_range(subgroup, setting, L"ValueMax", &max);
}

/* ------------------------------------------------------------------ enumeration */

static DWORD guid_out(const GUID *guid, UCHAR *buffer, DWORD *size)
{
    DWORD room = size ? *size : 0;
    if (size)
        *size = sizeof(GUID);
    if (!buffer || room < sizeof(GUID))
        return ERROR_MORE_DATA;
    memcpy(buffer, guid, sizeof(GUID));
    return ERROR_SUCCESS;
}

/* the index-th key under a key whose name is a guid, in the order the registry lists them. */
static DWORD nth_guid_key(HKEY parent, ULONG index, BOOL skip_overlays, GUID *guid)
{
    ULONG seen = 0;
    for (DWORD i = 0;; i++) {
        WCHAR name[64];
        DWORD size = 64;
        LONG error = RegEnumKeyExW(parent, i, name, &size, nullptr, nullptr, nullptr, nullptr);
        if (error == ERROR_NO_MORE_ITEMS)
            return ERROR_NO_MORE_ITEMS;
        if (error)
            return (DWORD)error;
        GUID candidate;
        if (!name_guid(name, &candidate) || (skip_overlays && is_overlay(&candidate)))
            continue;
        if (seen++ == index) {
            *guid = candidate;
            return ERROR_SUCCESS;
        }
    }
}

DWORD WINAPI PowerEnumerate(HKEY root, const GUID *scheme, const GUID *subgroup, POWER_DATA_ACCESSOR flags, ULONG index,
                            UCHAR *buffer, DWORD *size)
{
    GUID guid;
    HKEY key;
    DWORD error;
    switch (flags) {
    case ACCESS_SCHEME:
        error = (DWORD)RegOpenKeyExW(HKEY_LOCAL_MACHINE, USER_KEY, 0, KEY_READ, &key);
        if (error)
            return error;
        error = nth_guid_key(key, index, TRUE, &guid);
        RegCloseKey(key);
        break;
    case ACCESS_SUBGROUP:
        error = (DWORD)RegOpenKeyExW(HKEY_LOCAL_MACHINE, SETTINGS_KEY, 0, KEY_READ, &key);
        if (error)
            return error;
        error = nth_guid_key(key, index, FALSE, &guid);
        RegCloseKey(key);
        break;
    case ACCESS_INDIVIDUAL_SETTING:
        if (!subgroup)
            return ERROR_INVALID_PARAMETER;
        error = open_under(SETTINGS_KEY, subgroup, nullptr, nullptr, nullptr, KEY_READ, FALSE, &key);
        if (error)
            return error;
        error = nth_guid_key(key, index, FALSE, &guid);
        RegCloseKey(key);
        break;
    case ACCESS_ACTIVE_SCHEME: {
        GUID *active;
        error = PowerGetActiveScheme(nullptr, &active);
        if (error)
            return error;
        guid = *active;
        LocalFree(active);
        break;
    }
    default:
        return ERROR_INVALID_PARAMETER;
    }
    if (error)
        return error;
    return guid_out(&guid, buffer, size);
}

/* ------------------------------------------------------------------ schemes */

static void new_guid(GUID *guid)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    ULONG seed = counter.LowPart ^ GetTickCount() ^ (ULONG)GetCurrentThreadId() * 2654435761u;
    BYTE *b = (BYTE *)guid;
    for (int i = 0; i < 16; i += 4) {
        ULONG v = RtlRandomEx(&seed);
        memcpy(b + i, &v, 4);
    }
    guid->Data3 = (WORD)((guid->Data3 & 0x0FFF) | 0x4000);
    guid->Data4[0] = (BYTE)((guid->Data4[0] & 0x3F) | 0x80);
}

DWORD WINAPI PowerDuplicateScheme(HKEY root, const GUID *source, GUID **destination)
{
    if (!source || !destination)
        return ERROR_INVALID_PARAMETER;
    if (!scheme_exists(source))
        return ERROR_FILE_NOT_FOUND;
    GUID guid;
    BOOL given = *destination != nullptr;
    if (given)
        guid = **destination;
    else
        new_guid(&guid);
    HKEY schemes, target;
    DWORD error = (DWORD)RegOpenKeyExW(HKEY_LOCAL_MACHINE, USER_KEY, 0, KEY_ALL_ACCESS, &schemes);
    if (error)
        return error;
    WCHAR from[40], to[40];
    guid_name(source, from);
    guid_name(&guid, to);
    error = (DWORD)RegCreateKeyExW(schemes, to, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &target, nullptr);
    if (!error) {
        error = (DWORD)RegCopyTreeW(schemes, from, target);
        RegCloseKey(target);
    }
    RegCloseKey(schemes);
    if (error)
        return error;
    if (!given) {
        *destination = LocalAlloc(LMEM_ZEROINIT, sizeof(GUID));
        if (!*destination)
            return ERROR_NOT_ENOUGH_MEMORY;
        **destination = guid;
    }
    return ERROR_SUCCESS;
}

DWORD WINAPI PowerDeleteScheme(HKEY root, const GUID *scheme)
{
    if (!scheme)
        return ERROR_INVALID_PARAMETER;
    GUID *active;
    if (!PowerGetActiveScheme(nullptr, &active)) {
        BOOL is_active = same_guid(active, scheme);
        LocalFree(active);
        if (is_active)
            return ERROR_ACCESS_DENIED;
    }
    HKEY schemes;
    DWORD error = (DWORD)RegOpenKeyExW(HKEY_LOCAL_MACHINE, USER_KEY, 0, KEY_ALL_ACCESS, &schemes);
    if (error)
        return error;
    WCHAR name[40];
    guid_name(scheme, name);
    error = (DWORD)RegDeleteTreeW(schemes, name);
    RegCloseKey(schemes);
    return error;
}

/* the schemes that come with the system, whose settings can be put back. */
static BOOL is_default_scheme(const GUID *scheme)
{
    return same_guid(scheme, &g_balanced) || same_guid(scheme, &g_high) || same_guid(scheme, &g_saver) ||
           same_guid(scheme, &g_ultimate);
}

DWORD WINAPI PowerCanRestoreIndividualDefaultPowerScheme(const GUID *scheme)
{
    if (!scheme)
        return ERROR_INVALID_PARAMETER;
    return is_default_scheme(scheme) ? ERROR_SUCCESS : ERROR_FILE_NOT_FOUND;
}

/* the settings the user changed in a default scheme go, and the defaults show again. */
DWORD WINAPI PowerRestoreIndividualDefaultPowerScheme(const GUID *scheme)
{
    DWORD error = PowerCanRestoreIndividualDefaultPowerScheme(scheme);
    if (error)
        return error;
    HKEY key;
    error = open_under(USER_KEY, scheme, nullptr, nullptr, nullptr, KEY_ALL_ACCESS, FALSE, &key);
    if (error)
        return error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : error;
    for (;;) {
        WCHAR name[64];
        DWORD size = 64;
        if (RegEnumKeyExW(key, 0, name, &size, nullptr, nullptr, nullptr, nullptr))
            break;
        if (RegDeleteTreeW(key, name))
            break;
    }
    RegCloseKey(key);
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ overlays and the power mode */

DWORD WINAPI PowerGetOverlaySchemes(GUID **overlays, DWORD *count, BOOLEAN reserved)
{
    if (!overlays || !count)
        return ERROR_INVALID_PARAMETER;
    GUID found[3];
    DWORD n = 0;
    for (unsigned i = 0; i < sizeof(g_overlays) / sizeof(g_overlays[0]); i++)
        if (scheme_exists(&g_overlays[i]))
            found[n++] = g_overlays[i];
    if (n) {
        GUID *list = LocalAlloc(LMEM_ZEROINIT, n * sizeof(GUID));
        if (!list)
            return ERROR_NOT_ENOUGH_MEMORY;
        memcpy(list, found, n * sizeof(GUID));
        *overlays = list;
    }
    *count = n;
    return ERROR_SUCCESS;
}

/* the overlay chosen for the power source, none being the zero guid. */
static DWORD get_overlay(BOOL ac, GUID *out)
{
    DWORD error = read_guid_value(ac ? L"ActiveOverlayAcPowerScheme" : L"ActiveOverlayDcPowerScheme", out);
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        memset(out, 0, sizeof(*out));
        return ERROR_SUCCESS;
    }
    return error;
}

DWORD WINAPI PowerGetActualOverlayScheme(GUID *overlay)
{
    if (!overlay)
        return ERROR_INVALID_PARAMETER;
    return get_overlay(TRUE, overlay);
}

DWORD WINAPI PowerGetEffectiveOverlayScheme(GUID *overlay)
{
    if (!overlay)
        return ERROR_INVALID_PARAMETER;
    return get_overlay(TRUE, overlay);
}

DWORD WINAPI PowerSetActiveOverlayScheme(const GUID *overlay)
{
    if (!overlay)
        return ERROR_INVALID_PARAMETER;
    GUID none = {};
    if (!same_guid(overlay, &none) && !(is_overlay(overlay) && scheme_exists(overlay)))
        return ERROR_FILE_NOT_FOUND;
    DWORD error = write_guid_value(L"ActiveOverlayAcPowerScheme", overlay);
    return error ? error : write_guid_value(L"ActiveOverlayDcPowerScheme", overlay);
}

DWORD WINAPI PowerGetUserConfiguredACPowerMode(GUID *mode)
{
    return mode ? get_overlay(TRUE, mode) : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI PowerGetUserConfiguredDCPowerMode(GUID *mode)
{
    return mode ? get_overlay(FALSE, mode) : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI PowerSetUserConfiguredACPowerMode(const GUID *mode)
{
    return mode ? write_guid_value(L"ActiveOverlayAcPowerScheme", mode) : ERROR_INVALID_PARAMETER;
}

DWORD WINAPI PowerSetUserConfiguredDCPowerMode(const GUID *mode)
{
    return mode ? write_guid_value(L"ActiveOverlayDcPowerScheme", mode) : ERROR_INVALID_PARAMETER;
}

/* ------------------------------------------------------------------ access to a setting */

/* whether the caller may change the setting: the key of it opens for writing. */
DWORD WINAPI PowerSettingAccessCheck(POWER_DATA_ACCESSOR accessor, const GUID *setting)
{
    if (accessor > ACCESS_ACTIVE_OVERLAY_SCHEME)
        return ERROR_INVALID_PARAMETER;
    GUID subgroup;
    HKEY key;
    if (!setting)
        return ERROR_INVALID_PARAMETER;
    if (!find_subgroup(setting, &subgroup))
        return ERROR_FILE_NOT_FOUND;
    DWORD error = open_under(SETTINGS_KEY, &subgroup, setting, nullptr, nullptr, KEY_SET_VALUE, FALSE, &key);
    if (!error)
        RegCloseKey(key);
    return error;
}

DWORD WINAPI PowerSettingAccessCheckEx(POWER_DATA_ACCESSOR accessor, const GUID *setting, HANDLE token)
{
    (void)token;
    return PowerSettingAccessCheck(accessor, setting);
}
