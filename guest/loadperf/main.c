/*
 * loadperf.dll: registers the counter names and help text of a performance counter driver from its definition file,
 * the way lodctr does for the older kind of counters. a definition file has an [info] section naming the driver and
 * the file of symbols, the [languages] it has text for and the text of each name and help. the numbers the counters
 * take come after the last ones in use, are written for the driver below its Performance key, and the text goes into
 * the lists of names and help of each language. the other calls of the library report that they are not supported.
 */
#include <rtl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winerror.h>

#define LOADPERF DECLSPEC_EXPORT

#define PERFLIB L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Perflib"
#define SERVICES L"SYSTEM\\CurrentControlSet\\Services"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

/* ------------------------------------------------------------------ text */

static void *grab(SIZE_T bytes)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes ? bytes : 1);
}

static void *regrab(void *memory, SIZE_T bytes)
{
    return memory ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, memory, bytes) : grab(bytes);
}

static void release(void *memory)
{
    if (memory)
        HeapFree(GetProcessHeap(), 0, memory);
}

static WCHAR *copy(const WCHAR *text, SIZE_T count)
{
    WCHAR *out = grab((count + 1) * sizeof(WCHAR));
    if (out)
        memcpy(out, text, count * sizeof(WCHAR));
    return out;
}

static WCHAR lower(WCHAR c)
{
    return c >= L'A' && c <= L'Z' ? (WCHAR)(c + 32) : c;
}

static BOOL same(const WCHAR *a, const WCHAR *b)
{
    for (; *a && *b; a++, b++)
        if (lower(*a) != lower(*b))
            return FALSE;
    return *a == *b;
}

static BOOL starts(const WCHAR *text, const WCHAR *prefix)
{
    for (; *prefix; text++, prefix++)
        if (lower(*text) != lower(*prefix))
            return FALSE;
    return TRUE;
}

static BOOL is_blank(WCHAR c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

/* the text of a file as wide characters, whether it is utf-16 or in the code page. */
static WCHAR *read_text(const WCHAR *path, DWORD *error)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return nullptr;
    }
    DWORD size = GetFileSize(file, nullptr), got = 0;
    BYTE *bytes = grab((SIZE_T)size + 4);
    BOOL ok = size != INVALID_FILE_SIZE && bytes && ReadFile(file, bytes, size, &got, nullptr);
    *error = ok ? 0 : GetLastError();
    CloseHandle(file);
    if (!ok) {
        release(bytes);
        return nullptr;
    }
    WCHAR *text;
    if (got >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        text = copy((const WCHAR *)(bytes + 2), (got - 2) / sizeof(WCHAR));
    } else {
        UINT page = CP_ACP;
        DWORD skip = 0;
        if (got >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
            page = CP_UTF8;
            skip = 3;
        }
        int n = MultiByteToWideChar(page, 0, (LPCSTR)bytes + skip, (int)(got - skip), nullptr, 0);
        text = grab(((SIZE_T)n + 1) * sizeof(WCHAR));
        if (text)
            MultiByteToWideChar(page, 0, (LPCSTR)bytes + skip, (int)(got - skip), text, n);
    }
    release(bytes);
    if (!text)
        *error = ERROR_NOT_ENOUGH_MEMORY;
    return text;
}

/* ------------------------------------------------------------------ the definition */

typedef struct SYMBOL {
    WCHAR *Name;
    DWORD Offset;
} SYMBOL;

typedef struct ENTRY {
    WCHAR *Symbol, *Text;
    DWORD Language;
    BOOL Help;
} ENTRY;

typedef struct DEFINITION {
    WCHAR *Driver, *SymbolFile;
    SYMBOL *Symbols;
    DWORD SymbolCount, SymbolCapacity;
    ENTRY *Entries;
    DWORD EntryCount, EntryCapacity;
    DWORD Languages[32];
    DWORD LanguageCount;
} DEFINITION;

static void free_definition(DEFINITION *d)
{
    release(d->Driver);
    release(d->SymbolFile);
    for (DWORD i = 0; i < d->SymbolCount; i++)
        release(d->Symbols[i].Name);
    release(d->Symbols);
    for (DWORD i = 0; i < d->EntryCount; i++) {
        release(d->Entries[i].Symbol);
        release(d->Entries[i].Text);
    }
    release(d->Entries);
    memset(d, 0, sizeof(*d));
}

static DWORD hex_number(const WCHAR *text, SIZE_T length)
{
    DWORD value = 0;
    for (SIZE_T i = 0; i < length; i++) {
        WCHAR c = lower(text[i]);
        value = value * 16 + (c >= L'a' ? (DWORD)(c - L'a' + 10) : (DWORD)(c - L'0'));
    }
    return value;
}

/* the lines of the file, one at a time, without their breaks. */
static BOOL next_line(const WCHAR **cursor, const WCHAR **line, SIZE_T *length)
{
    const WCHAR *p = *cursor;
    if (!*p)
        return FALSE;
    *line = p;
    while (*p && *p != L'\n' && *p != L'\r')
        p++;
    *length = (SIZE_T)(p - *line);
    while (*p == L'\n' || *p == L'\r')
        p++;
    *cursor = p;
    return TRUE;
}

static DWORD read_symbols(DEFINITION *d, const WCHAR *path)
{
    DWORD error = 0;
    WCHAR *text = read_text(path, &error);
    if (!text)
        return error;
    const WCHAR *cursor = text, *line;
    SIZE_T length;
    while (next_line(&cursor, &line, &length)) {
        while (length && is_blank(*line)) {
            line++;
            length--;
        }
        if (length < 9 || !starts(line, L"#define"))
            continue;
        const WCHAR *p = line + 7, *end = line + length;
        while (p < end && is_blank(*p))
            p++;
        const WCHAR *name = p;
        while (p < end && !is_blank(*p))
            p++;
        SIZE_T name_length = (SIZE_T)(p - name);
        while (p < end && is_blank(*p))
            p++;
        DWORD value = 0;
        BOOL digits = FALSE;
        for (; p < end && *p >= L'0' && *p <= L'9'; p++) {
            value = value * 10 + (DWORD)(*p - L'0');
            digits = TRUE;
        }
        if (!name_length || !digits)
            continue;
        if (d->SymbolCount == d->SymbolCapacity) {
            d->SymbolCapacity = d->SymbolCapacity ? d->SymbolCapacity * 2 : 256;
            d->Symbols = regrab(d->Symbols, d->SymbolCapacity * sizeof(SYMBOL));
            if (!d->Symbols) {
                release(text);
                return ERROR_NOT_ENOUGH_MEMORY;
            }
        }
        d->Symbols[d->SymbolCount].Name = copy(name, name_length);
        d->Symbols[d->SymbolCount++].Offset = value;
    }
    release(text);
    return 0;
}

static DWORD parse_definition(DEFINITION *d, const WCHAR *path)
{
    DWORD error = 0;
    WCHAR *text = read_text(path, &error);
    if (!text)
        return error;
    enum { NONE, INFO, LANGUAGES, TEXT } section = NONE;
    const WCHAR *cursor = text, *line;
    SIZE_T length;
    while (next_line(&cursor, &line, &length)) {
        while (length && is_blank(*line)) {
            line++;
            length--;
        }
        while (length && is_blank(line[length - 1]))
            length--;
        if (!length || *line == L';')
            continue;
        if (*line == L'[') {
            WCHAR *name = copy(line + 1, length > 2 ? length - 2 : 0);
            section = same(name, L"info") ? INFO : same(name, L"languages") ? LANGUAGES : same(name, L"text") ? TEXT : NONE;
            release(name);
            continue;
        }
        const WCHAR *equals = line;
        while (equals < line + length && *equals != L'=')
            equals++;
        if (equals >= line + length)
            continue;
        SIZE_T key_length = (SIZE_T)(equals - line);
        while (key_length && is_blank(line[key_length - 1]))
            key_length--;
        const WCHAR *value = equals + 1;
        SIZE_T value_length = (SIZE_T)(line + length - value);
        while (value_length && is_blank(*value)) {
            value++;
            value_length--;
        }
        if (section == INFO) {
            WCHAR *key = copy(line, key_length);
            if (same(key, L"drivername") && !d->Driver)
                d->Driver = copy(value, value_length);
            else if (same(key, L"symbolfile") && !d->SymbolFile)
                d->SymbolFile = copy(value, value_length);
            release(key);
        } else if (section == LANGUAGES) {
            if (d->LanguageCount < 32 && key_length <= 4)
                d->Languages[d->LanguageCount++] = hex_number(line, key_length);
        } else if (section == TEXT) {
            /* SYMBOL_009_NAME or SYMBOL_009_HELP. */
            if (key_length < 10)
                continue;
            BOOL help = starts(line + key_length - 5, L"_help");
            if (!help && !starts(line + key_length - 5, L"_name"))
                continue;
            SIZE_T symbol_length = key_length - 9;
            if (line[symbol_length] != L'_')
                continue;
            if (d->EntryCount == d->EntryCapacity) {
                d->EntryCapacity = d->EntryCapacity ? d->EntryCapacity * 2 : 256;
                d->Entries = regrab(d->Entries, d->EntryCapacity * sizeof(ENTRY));
                if (!d->Entries) {
                    release(text);
                    return ERROR_NOT_ENOUGH_MEMORY;
                }
            }
            ENTRY *e = &d->Entries[d->EntryCount++];
            e->Symbol = copy(line, symbol_length);
            e->Language = hex_number(line + symbol_length + 1, 3);
            e->Help = help;
            e->Text = copy(value, value_length);
        }
    }
    release(text);
    if (!d->Driver || !*d->Driver)
        return ERROR_BAD_FORMAT;
    if (d->SymbolFile && *d->SymbolFile) {
        error = read_symbols(d, d->SymbolFile);
        if (error)
            return error;
    }
    return 0;
}

static BOOL symbol_offset(const DEFINITION *d, const WCHAR *name, DWORD *offset)
{
    for (DWORD i = 0; i < d->SymbolCount; i++) {
        if (same(d->Symbols[i].Name, name)) {
            *offset = d->Symbols[i].Offset;
            return TRUE;
        }
    }
    return FALSE;
}

/* ------------------------------------------------------------------ the registry */

static DWORD get_dword(HKEY key, const WCHAR *name, DWORD fallback)
{
    DWORD value = 0, size = sizeof(value), type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, (BYTE *)&value, &size) || type != REG_DWORD)
        return fallback;
    return value;
}

static LONG set_dword(HKEY key, const WCHAR *name, DWORD value)
{
    return RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
}

/* the numbers and the text of one language are appended to what its list holds. */
static LONG append_text(HKEY language_key, const WCHAR *list_name, const WCHAR *text, SIZE_T text_chars)
{
    DWORD size = 0, type = 0;
    if (RegQueryValueExW(language_key, list_name, nullptr, &type, nullptr, &size) || type != REG_MULTI_SZ)
        size = 0;
    SIZE_T old_chars = size / sizeof(WCHAR);
    BYTE *old = grab(size + sizeof(WCHAR));
    if (!old)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (size)
        RegQueryValueExW(language_key, list_name, nullptr, &type, old, &size);
    while (old_chars && !((WCHAR *)old)[old_chars - 1])
        old_chars--;
    /* what is there ends at its last character and the terminator after it; the new text follows that, then the end of the list. */
    if (old_chars)
        old_chars++;
    WCHAR *merged = grab((old_chars + text_chars + 2) * sizeof(WCHAR));
    if (!merged) {
        release(old);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    memcpy(merged, old, old_chars * sizeof(WCHAR));
    SIZE_T at = old_chars;
    memcpy(merged + at, text, text_chars * sizeof(WCHAR));
    at += text_chars;
    LONG status = RegSetValueExW(language_key, list_name, 0, REG_MULTI_SZ, (const BYTE *)merged, (DWORD)((at + 1) * sizeof(WCHAR)));
    release(merged);
    release(old);
    return status;
}

static DWORD load_definition(const WCHAR *ini)
{
    DEFINITION d = {};
    DWORD error = parse_definition(&d, ini);
    if (error) {
        free_definition(&d);
        return error;
    }
    HKEY perflib = nullptr, performance = nullptr;
    WCHAR path[400];
    HKEY service = nullptr;
    _snwprintf(path, RTL_NUMBER_OF(path), SERVICES L"\\%ls", d.Driver);
    LONG status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_ALL_ACCESS, &service);
    if (!status) {
        status = RegCreateKeyExW(service, L"Performance", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &performance, nullptr);
        RegCloseKey(service);
    }
    if (!status)
        status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, PERFLIB, 0, KEY_ALL_ACCESS, &perflib);
    if (status) {
        if (performance)
            RegCloseKey(performance);
        free_definition(&d);
        return (DWORD)status;
    }
    DWORD last_counter = get_dword(perflib, L"Last Counter", 1846), last_help = get_dword(perflib, L"Last Help", 1847);
    if (last_counter < 1846)
        last_counter = 1846;
    if (last_help < 1847)
        last_help = 1847;
    DWORD first_counter = (last_counter + 2) & ~1u, first_help = (last_help + 2) | 1u, highest = 0;
    for (DWORD i = 0; i < d.EntryCount; i++) {
        DWORD offset;
        if (symbol_offset(&d, d.Entries[i].Symbol, &offset) && offset > highest)
            highest = offset;
    }
    DWORD new_last_counter = first_counter + highest, new_last_help = first_help + highest;
    /* the text of each language the file lists goes to the list of names or of help. */
    for (DWORD l = 0; !status && l < d.LanguageCount; l++) {
        static const WCHAR digits[] = L"0123456789ABCDEF";
        WCHAR language_path[4] = {digits[(d.Languages[l] >> 8) & 15], digits[(d.Languages[l] >> 4) & 15], digits[d.Languages[l] & 15], 0};
        HKEY language = nullptr;
        status = RegCreateKeyExW(perflib, language_path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &language, nullptr);
        for (DWORD i = 0; !status && i < d.EntryCount; i++) {
            ENTRY *e = &d.Entries[i];
            DWORD offset;
            if (e->Language != d.Languages[l] || !symbol_offset(&d, e->Symbol, &offset))
                continue;
            WCHAR number[16];
            int n = _snwprintf(number, RTL_NUMBER_OF(number), L"%lu", e->Help ? first_help + offset : first_counter + offset);
            SIZE_T text_chars = (SIZE_T)n + 1 + wcslen(e->Text) + 1;
            WCHAR *pair = grab(text_chars * sizeof(WCHAR));
            if (!pair) {
                status = ERROR_NOT_ENOUGH_MEMORY;
                break;
            }
            memcpy(pair, number, ((SIZE_T)n + 1) * sizeof(WCHAR));
            memcpy(pair + n + 1, e->Text, (wcslen(e->Text) + 1) * sizeof(WCHAR));
            status = append_text(language, e->Help ? L"Help" : L"Counter", pair, text_chars);
            release(pair);
        }
        if (language)
            RegCloseKey(language);
    }
    if (!status)
        status = set_dword(performance, L"First Counter", first_counter);
    if (!status)
        status = set_dword(performance, L"Last Counter", new_last_counter);
    if (!status)
        status = set_dword(performance, L"First Help", first_help);
    if (!status)
        status = set_dword(performance, L"Last Help", new_last_help);
    if (!status)
        status = set_dword(perflib, L"Last Counter", new_last_counter);
    if (!status)
        status = set_dword(perflib, L"Last Help", new_last_help);
    RegCloseKey(performance);
    RegCloseKey(perflib);
    free_definition(&d);
    return (DWORD)status;
}

/* ------------------------------------------------------------------ the command line */

/* the last word of the line, the definition file or the name of the driver, with quotes taken off. */
static WCHAR *last_argument(const WCHAR *line)
{
    WCHAR *last = nullptr;
    const WCHAR *p = line;
    while (*p) {
        while (*p && is_blank(*p))
            p++;
        if (!*p)
            break;
        const WCHAR *start = p;
        BOOL quoted = *p == L'"';
        if (quoted)
            start = ++p;
        while (*p && (quoted ? *p != L'"' : !is_blank(*p)))
            p++;
        release(last);
        last = copy(start, (SIZE_T)(p - start));
        if (quoted && *p == L'"')
            p++;
    }
    return last;
}

static BOOL has_arguments(const WCHAR *line)
{
    /* the first word is the program's name. */
    const WCHAR *p = line;
    while (*p && is_blank(*p))
        p++;
    BOOL quoted = *p == L'"';
    if (quoted)
        p++;
    while (*p && (quoted ? *p != L'"' : !is_blank(*p)))
        p++;
    if (quoted && *p == L'"')
        p++;
    while (*p && is_blank(*p))
        p++;
    return *p != 0;
}

LOADPERF DWORD WINAPI LoadPerfCounterTextStringsW(LPWSTR command_line, BOOL quiet)
{
    if (!command_line || !has_arguments(command_line))
        return ERROR_INVALID_PARAMETER;
    WCHAR *file = last_argument(command_line);
    if (!file)
        return ERROR_INVALID_PARAMETER;
    DWORD error = load_definition(file);
    release(file);
    return error;
}

LOADPERF DWORD WINAPI UnloadPerfCounterTextStringsW(LPWSTR command_line, BOOL quiet)
{
    if (!command_line || !has_arguments(command_line))
        return ERROR_INVALID_PARAMETER;
    WCHAR *driver = last_argument(command_line);
    if (!driver)
        return ERROR_INVALID_PARAMETER;
    WCHAR path[400];
    _snwprintf(path, RTL_NUMBER_OF(path), SERVICES L"\\%ls\\Performance", driver);
    release(driver);
    HKEY key;
    LONG status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_ALL_ACCESS, &key);
    if (status)
        return (DWORD)status;
    /* the numbers are free to the driver again, the text of the other drivers is left where it is. */
    RegDeleteValueW(key, L"First Counter");
    RegDeleteValueW(key, L"Last Counter");
    RegDeleteValueW(key, L"First Help");
    RegDeleteValueW(key, L"Last Help");
    RegCloseKey(key);
    return ERROR_SUCCESS;
}

static WCHAR *widen(LPCSTR text)
{
    int n = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    WCHAR *out = grab((SIZE_T)n * sizeof(WCHAR) + sizeof(WCHAR));
    if (out)
        MultiByteToWideChar(CP_ACP, 0, text, -1, out, n);
    return out;
}

LOADPERF DWORD WINAPI LoadPerfCounterTextStringsA(LPSTR command_line, BOOL quiet)
{
    if (!command_line)
        return ERROR_INVALID_PARAMETER;
    WCHAR *wide = widen(command_line);
    if (!wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD error = LoadPerfCounterTextStringsW(wide, quiet);
    release(wide);
    return error;
}

LOADPERF DWORD WINAPI UnloadPerfCounterTextStringsA(LPSTR command_line, BOOL quiet)
{
    if (!command_line)
        return ERROR_INVALID_PARAMETER;
    WCHAR *wide = widen(command_line);
    if (!wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD error = UnloadPerfCounterTextStringsW(wide, quiet);
    release(wide);
    return error;
}

/* ------------------------------------------------------------------ the rest */

static HANDLE g_mutex;

LOADPERF BOOL WINAPI LpAcquireInstallationMutex(void)
{
    if (!g_mutex)
        g_mutex = CreateMutexW(nullptr, FALSE, L"Global\\MimePerflibInstallation");
    return g_mutex && WaitForSingleObject(g_mutex, 60000) <= WAIT_ABANDONED;
}

LOADPERF BOOL WINAPI LpReleaseInstallationMutex(void)
{
    return g_mutex && ReleaseMutex(g_mutex);
}

LOADPERF DWORD WINAPI InstallPerfDllW(LPCWSTR computer, LPCWSTR ini, ULONG_PTR flags)
{
    return ERROR_NOT_SUPPORTED;
}

LOADPERF DWORD WINAPI InstallPerfDllA(LPCSTR computer, LPCSTR ini, ULONG_PTR flags)
{
    return ERROR_NOT_SUPPORTED;
}

LOADPERF DWORD WINAPI UpdatePerfNameFilesW(LPWSTR counters, LPWSTR help, LPWSTR language, ULONG_PTR flags)
{
    return ERROR_NOT_SUPPORTED;
}

LOADPERF DWORD WINAPI UpdatePerfNameFilesA(LPSTR counters, LPSTR help, LPSTR language, ULONG_PTR flags)
{
    return ERROR_NOT_SUPPORTED;
}

LOADPERF DWORD WINAPI SetServiceAsTrustedW(LPCWSTR machine, LPCWSTR service)
{
    return ERROR_NOT_SUPPORTED;
}

LOADPERF DWORD WINAPI SetServiceAsTrustedA(LPCSTR machine, LPCSTR service)
{
    return ERROR_NOT_SUPPORTED;
}

LOADPERF DWORD WINAPI BackupPerfRegistryToFileW(LPCWSTR file, LPCWSTR comment)
{
    return ERROR_NOT_SUPPORTED;
}

LOADPERF DWORD WINAPI RestorePerfRegistryFromFileW(LPCWSTR file, LPCWSTR language)
{
    return ERROR_NOT_SUPPORTED;
}
