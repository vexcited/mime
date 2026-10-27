/*
 * reg query: the values and subkeys of a key, and the search through them.
 *
 * the listing is the one of the original, line by line: a blank line first, the key's name above its values the first
 * time one is shown, a blank line after the values, then the full name of every subkey. with /s each subkey's name is
 * followed by its values and a blank line, even when it has none. a search shows only what matches and ends with a line
 * that counts the matches; no match is exit code 1.
 */
#include "reg.h"

#define SCOPE_KEYS 1
#define SCOPE_NAMES 2
#define SCOPE_DATA 4

typedef struct QUERY {
    TARGET Target;
    PWSTR ValueName; /* NULL, "" for /ve, else the name or a pattern of names. */
    BOOL Recursive, Verbose, Exact, CaseSensitive;
    PWSTR Search;
    WCHAR Separator[3];
    DWORD Types[32];
    int TypeCount;
    int Scope;
    long Count;
} QUERY;

/* ------------------------------------------------------------------ reading values */

/* the data of a value with four zero bytes behind it. an unset default value reads as the text "(value not set)". */
static LSTATUS read_value(HKEY key, PCWSTR name, DWORD *type, BYTE **data, DWORD *size)
{
    DWORD capacity = 256;
    for (int attempt = 0; attempt < 8; attempt++) {
        BYTE *buffer = Allocate(capacity + 4);
        if (!buffer)
            return ERROR_NOT_ENOUGH_MEMORY;
        DWORD length = capacity;
        LSTATUS status = RegQueryValueExW(key, name, nullptr, type, buffer, &length);
        if (status == ERROR_SUCCESS) {
            *data = buffer;
            *size = length;
            return ERROR_SUCCESS;
        }
        Release(buffer);
        if (status == ERROR_MORE_DATA) {
            capacity = length > capacity ? length : capacity * 2;
            continue;
        }
        if (status == ERROR_FILE_NOT_FOUND && !*name) {
            static const WCHAR unset[] = L"(value not set)";
            *data = Allocate(sizeof(unset) + 4);
            if (!*data)
                return ERROR_NOT_ENOUGH_MEMORY;
            memcpy(*data, unset, sizeof(unset));
            *type = REG_SZ;
            *size = sizeof(unset);
            return ERROR_SUCCESS;
        }
        return status;
    }
    return ERROR_MORE_DATA;
}

void ShowValueRow(PCWSTR name, DWORD type, const BYTE *data, DWORD size, PCWSTR separator, int indent, BOOL verbose,
                  PCWSTR multi_separator)
{
    if (indent)
        Out(L"%*s", indent, L" ");
    Out(L"%s%s", *name ? name : L"(Default)", separator);
    if (verbose)
        Out(L"%s (%d)", TypeName(type), (int)type);
    else
        OutText(TypeName(type));
    OutText(separator);
    if (size) {
        const WCHAR *text = (const WCHAR *)data;
        if (type == REG_SZ || type == REG_EXPAND_SZ) {
            OutText(text);
        } else if (type == REG_MULTI_SZ) {
            BOOL after_string = FALSE;
            const WCHAR *end = (const WCHAR *)(data + size);
            while (text < end) {
                if (*text) {
                    if (after_string)
                        OutText(multi_separator);
                    OutText(text);
                    text += wcslen(text);
                } else {
                    after_string = TRUE;
                    text++;
                }
            }
        } else if (type == REG_QWORD) {
            ULONGLONG value;
            memcpy(&value, data, sizeof(value));
            Out(L"0x%I64x", value);
        } else if (type == REG_DWORD || type == REG_DWORD_BIG_ENDIAN) {
            DWORD value;
            memcpy(&value, data, sizeof(value));
            Out(L"0x%x", value);
        } else {
            for (DWORD i = 0; i < size; i++)
                Out(L"%02X", data[i]);
        }
    }
    OutText(L"\n");
}

/* ------------------------------------------------------------------ matching */

static BOOL text_matches(const QUERY *q, PCWSTR text)
{
    if (q->Exact)
        return q->CaseSensitive ? wcscmp(text, q->Search) == 0 : _wcsicmp(text, q->Search) == 0;
    SIZE_T length = wcslen(q->Search);
    PWSTR pattern = Allocate((length + 3) * sizeof(WCHAR));
    if (!pattern)
        return FALSE;
    pattern[0] = L'*';
    memcpy(pattern + 1, q->Search, length * sizeof(WCHAR));
    pattern[length + 1] = L'*';
    BOOL result = WildcardMatch(text, pattern, q->CaseSensitive);
    Release(pattern);
    return result;
}

/* the text a value's data is searched as. */
static PWSTR data_text(const QUERY *q, DWORD type, const BYTE *data, DWORD size)
{
    SIZE_T capacity = size * 2 + 64;
    PWSTR text = Allocate(capacity * sizeof(WCHAR));
    if (!text)
        return nullptr;
    if (type == REG_SZ || type == REG_EXPAND_SZ) {
        for (DWORD i = 0; i < size / 2 && ((const WCHAR *)data)[i]; i++)
            text[i] = ((const WCHAR *)data)[i];
    } else if (type == REG_DWORD || type == REG_DWORD_BIG_ENDIAN) {
        DWORD value = 0;
        memcpy(&value, data, size < 4 ? size : 4);
        if (_wcsnicmp(q->Search, L"0x", 2) == 0)
            _snwprintf(text, capacity, L"0x%x", value);
        else
            _snwprintf(text, capacity, L"%d", (int)value);
    } else if (type == REG_MULTI_SZ) {
        BOOL after_string = FALSE;
        const WCHAR *p = (const WCHAR *)data, *end = (const WCHAR *)(data + size);
        SIZE_T used = 0;
        while (p < end) {
            if (*p) {
                SIZE_T n = wcslen(p);
                if (after_string) {
                    SIZE_T s = wcslen(q->Separator);
                    if (used + s < capacity) {
                        memcpy(text + used, q->Separator, s * sizeof(WCHAR));
                        used += s;
                    }
                }
                if (used + n < capacity) {
                    memcpy(text + used, p, n * sizeof(WCHAR));
                    used += n;
                }
                p += n;
            } else {
                after_string = TRUE;
                p++;
            }
        }
    } else {
        for (DWORD i = 0; i < size; i++)
            _snwprintf(text + i * 2, 3, L"%02X", data[i]);
    }
    return text;
}

/* ------------------------------------------------------------------ the listing */

/* the full name of a key is shown above its first value, and once. */
typedef struct FRAME {
    PCWSTR Path;
    BOOL HeaderPending;
} FRAME;

static BOOL filtered(const QUERY *q)
{
    return q->ValueName != nullptr || q->TypeCount != 0;
}

static BOOL type_wanted(const QUERY *q, DWORD type)
{
    if (!q->TypeCount)
        return TRUE;
    for (int i = 0; i < q->TypeCount; i++)
        if (q->Types[i] == type)
            return TRUE;
    return FALSE;
}

static LSTATUS list_values(QUERY *q, HKEY key, FRAME *frame)
{
    DWORD values = 0, longest = 0;
    LSTATUS status = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &values, &longest,
                                      nullptr, nullptr, nullptr);
    if (status != ERROR_SUCCESS)
        return status;
    PWSTR name = Allocate((longest + 2) * sizeof(WCHAR));
    if (!name)
        return ERROR_NOT_ENOUGH_MEMORY;
    for (DWORD index = 0; index < values; index++) {
        DWORD length = longest + 1, type;
        status = RegEnumValueW(key, index, name, &length, nullptr, &type, nullptr, nullptr);
        if (status != ERROR_SUCCESS)
            break;
        if (q->ValueName && *q->ValueName && !WildcardMatch(name, q->ValueName, FALSE))
            continue;
        if (q->ValueName && !*q->ValueName && *name)
            continue;
        if (!type_wanted(q, type))
            continue;
        BOOL show = TRUE;
        BYTE *data = nullptr;
        DWORD size = 0;
        BOOL have_data = FALSE;
        if (q->Search) {
            BOOL hit;
            if (q->Scope & SCOPE_NAMES)
                hit = text_matches(q, name);
            else
                hit = filtered(q);
            if (hit && (q->Scope & SCOPE_NAMES) && !filtered(q))
                q->Count++;
            if (hit) {
                show = TRUE;
            } else if (q->Scope == SCOPE_NAMES) {
                show = FALSE;
            } else if (q->Scope & SCOPE_DATA) {
                status = read_value(key, name, &type, &data, &size);
                if (status != ERROR_SUCCESS) {
                    status = ERROR_SUCCESS;
                    continue;
                }
                have_data = TRUE;
                PWSTR text = data_text(q, type, data, size);
                show = text && text_matches(q, text);
                Release(text);
                if (show)
                    q->Count++;
            } else {
                show = FALSE;
            }
        }
        if (show) {
            if (!have_data) {
                status = read_value(key, name, &type, &data, &size);
                if (status != ERROR_SUCCESS) {
                    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_ACCESS_DENIED) {
                        status = ERROR_SUCCESS;
                        continue;
                    }
                    break;
                }
            }
            if (frame->HeaderPending) {
                Out(L"%s\n", frame->Path);
                frame->HeaderPending = FALSE;
            }
            if (filtered(q))
                q->Count++;
            ShowValueRow(name, type, data, size, L"    ", 4, q->Verbose, q->Separator);
        }
        Release(data);
    }
    Release(name);
    /* the values of a key whose name is already shown, or that was just shown, end with a blank line. */
    if (!frame->HeaderPending)
        OutText(L"\n");
    return status;
}

static LSTATUS list_key(QUERY *q, HKEY key, PCWSTR path, BOOL header_pending, BOOL values_too)
{
    LSTATUS status = ERROR_SUCCESS;
    if (values_too && !(q->Scope == SCOPE_KEYS && !filtered(q))) {
        FRAME frame = {path, header_pending};
        status = list_values(q, key, &frame);
        if (status != ERROR_SUCCESS)
            return status;
    }
    DWORD subkeys = 0, longest = 0;
    status = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, &longest, nullptr, nullptr, nullptr, nullptr,
                              nullptr, nullptr);
    if (status != ERROR_SUCCESS || !subkeys)
        return status;
    SIZE_T path_length = wcslen(path);
    PWSTR child = Allocate((path_length + 1 + longest + 2) * sizeof(WCHAR));
    if (!child)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(child, path, path_length * sizeof(WCHAR));
    child[path_length] = L'\\';
    PWSTR name = child + path_length + 1;
    for (DWORD index = 0; index < subkeys; index++) {
        DWORD length = longest + 1;
        status = RegEnumKeyExW(key, index, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (status != ERROR_SUCCESS)
            break;
        BOOL hit;
        if (q->Scope & SCOPE_KEYS)
            hit = text_matches(q, name);
        else
            hit = q->Scope == 0 && q->ValueName == nullptr;
        if ((q->Scope & SCOPE_KEYS) && !hit && q->Scope == SCOPE_KEYS && !q->Recursive)
            continue;
        BOOL printed = FALSE;
        if (hit && !q->ValueName && !q->TypeCount) {
            q->Count++;
            Out(L"%s\n", child);
            printed = TRUE;
        }
        if (q->Recursive) {
            HKEY sub;
            if (RegOpenKeyExW(key, name, 0, KEY_READ | q->Target.View, &sub) == ERROR_SUCCESS) {
                status = list_key(q, sub, child, !printed, TRUE);
                RegCloseKey(sub);
                if (status != ERROR_SUCCESS)
                    break;
            }
        }
    }
    Release(child);
    return status;
}

/* ------------------------------------------------------------------ the command line */

/* an option that /v does not take as the name of a value. */
static BOOL is_query_switch(PCWSTR text)
{
    static const WCHAR *const names[] = {L"z", L"ve", L"f", L"k", L"d", L"c", L"e", L"s", L"t", L"se"};
    if (text[0] != L'/')
        return FALSE;
    for (int i = 0; i < 10; i++)
        if (_wcsicmp(text + 1, names[i]) == 0)
            return TRUE;
    return FALSE;
}

static BOOL parse_types(QUERY *q, PCWSTR list)
{
    PWSTR copy = DuplicateString(list);
    if (!copy)
        return FALSE;
    BOOL ok = TRUE;
    for (PWSTR part = copy; part && ok;) {
        PWSTR comma = wcschr(part, L',');
        if (comma)
            *comma = 0;
        int type = ParseTypeName(part);
        if (type < 0) {
            ULONGLONG number;
            BOOL overflow;
            PCWSTR end;
            if (!ParseNumber(part, 10, &number, &overflow, &end, FALSE) || *end || overflow)
                ok = FALSE;
            else
                type = (int)number;
        }
        if (ok) {
            if (q->TypeCount >= (int)RTL_NUMBER_OF(q->Types))
                ok = FALSE;
            else
                q->Types[q->TypeCount++] = (DWORD)type;
        }
        part = comma ? comma + 1 : nullptr;
    }
    Release(copy);
    return ok;
}

static void free_query(QUERY *q)
{
    FreeTarget(&q->Target);
    Release(q->ValueName);
    Release(q->Search);
}

int DoQuery(const ARGS *args)
{
    int argc = args->Count;
    PWSTR *argv = args->Items;
    if (argc < 3 || argc > 13)
        return SyntaxError(OP_QUERY);
    if (IsHelp(argv[2])) {
        if (argc == 3) {
            Usage(OP_QUERY);
            return 0;
        }
        return SyntaxError(OP_QUERY);
    }
    QUERY q;
    memset(&q, 0, sizeof(q));
    wcscpy(q.Separator, L"\\0");
    if (!ParseKeyArgument(argv[2], OP_QUERY, &q.Target))
        return 1;
    BOOL keys_only = FALSE, names_only = FALSE, data_only = FALSE, have_s = FALSE, have_z = FALSE, have_se = FALSE;
    BOOL have_v = FALSE, have_ve = FALSE, have_c = FALSE, have_e = FALSE;
    int code = 0;
    for (int i = 3; i < argc && !code; i++) {
        PCWSTR a = argv[i];
        if (IsSwitch(a, L"f")) {
            if (q.Search || ++i >= argc) {
                code = SyntaxError(OP_QUERY);
                break;
            }
            q.Search = DuplicateString(argv[i]);
        } else if (IsSwitch(a, L"k")) {
            if (keys_only)
                code = SyntaxError(OP_QUERY);
            keys_only = TRUE;
        } else if (IsSwitch(a, L"v")) {
            if (have_v || have_ve) {
                code = SyntaxError(OP_QUERY);
                break;
            }
            have_v = TRUE;
            if (i + 1 < argc && (wcslen(argv[i + 1]) < 2 || !is_query_switch(argv[i + 1]))) {
                q.ValueName = DuplicateString(argv[++i]);
            } else {
                names_only = TRUE;
            }
        } else if (IsSwitch(a, L"ve")) {
            if (have_v || have_ve) {
                code = SyntaxError(OP_QUERY);
                break;
            }
            have_ve = TRUE;
            q.ValueName = DuplicateString(L"");
        } else if (IsSwitch(a, L"d")) {
            if (data_only)
                code = SyntaxError(OP_QUERY);
            data_only = TRUE;
        } else if (IsSwitch(a, L"c")) {
            if (have_c)
                code = SyntaxError(OP_QUERY);
            have_c = q.CaseSensitive = TRUE;
        } else if (IsSwitch(a, L"e")) {
            if (have_e)
                code = SyntaxError(OP_QUERY);
            have_e = q.Exact = TRUE;
        } else if (IsSwitch(a, L"z")) {
            if (have_z)
                code = SyntaxError(OP_QUERY);
            have_z = q.Verbose = TRUE;
        } else if (IsSwitch(a, L"s")) {
            if (have_s)
                code = SyntaxError(OP_QUERY);
            have_s = q.Recursive = TRUE;
        } else if (IsSwitch(a, L"se")) {
            if (have_se || ++i >= argc || wcslen(argv[i]) != 1) {
                code = SyntaxError(OP_QUERY);
                break;
            }
            have_se = TRUE;
            wcscpy(q.Separator, argv[i]);
        } else if (IsSwitch(a, L"t")) {
            if (q.TypeCount || ++i >= argc || !parse_types(&q, argv[i]))
                code = SyntaxError(OP_QUERY);
        } else if (!ParseView(a, &q.Target.View)) {
            code = SyntaxError(OP_QUERY);
        }
    }
    if (!code) {
        if (keys_only)
            q.Scope |= SCOPE_KEYS;
        if (names_only)
            q.Scope |= SCOPE_NAMES;
        if (data_only)
            q.Scope |= SCOPE_DATA;
        if (q.Search) {
            if (!q.Scope)
                q.Scope = q.ValueName ? (SCOPE_KEYS | SCOPE_DATA) : (SCOPE_KEYS | SCOPE_NAMES | SCOPE_DATA);
        } else if (q.Scope || q.Exact || q.CaseSensitive) {
            /* the search options need something to search for. */
            code = SyntaxError(OP_QUERY);
        }
    }
    if (code) {
        free_query(&q);
        return 1;
    }

    DWORD error;
    if (!ConnectTarget(&q.Target, &error)) {
        code = ReportError(error, OP_QUERY);
        free_query(&q);
        return code;
    }
    HKEY key;
    LSTATUS status = RegOpenKeyExW(q.Target.RootKey, q.Target.Subkey, 0, KEY_READ | q.Target.View, &key);
    if (status != ERROR_SUCCESS) {
        code = ReportError((DWORD)status, OP_QUERY);
        free_query(&q);
        return code;
    }
    OutText(L"\n");
    BOOL searching;
    if (q.Search || !q.ValueName || q.TypeCount || q.Recursive) {
        status = list_key(&q, key, q.Target.Full, TRUE, TRUE);
        searching = q.Search || q.ValueName || q.TypeCount;
    } else if (!*q.ValueName || !FindAny(q.ValueName, L"*?")) {
        DWORD type, size;
        BYTE *data;
        status = read_value(key, q.ValueName, &type, &data, &size);
        searching = FALSE;
        if (status == ERROR_SUCCESS) {
            Out(L"%s\n", q.Target.Full);
            ShowValueRow(q.ValueName, type, data, size, L"    ", 4, q.Verbose, q.Separator);
            Release(data);
            OutText(L"\n");
        }
    } else {
        FRAME frame = {q.Target.Full, TRUE};
        status = list_values(&q, key, &frame);
        searching = TRUE;
    }
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        code = ReportError((DWORD)status, OP_QUERY);
    } else if (searching) {
        Out(L"End of search: %ld match(es) found.\n", q.Count);
        code = q.Count == 0;
    }
    free_query(&q);
    return code;
}
