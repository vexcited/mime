/*
 * reg compare: two keys, or two values, side by side.
 *
 * the answer is the exit code: 0 identical, 2 different, 1 failed. what differs is listed with "<" for the first key and
 * ">" for the second, and what is equal with "=", according to the output option.
 */
#include "reg.h"

enum { MODE_NONE = 1, MODE_SAME = 2, MODE_DIFFERENT = 3, MODE_ALL = 4 };

typedef struct SIDE {
    TARGET Target;
} SIDE;

typedef struct COMPARISON {
    int Mode;
    BOOL Recursive, Different, Denied;
    DWORD View;
} COMPARISON;

static BOOL shows_different(const COMPARISON *c)
{
    return c->Mode == MODE_DIFFERENT || c->Mode == MODE_ALL;
}

static BOOL shows_same(const COMPARISON *c)
{
    return c->Mode == MODE_SAME || c->Mode == MODE_ALL;
}

static void print_key(PCWSTR parent, PCWSTR name, WCHAR mark)
{
    Out(L"%c Key: %s\\%s\n", mark, parent, name);
}

static void print_value(PCWSTR path, PCWSTR name, DWORD type, const BYTE *data, DWORD size, WCHAR mark)
{
    Out(L"%c Value: %s", mark, path);
    static const WCHAR separator[] = L"\\0";
    ShowValueRow(name, type, data, size, L" ", 2, FALSE, separator);
}

static LSTATUS read_data(HKEY key, PCWSTR name, DWORD *type, BYTE **data, DWORD *size)
{
    DWORD capacity = 128;
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
        if (status != ERROR_MORE_DATA)
            return status;
        capacity = length > capacity ? length : capacity * 2;
    }
    return ERROR_MORE_DATA;
}

static void show_one(HKEY key, PCWSTR path, PCWSTR name, WCHAR mark)
{
    DWORD type, size;
    BYTE *data;
    if (read_data(key, name, &type, &data, &size) == ERROR_SUCCESS) {
        print_value(path, name, type, data, size, mark);
        Release(data);
    }
}

/* one value of both keys. */
static DWORD compare_value(COMPARISON *c, HKEY left, PCWSTR left_path, HKEY right, PCWSTR right_path, PCWSTR name)
{
    if (c->Mode == MODE_NONE && c->Different)
        return 0;
    DWORD type_left, size_left, type_right, size_right;
    BYTE *data_left = nullptr, *data_right = nullptr;
    DWORD error = (DWORD)read_data(left, name, &type_left, &data_left, &size_left);
    if (!error)
        error = (DWORD)read_data(right, name, &type_right, &data_right, &size_right);
    if (!error) {
        BOOL same = type_left == type_right && size_left == size_right && memcmp(data_left, data_right, size_left) == 0;
        if (same) {
            if (shows_same(c))
                print_value(left_path, name, type_left, data_left, size_left, L'=');
        } else {
            c->Different = TRUE;
            if (shows_different(c)) {
                print_value(left_path, name, type_left, data_left, size_left, L'<');
                print_value(right_path, name, type_right, data_right, size_right, L'>');
            }
        }
    }
    Release(data_left);
    Release(data_right);
    return error;
}

typedef struct NAMES {
    PWSTR *Items;
    DWORD Count;
} NAMES;

static void free_names(NAMES *names)
{
    for (DWORD i = 0; i < names->Count; i++)
        Release(names->Items[i]);
    Release(names->Items);
    names->Items = nullptr;
    names->Count = 0;
}

/* the names of the values or of the subkeys, in the order of the registry. */
static DWORD list_names(HKEY key, BOOL keys, NAMES *names)
{
    DWORD count = 0, longest = 0;
    DWORD error = keys ? (DWORD)RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &count, &longest, nullptr, nullptr,
                                                 nullptr, nullptr, nullptr, nullptr)
                       : (DWORD)RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &count,
                                                 &longest, nullptr, nullptr, nullptr);
    if (error)
        return error;
    names->Items = Allocate((SIZE_T)(count + 1) * sizeof(PWSTR));
    if (!names->Items)
        return ERROR_NOT_ENOUGH_MEMORY;
    for (DWORD i = 0; i < count; i++) {
        PWSTR buffer = Allocate((longest + 2) * sizeof(WCHAR));
        if (!buffer)
            return ERROR_NOT_ENOUGH_MEMORY;
        DWORD length = longest + 1;
        error = keys ? (DWORD)RegEnumKeyExW(key, i, buffer, &length, nullptr, nullptr, nullptr, nullptr)
                     : (DWORD)RegEnumValueW(key, i, buffer, &length, nullptr, nullptr, nullptr, nullptr);
        if (error) {
            Release(buffer);
            return error;
        }
        names->Items[names->Count++] = buffer;
    }
    return 0;
}

static int find_name(const NAMES *names, PCWSTR name)
{
    for (DWORD i = 0; i < names->Count; i++)
        if (names->Items[i] && _wcsicmp(names->Items[i], name) == 0)
            return (int)i;
    return -1;
}

static DWORD compare_keys(COMPARISON *c, HKEY left, PCWSTR left_path, HKEY right, PCWSTR right_path)
{
    NAMES values_left = {}, values_right = {}, keys_left = {}, keys_right = {};
    DWORD error = list_names(left, FALSE, &values_left);
    if (!error)
        error = list_names(right, FALSE, &values_right);
    for (DWORD i = 0; i < values_left.Count && !error; i++) {
        int other = find_name(&values_right, values_left.Items[i]);
        if (other < 0)
            continue;
        error = compare_value(c, left, left_path, right, right_path, values_left.Items[i]);
        Release(values_right.Items[other]);
        values_right.Items[other] = nullptr;
        Release(values_left.Items[i]);
        values_left.Items[i] = nullptr;
    }
    for (DWORD i = 0; i < values_left.Count && !error; i++) {
        if (!values_left.Items[i])
            continue;
        c->Different = TRUE;
        if (shows_different(c))
            show_one(left, left_path, values_left.Items[i], L'<');
    }
    for (DWORD i = 0; i < values_right.Count && !error; i++) {
        if (!values_right.Items[i])
            continue;
        c->Different = TRUE;
        if (shows_different(c))
            show_one(right, right_path, values_right.Items[i], L'>');
    }
    if (!error && c->Recursive && !(c->Mode == MODE_NONE && c->Different)) {
        error = list_names(left, TRUE, &keys_left);
        if (!error)
            error = list_names(right, TRUE, &keys_right);
        for (DWORD i = 0; i < keys_left.Count && !error; i++) {
            int other = find_name(&keys_right, keys_left.Items[i]);
            if (other < 0)
                continue;
            PCWSTR name = keys_left.Items[i];
            if (shows_same(c))
                print_key(left_path, name, L'=');
            SIZE_T length_left = wcslen(left_path) + wcslen(name) + 2, length_right = wcslen(right_path) + wcslen(name) + 2;
            PWSTR child_left = Allocate(length_left * sizeof(WCHAR)), child_right = Allocate(length_right * sizeof(WCHAR));
            HKEY sub_left, sub_right;
            if (child_left && child_right) {
                _snwprintf(child_left, length_left, L"%s\\%s", left_path, name);
                _snwprintf(child_right, length_right, L"%s\\%s", right_path, name);
                DWORD open_left = (DWORD)RegOpenKeyExW(left, name, 0, KEY_READ | c->View, &sub_left);
                DWORD open_right = 1;
                if (!open_left)
                    open_right = (DWORD)RegOpenKeyExW(right, keys_right.Items[other], 0, KEY_READ | c->View, &sub_right);
                if (open_left || open_right) {
                    if (!open_left)
                        RegCloseKey(sub_left);
                    DWORD failed = open_left ? open_left : open_right;
                    if (failed == ERROR_ACCESS_DENIED)
                        c->Denied = TRUE;
                    else
                        error = failed;
                } else {
                    error = compare_keys(c, sub_left, child_left, sub_right, child_right);
                    RegCloseKey(sub_left);
                    RegCloseKey(sub_right);
                }
            } else {
                error = ERROR_NOT_ENOUGH_MEMORY;
            }
            Release(child_left);
            Release(child_right);
            Release(keys_right.Items[other]);
            keys_right.Items[other] = nullptr;
            Release(keys_left.Items[i]);
            keys_left.Items[i] = nullptr;
        }
        for (DWORD i = 0; i < keys_left.Count && !error; i++) {
            if (!keys_left.Items[i])
                continue;
            c->Different = TRUE;
            if (shows_different(c))
                print_key(left_path, keys_left.Items[i], L'<');
        }
        for (DWORD i = 0; i < keys_right.Count && !error; i++) {
            if (!keys_right.Items[i])
                continue;
            c->Different = TRUE;
            if (shows_different(c))
                print_key(right_path, keys_right.Items[i], L'>');
        }
    }
    free_names(&values_left);
    free_names(&values_right);
    free_names(&keys_left);
    free_names(&keys_right);
    return error;
}

int DoCompare(const ARGS *args)
{
    int argc = args->Count;
    PWSTR *argv = args->Items;
    if (argc == 3 && IsHelp(argv[2])) {
        Usage(OP_COMPARE);
        return 0;
    }
    if (argc < 4 || argc > 9)
        return SyntaxError(OP_COMPARE);
    TARGET left, right;
    if (!ParseKeyArgument(argv[2], OP_COMPARE, &left))
        return 1;
    PWSTR second = argv[3];
    PWSTR built = nullptr;
    if (wcslen(second) > 2 && second[0] == L'\\' && second[1] == L'\\' && !wcschr(second + 2, L'\\')) {
        /* a machine alone names the same key on that machine. */
        SIZE_T size = wcslen(second) + 1 + wcslen(left.Full) + 1;
        built = Allocate(size * sizeof(WCHAR));
        _snwprintf(built, size, L"%s\\%s", second, left.Full);
        second = built;
    }
    if (!ParseKeyArgument(second, OP_COMPARE, &right)) {
        Release(built);
        FreeTarget(&left);
        return 1;
    }
    Release(built);
    COMPARISON c = {MODE_DIFFERENT};
    PWSTR name = nullptr;
    BOOL bad = FALSE, have_mode = FALSE, have_s = FALSE;
    DWORD view = 0;
    for (int i = 4; i < argc && !bad; i++) {
        PCWSTR a = argv[i];
        if (IsSwitch(a, L"v")) {
            if (name || have_s || ++i >= argc)
                bad = TRUE;
            else
                name = DuplicateString(argv[i]);
        } else if (IsSwitch(a, L"ve")) {
            if (name || have_s)
                bad = TRUE;
            else
                name = DuplicateString(L"");
        } else if (IsSwitch(a, L"oa") || IsSwitch(a, L"od") || IsSwitch(a, L"os") || IsSwitch(a, L"on")) {
            if (name || have_mode)
                bad = TRUE;
            have_mode = TRUE;
            c.Mode = IsSwitch(a, L"oa") ? MODE_ALL : IsSwitch(a, L"od") ? MODE_DIFFERENT : IsSwitch(a, L"os") ? MODE_SAME : MODE_NONE;
        } else if (IsSwitch(a, L"s")) {
            if (name || have_s)
                bad = TRUE;
            have_s = c.Recursive = TRUE;
        } else if (!ParseView(a, &view)) {
            bad = TRUE;
        }
    }
    int code = 1;
    HKEY key_left = nullptr, key_right = nullptr;
    if (bad) {
        SyntaxError(OP_COMPARE);
        goto done;
    }
    left.View = right.View = c.View = view;
    DWORD error;
    if (!ConnectTarget(&left, &error) || !ConnectTarget(&right, &error)) {
        ReportError(error, OP_COMPARE);
        goto done;
    }
    if (left.RootKey == right.RootKey && _wcsicmp(left.Subkey, right.Subkey) == 0) {
        Err(L"ERROR: The registry entry is being compared with itself.\n");
        goto done;
    }
    error = (DWORD)RegOpenKeyExW(left.RootKey, left.Subkey, 0, KEY_READ | view, &key_left);
    if (error) {
        ReportError(error, OP_COMPARE);
        goto done;
    }
    error = (DWORD)RegOpenKeyExW(right.RootKey, right.Subkey, 0, KEY_READ | view, &key_right);
    if (error) {
        ReportError(error, OP_COMPARE);
        goto done;
    }
    if (name)
        error = compare_value(&c, key_left, left.Full, key_right, right.Full, name);
    else
        error = compare_keys(&c, key_left, left.Full, key_right, right.Full);
    if (error) {
        ReportError(error, OP_COMPARE);
        goto done;
    }
    OutText(c.Different ? L"\nResult Compared:  Different\n" : L"\nResult Compared:  Identical\n");
    ReportSuccess();
    code = c.Different ? 2 : 0;
    if (c.Denied)
        Err(L"\nNot all keys were successfully accessed. You may have insufficient privileges to perform this operation.\n");
done:
    if (key_left)
        RegCloseKey(key_left);
    if (key_right)
        RegCloseKey(key_right);
    Release(name);
    FreeTarget(&left);
    FreeTarget(&right);
    return code;
}
