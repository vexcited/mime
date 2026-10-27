/*
 * reg add, delete, copy, save, restore, load, unload and flags.
 */
#include "reg.h"
#include <mime/ntapi.h>

WINBASEAPI LSTATUS WINAPI RegLoadKeyW(HKEY key, LPCWSTR subkey, LPCWSTR file);
WINBASEAPI LSTATUS WINAPI RegUnLoadKeyW(HKEY key, LPCWSTR subkey);

#define ACCESS_WRITE_EXTRA 0x0002001F
#define ACCESS_FULL 0x000F003F

static const WCHAR *const k_default_name = L"(Default)";

/* ------------------------------------------------------------------ add */

/* the bytes of a value from its text, or the error that the text is wrong: 121 numeric, 122 hex, 123 value. */
static DWORD build_data(DWORD type, PCWSTR text, PCWSTR separator, BYTE **bytes, DWORD *size)
{
    *bytes = nullptr;
    *size = 0;
    SIZE_T length = wcslen(text);
    switch (type) {
    case REG_BINARY: {
        BYTE *data = Allocate(length / 2 + 2);
        if (!data)
            return ERROR_NOT_ENOUGH_MEMORY;
        DWORD n = 0;
        const WCHAR *p = text;
        /* an odd number of digits has a leading zero. */
        BOOL odd = length & 1;
        while (*p) {
            WCHAR pair[3];
            pair[0] = odd ? L'0' : *p++;
            odd = FALSE;
            pair[1] = *p++;
            pair[2] = 0;
            ULONGLONG value;
            BOOL overflow;
            PCWSTR end;
            if (!ParseNumber(pair, 16, &value, &overflow, &end, FALSE) || *end || pair[1] == L'x' || pair[1] == L'X') {
                Release(data);
                return 122;
            }
            data[n++] = (BYTE)value;
        }
        *bytes = data;
        *size = n;
        return 0;
    }
    case REG_DWORD:
    case REG_DWORD_BIG_ENDIAN: {
        BOOL hex = _wcsnicmp(text, L"0x", 2) == 0;
        ULONGLONG value;
        BOOL overflow;
        PCWSTR end;
        if (!ParseNumber(text, hex ? 16 : 10, &value, &overflow, &end, FALSE) || *end || overflow || value > 0xFFFFFFFF ||
            wcschr(text, L'-'))
            return 121;
        DWORD *data = Allocate(sizeof(DWORD));
        if (!data)
            return ERROR_NOT_ENOUGH_MEMORY;
        *data = (DWORD)value;
        *bytes = (BYTE *)data;
        *size = 4;
        return 0;
    }
    case REG_QWORD: {
        BOOL hex = _wcsnicmp(text, L"0x", 2) == 0;
        ULONGLONG value;
        BOOL overflow;
        PCWSTR end;
        if (!ParseNumber(text, hex ? 16 : 10, &value, &overflow, &end, TRUE) || *end || overflow)
            return 121;
        ULONGLONG *data = Allocate(sizeof(ULONGLONG));
        if (!data)
            return ERROR_NOT_ENOUGH_MEMORY;
        *data = value;
        *bytes = (BYTE *)data;
        *size = 8;
        return 0;
    }
    case REG_MULTI_SZ: {
        SIZE_T separator_length = wcslen(separator);
        WCHAR *data = Allocate((length + 3) * sizeof(WCHAR));
        if (!data)
            return ERROR_NOT_ENOUGH_MEMORY;
        WCHAR *out = data;
        if (length == separator_length && _wcsicmp(text, separator) == 0) {
            /* nothing but the separator is an empty list. */
            *bytes = (BYTE *)data;
            *size = 2 * sizeof(WCHAR);
            return 0;
        }
        SIZE_T at = 0;
        while (at < length) {
            const WCHAR *next = wcsstr(text + at, separator);
            SIZE_T end = next ? (SIZE_T)(next - text) : length;
            if (end == at && next) {
                Release(data);
                return 123;
            }
            memcpy(out, text + at, (end - at) * sizeof(WCHAR));
            out += end - at;
            *out++ = 0;
            at = end + separator_length;
            if (!next)
                break;
        }
        *out++ = 0;
        *bytes = (BYTE *)data;
        *size = (DWORD)((out - data) * sizeof(WCHAR));
        return 0;
    }
    default: {
        WCHAR *data = Allocate((length + 1) * sizeof(WCHAR));
        if (!data)
            return ERROR_NOT_ENOUGH_MEMORY;
        memcpy(data, text, length * sizeof(WCHAR));
        *bytes = (BYTE *)data;
        *size = (DWORD)((length + 1) * sizeof(WCHAR));
        return 0;
    }
    }
}

static int bad_data(DWORD error)
{
    if (error == 121)
        Err(L"ERROR: Invalid syntax. Specify valid numeric value for '/d'.\nType \"REG ADD /?\" for usage.\n");
    else if (error == 122)
        Err(L"ERROR: Invalid syntax. Specify valid hex value for '/d'.\nType \"REG ADD /?\" for usage.\n");
    else if (error == 123)
        Err(L"ERROR: Invalid value specified for '/d'.\nType \"REG ADD /?\" for usage.\n");
    else
        return ReportError(error, OP_ADD);
    return 1;
}

int DoAdd(const ARGS *args)
{
    int argc = args->Count;
    PWSTR *argv = args->Items;
    if (argc < 3)
        return SyntaxError(OP_ADD);
    if (IsHelp(argv[2])) {
        if (argc == 3) {
            Usage(OP_ADD);
            return 0;
        }
        return SyntaxError(OP_ADD);
    }
    TARGET target;
    if (!ParseKeyArgument(argv[2], OP_ADD, &target))
        return 1;
    PWSTR name = nullptr, data_text = nullptr;
    DWORD type = REG_SZ;
    BOOL have_type = FALSE, have_separator = FALSE, force = FALSE, bad = FALSE;
    WCHAR separator[3] = L"\\0";
    for (int i = 3; i < argc && !bad; i++) {
        PCWSTR a = argv[i];
        if (IsSwitch(a, L"v")) {
            if (name || ++i >= argc) {
                bad = TRUE;
            } else {
                name = DuplicateString(argv[i]);
                TrimSpaces(name);
            }
        } else if (IsSwitch(a, L"ve")) {
            if (name)
                bad = TRUE;
            else
                name = DuplicateString(L"");
        } else if (IsSwitch(a, L"t")) {
            if (have_type || ++i >= argc) {
                bad = TRUE;
            } else {
                int t = ParseTypeName(argv[i]);
                if (t < 0) {
                    ULONGLONG number;
                    BOOL overflow;
                    PCWSTR end;
                    if (!ParseNumber(argv[i], 10, &number, &overflow, &end, TRUE) || *end || overflow)
                        bad = TRUE;
                    else
                        t = (int)number;
                }
                type = (DWORD)t;
                have_type = TRUE;
                if (have_separator && type != REG_MULTI_SZ)
                    bad = TRUE;
            }
        } else if (IsSwitch(a, L"s")) {
            if (have_separator || (have_type && type != REG_MULTI_SZ) || ++i >= argc || wcslen(argv[i]) != 1) {
                bad = TRUE;
            } else {
                have_separator = TRUE;
                wcscpy(separator, argv[i]);
            }
        } else if (IsSwitch(a, L"d")) {
            if (data_text || ++i >= argc)
                bad = TRUE;
            else
                data_text = DuplicateString(argv[i]);
        } else if (IsSwitch(a, L"f")) {
            if (force)
                bad = TRUE;
            force = TRUE;
        } else if (!ParseView(a, &target.View)) {
            bad = TRUE;
        }
    }
    if (have_separator && !have_type)
        bad = TRUE;
    int code = 1;
    BYTE *bytes = nullptr;
    HKEY key = nullptr;
    if (bad) {
        SyntaxError(OP_ADD);
        goto done;
    }
    if (!name)
        name = DuplicateString(L"");
    if (!data_text)
        data_text = DuplicateString(L"");
    DWORD error;
    if (!ConnectTarget(&target, &error)) {
        ReportError(error, OP_ADD);
        goto done;
    }
    error = (DWORD)RegCreateKeyExW(target.RootKey, target.Subkey, 0, nullptr, 0, ACCESS_WRITE_EXTRA | target.View,
                                   nullptr, &key, nullptr);
    if (error != ERROR_SUCCESS) {
        ReportError(error, OP_ADD);
        goto done;
    }
    error = (DWORD)RegQueryValueExW(key, name, nullptr, nullptr, nullptr, nullptr);
    if (error == ERROR_SUCCESS) {
        int answer;
        do
            answer = Prompt(L"Value %s exists, overwrite(Yes/No)? ", name, L"YNA", force);
        while (answer > 2);
        if (answer != 1) {
            ReportPlain(ERROR_CANCELLED, 1);
            code = 0;
            goto done;
        }
    } else if (error != ERROR_FILE_NOT_FOUND) {
        ReportError(error, OP_ADD);
        goto done;
    }
    DWORD size;
    error = build_data(type, data_text, separator, &bytes, &size);
    if (error) {
        bad_data(error);
        goto done;
    }
    error = (DWORD)RegSetValueExW(key, name, 0, type, bytes, size);
    if (error != ERROR_SUCCESS) {
        ReportError(error, OP_ADD);
        goto done;
    }
    ReportSuccess();
    code = 0;
done:
    if (key)
        RegCloseKey(key);
    Release(bytes);
    Release(name);
    Release(data_text);
    FreeTarget(&target);
    return code;
}

/* ------------------------------------------------------------------ delete */

enum { DELETE_PARTIAL = 0x80040101 };

/* a key with everything below it; DELETE_PARTIAL when only some of it went. */
static DWORD delete_key_tree(HKEY root, PCWSTR subkey, DWORD view)
{
    HKEY key;
    DWORD error = (DWORD)RegOpenKeyExW(root, subkey, 0, ACCESS_FULL | view, &key);
    if (error != ERROR_SUCCESS)
        return error;
    DWORD subkeys = 0, longest = 0;
    error = (DWORD)RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, &longest, nullptr, nullptr, nullptr,
                                    nullptr, nullptr, nullptr);
    if (error != ERROR_SUCCESS) {
        RegCloseKey(key);
        return error;
    }
    if (subkeys) {
        PWSTR names = Allocate((SIZE_T)subkeys * (longest + 2) * sizeof(WCHAR));
        if (!names) {
            RegCloseKey(key);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        DWORD found = 0;
        for (DWORD i = 0; i < subkeys; i++) {
            DWORD length = longest + 1;
            if (RegEnumKeyExW(key, i, names + (SIZE_T)i * (longest + 2), &length, nullptr, nullptr, nullptr, nullptr) ==
                ERROR_SUCCESS)
                found++;
        }
        DWORD deleted = 0, first = 0;
        for (DWORD i = 0; i < found; i++) {
            DWORD result = delete_key_tree(key, names + (SIZE_T)i * (longest + 2), view);
            if (result == 0)
                deleted++;
            else if (!first)
                first = result;
        }
        Release(names);
        RegCloseKey(key);
        if (deleted != subkeys)
            return deleted ? (DWORD)DELETE_PARTIAL : first;
    } else {
        RegCloseKey(key);
    }
    return (DWORD)(view ? RegDeleteKeyExW(root, subkey, view, 0) : RegDeleteKeyW(root, subkey));
}

static int finish_delete(DWORD error, BOOL top)
{
    if (!top)
        return error != 0;
    if (error == 0) {
        ReportSuccess();
        return 0;
    }
    if (error == (DWORD)DELETE_PARTIAL) {
        Err(L"ERROR: Delete request is partially completed.\n");
        return 1;
    }
    return ReportError(error, OP_DELETE);
}

int DoDelete(const ARGS *args)
{
    int argc = args->Count;
    PWSTR *argv = args->Items;
    if (argc < 3 || argc > 7)
        return SyntaxError(OP_DELETE);
    if (IsHelp(argv[2])) {
        if (argc == 3) {
            Usage(OP_DELETE);
            return 0;
        }
        return SyntaxError(OP_DELETE);
    }
    TARGET target;
    if (!ParseKeyArgument(argv[2], OP_DELETE, &target))
        return 1;
    PWSTR name = nullptr;
    BOOL all_values = FALSE, force = FALSE, bad = FALSE, have_selection = FALSE;
    for (int i = 3; i < argc && !bad; i++) {
        PCWSTR a = argv[i];
        if (IsSwitch(a, L"v")) {
            if (have_selection || ++i >= argc) {
                bad = TRUE;
            } else {
                have_selection = TRUE;
                name = DuplicateString(argv[i]);
            }
        } else if (IsSwitch(a, L"ve")) {
            if (have_selection)
                bad = TRUE;
            have_selection = TRUE;
            name = DuplicateString(L"");
        } else if (IsSwitch(a, L"va")) {
            if (have_selection)
                bad = TRUE;
            have_selection = TRUE;
            all_values = TRUE;
        } else if (IsSwitch(a, L"f")) {
            if (force)
                bad = TRUE;
            force = TRUE;
        } else if (!ParseView(a, &target.View)) {
            bad = TRUE;
        }
    }
    int code = 1;
    if (bad) {
        SyntaxError(OP_DELETE);
        goto done;
    }
    DWORD error;
    if (!ConnectTarget(&target, &error)) {
        ReportError(error, OP_DELETE);
        goto done;
    }
    if (have_selection) {
        PCWSTR shown = name ? (*name ? name : k_default_name) : nullptr;
        int answer;
        do
            answer = all_values ? Prompt(L"Delete all values under the registry key %s (Yes/No)? ", target.Full, L"YNA", force)
                                : Prompt(L"Delete the registry value %s (Yes/No)? ", shown, L"YNA", force);
        while (answer > 2);
        if (answer != 1) {
            ReportPlain(ERROR_CANCELLED, 1);
            goto done;
        }
        HKEY key;
        error = (DWORD)RegOpenKeyExW(target.RootKey, target.Subkey, 0, ACCESS_FULL | target.View, &key);
        if (error != ERROR_SUCCESS) {
            ReportError(error, OP_DELETE);
            goto done;
        }
        if (name) {
            error = (DWORD)RegDeleteValueW(key, name);
        } else {
            DWORD values = 0, longest = 0;
            error = (DWORD)RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &values, &longest,
                                            nullptr, nullptr, nullptr);
            if (error == ERROR_SUCCESS && values) {
                PWSTR names = Allocate((SIZE_T)values * (longest + 2) * sizeof(WCHAR));
                DWORD found = 0;
                for (DWORD i = 0; names && i < values; i++) {
                    DWORD length = longest + 1;
                    if (RegEnumValueW(key, i, names + (SIZE_T)found * (longest + 2), &length, nullptr, nullptr, nullptr,
                                      nullptr) == ERROR_SUCCESS)
                        found++;
                }
                DWORD deleted = 0, first = 0;
                for (DWORD i = 0; i < found; i++) {
                    DWORD result = (DWORD)RegDeleteValueW(key, names + (SIZE_T)i * (longest + 2));
                    if (result == 0)
                        deleted++;
                    else if (!first)
                        first = result;
                }
                Release(names);
                if (!names)
                    error = ERROR_NOT_ENOUGH_MEMORY;
                else if (deleted == values)
                    error = 0;
                else
                    error = deleted ? (DWORD)DELETE_PARTIAL : first;
            }
        }
        RegCloseKey(key);
        code = finish_delete(error, TRUE);
        goto done;
    }
    if (!*target.Subkey) {
        /* the original would empty the root; a key with nothing above it is not deleted here. */
        Err(L"ERROR: Access is denied.\n");
        goto done;
    }
    int answer;
    do
        answer = Prompt(L"Permanently delete the registry key %s (Yes/No)? ", target.Full, L"YNA", force);
    while (answer > 2);
    if (answer != 1) {
        ReportPlain(ERROR_CANCELLED, 1);
        code = 0;
        goto done;
    }
    code = finish_delete(delete_key_tree(target.RootKey, target.Subkey, target.View), TRUE);
done:
    Release(name);
    FreeTarget(&target);
    return code;
}

/* ------------------------------------------------------------------ copy */

typedef struct COPY {
    BOOL Force;
    BOOL All;
    DWORD View;
    PCWSTR DestinationName;
} COPY;

/* copies the values of a key and, with recursion, its subkeys. returns an error, ERROR_CANCELLED for no. */
static DWORD copy_key(COPY *copy, HKEY source, HKEY destination, PCWSTR path, BOOL recursive)
{
    DWORD values = 0, longest = 0, subkeys = 0, longest_key = 0;
    DWORD error = (DWORD)RegQueryInfoKeyW(source, nullptr, nullptr, nullptr, &subkeys, &longest_key, nullptr, &values,
                                          &longest, nullptr, nullptr, nullptr);
    if (error != ERROR_SUCCESS)
        return error;
    PWSTR name = Allocate((longest + 2) * sizeof(WCHAR));
    if (!name)
        return ERROR_NOT_ENOUGH_MEMORY;
    for (DWORD i = 0; i < values && !error; i++) {
        DWORD length = longest + 1, type, size = 0;
        error = (DWORD)RegEnumValueW(source, i, name, &length, nullptr, &type, nullptr, &size);
        if (error)
            break;
        BYTE *data = Allocate((SIZE_T)size + 4);
        if (!data) {
            error = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        DWORD read = size;
        error = (DWORD)RegQueryValueExW(source, name, nullptr, &type, data, &read);
        if (!error) {
            if (!copy->Force && !copy->All && RegQueryValueExW(destination, name, nullptr, nullptr, nullptr, nullptr) == 0) {
                WCHAR shown[1200];
                _snwprintf(shown, RTL_NUMBER_OF(shown) - 1, L"%s\\%s", path, *name ? name : k_default_name);
                shown[RTL_NUMBER_OF(shown) - 1] = 0;
                int answer;
                do
                    answer = Prompt(L"Value %s exists. Overwrite (Yes/No/All)? ", shown, L"YNA", FALSE);
                while (answer > 3);
                if (answer == 3)
                    copy->All = TRUE;
                else if (answer != 1)
                    read = (DWORD)-1;
            }
            if (read != (DWORD)-1)
                error = (DWORD)RegSetValueExW(destination, name, 0, type, data, read);
        }
        Release(data);
    }
    Release(name);
    if (error || !recursive)
        return error;
    PWSTR child = Allocate((longest_key + 2) * sizeof(WCHAR));
    if (!child)
        return ERROR_NOT_ENOUGH_MEMORY;
    for (DWORD i = 0; i < subkeys && !error; i++) {
        DWORD length = longest_key + 1;
        error = (DWORD)RegEnumKeyExW(source, i, child, &length, nullptr, nullptr, nullptr, nullptr);
        if (error)
            break;
        HKEY from, to;
        error = (DWORD)RegOpenKeyExW(source, child, 0, KEY_READ | copy->View, &from);
        if (error)
            break;
        error = (DWORD)RegCreateKeyExW(destination, child, 0, nullptr, 0, ACCESS_FULL | copy->View, nullptr, &to, nullptr);
        if (!error) {
            WCHAR below[1200];
            _snwprintf(below, RTL_NUMBER_OF(below) - 1, L"%s\\%s", path, child);
            below[RTL_NUMBER_OF(below) - 1] = 0;
            error = copy_key(copy, from, to, below, TRUE);
            RegCloseKey(to);
        }
        RegCloseKey(from);
    }
    Release(child);
    return error;
}

int DoCopy(const ARGS *args)
{
    int argc = args->Count;
    PWSTR *argv = args->Items;
    if (argc >= 3 && IsHelp(argv[2])) {
        if (argc == 3) {
            Usage(OP_COPY);
            return 0;
        }
        return SyntaxError(OP_COPY);
    }
    if (argc < 4 || argc > 7)
        return SyntaxError(OP_COPY);
    TARGET from, to;
    if (!ParseKeyArgument(argv[2], OP_COPY, &from))
        return 1;
    if (!ParseKeyArgument(argv[3], OP_COPY, &to)) {
        FreeTarget(&from);
        return 1;
    }
    COPY copy = {};
    BOOL recursive = FALSE, bad = FALSE, have_s = FALSE;
    DWORD view = 0;
    for (int i = 4; i < argc && !bad; i++) {
        if (IsSwitch(argv[i], L"f")) {
            if (copy.Force)
                bad = TRUE;
            copy.Force = TRUE;
        } else if (IsSwitch(argv[i], L"s")) {
            if (have_s)
                bad = TRUE;
            have_s = recursive = TRUE;
        } else if (!ParseView(argv[i], &view)) {
            bad = TRUE;
        }
    }
    int code = 1;
    HKEY source = nullptr, destination = nullptr;
    if (bad) {
        SyntaxError(OP_COPY);
        goto done;
    }
    from.View = to.View = copy.View = view;
    DWORD error;
    if (!ConnectTarget(&from, &error) || !ConnectTarget(&to, &error)) {
        ReportError(error, OP_COPY);
        goto done;
    }
    if (from.RootKey == to.RootKey && _wcsicmp(from.Subkey, to.Subkey) == 0) {
        Err(L"ERROR: The registry entry cannot be copied onto itself.\nType \"REG COPY /?\" for usage.\n");
        goto done;
    }
    error = (DWORD)RegOpenKeyExW(from.RootKey, from.Subkey, 0, KEY_READ | view, &source);
    if (error != ERROR_SUCCESS) {
        ReportError(error, OP_COPY);
        goto done;
    }
    error = (DWORD)RegCreateKeyExW(to.RootKey, to.Subkey, 0, nullptr, 0, ACCESS_FULL | view, nullptr, &destination, nullptr);
    if (error != ERROR_SUCCESS) {
        ReportError(error, OP_COPY);
        goto done;
    }
    error = copy_key(&copy, source, destination, to.Full, recursive);
    if (error) {
        ReportError(error, OP_COPY);
        goto done;
    }
    ReportSuccess();
    code = 0;
done:
    if (source)
        RegCloseKey(source);
    if (destination)
        RegCloseKey(destination);
    FreeTarget(&from);
    FreeTarget(&to);
    return code;
}

/* ------------------------------------------------------------------ save, restore, load and unload */

typedef struct PRIVILEGE_STATE {
    ULONG Count;
    LUID Luid;
    ULONG Attributes;
} PRIVILEGE_STATE;

NTSYSCALLAPI NTSTATUS NTAPI NtAdjustPrivilegesToken(HANDLE token, BOOLEAN disable_all, PVOID new_state, ULONG length,
                                                    PVOID previous, PULONG returned);

/* the privilege to read and write the files of hives: 17 backup, 18 restore. */
static DWORD enable_privilege(ULONG number)
{
    HANDLE token;
    NTSTATUS status = NtOpenProcessToken(NtCurrentProcess(), 0x28, &token);
    if (!NT_SUCCESS(status))
        return RtlNtStatusToDosError(status);
    PRIVILEGE_STATE state = {1, {number, 0}, 2};
    status = NtAdjustPrivilegesToken(token, FALSE, &state, 0, nullptr, nullptr);
    NtClose(token);
    return NT_SUCCESS(status) ? 0 : RtlNtStatusToDosError(status);
}

/* the key and file of save, restore and load, and the switches of save. */
static BOOL parse_hive_command(const ARGS *args, int operation, TARGET *target, PWSTR *file, BOOL *force)
{
    int argc = args->Count;
    PWSTR *argv = args->Items;
    int wanted_max = operation == OP_SAVE ? 7 : operation == OP_RESTORE ? 5 : 4;
    if (argc < 4 || argc > wanted_max) {
        SyntaxError(operation);
        return FALSE;
    }
    if (!ParseKeyArgument(argv[2], operation, target))
        return FALSE;
    if (operation == OP_LOAD && !*target->Subkey) {
        SyntaxError(operation);
        FreeTarget(target);
        return FALSE;
    }
    *file = DuplicateString(argv[3]);
    TrimSpaces(*file);
    BOOL bad = !**file;
    for (int i = 4; i < argc && !bad; i++) {
        if (operation == OP_SAVE && IsSwitch(argv[i], L"y"))
            *force = TRUE;
        else if (operation == OP_SAVE && IsSwitch(argv[i], L"c"))
            ; /* accepted and ignored */
        else if (!ParseView(argv[i], &target->View))
            bad = TRUE;
    }
    if (bad) {
        SyntaxError(operation);
        Release(*file);
        FreeTarget(target);
        return FALSE;
    }
    return TRUE;
}

static int hive_result(DWORD error, int operation)
{
    if (error == ERROR_INVALID_PARAMETER)
        error = ERROR_FILENAME_EXCED_RANGE;
    if (error == 0) {
        ReportSuccess();
        return 0;
    }
    return ReportError(error, operation);
}

int DoSave(const ARGS *args)
{
    if (args->Count >= 3 && IsHelp(args->Items[2])) {
        if (args->Count == 3) {
            Usage(OP_SAVE);
            return 0;
        }
        return SyntaxError(OP_SAVE);
    }
    TARGET target;
    PWSTR file;
    BOOL force = FALSE;
    if (!parse_hive_command(args, OP_SAVE, &target, &file, &force))
        return 1;
    int code = 1;
    DWORD error;
    HKEY key = nullptr;
    if (!ConnectTarget(&target, &error)) {
        ReportError(error, OP_SAVE);
        goto done;
    }
    error = (DWORD)RegOpenKeyExW(target.RootKey, target.Subkey, 0, 0x20000 | target.View, &key);
    if (error == 0)
        error = enable_privilege(17);
    if (error == 0) {
        error = (DWORD)RegSaveKeyExW(key, file, nullptr, 4);
        if (error == ERROR_INVALID_PARAMETER) {
            DeleteFileW(file);
            error = (DWORD)RegSaveKeyExW(key, file, nullptr, 1);
        }
        if (error == ERROR_ALREADY_EXISTS) {
            int answer;
            do
                answer = Prompt(L"File %s already exists. Overwrite (Yes/No)?", file, L"YNA", force);
            while (answer > 2);
            if (answer != 1) {
                ReportPlain(ERROR_CANCELLED, 1);
                code = 0;
                goto done;
            }
            WCHAR temporary[MAX_PATH + 16];
            WCHAR directory[MAX_PATH];
            DWORD n = GetFullPathNameW(file, MAX_PATH, directory, nullptr);
            PWSTR slash = n ? wcsrchr(directory, L'\\') : nullptr;
            if (slash)
                *slash = 0;
            if (!n || GetTempFileNameW(directory, L"REG", 0, temporary) == 0) {
                error = GetLastError();
            } else {
                DeleteFileW(temporary);
                error = (DWORD)RegSaveKeyExW(key, temporary, nullptr, 4);
                if (error == 0 && !MoveFileExW(temporary, file, MOVEFILE_REPLACE_EXISTING))
                    error = GetLastError();
                DeleteFileW(temporary);
            }
        }
    }
    code = hive_result(error, OP_SAVE);
done:
    if (key)
        RegCloseKey(key);
    Release(file);
    FreeTarget(&target);
    return code;
}

int DoRestore(const ARGS *args)
{
    if (args->Count >= 3 && IsHelp(args->Items[2])) {
        if (args->Count == 3) {
            Usage(OP_RESTORE);
            return 0;
        }
        return SyntaxError(OP_RESTORE);
    }
    TARGET target;
    PWSTR file;
    BOOL force = FALSE;
    if (!parse_hive_command(args, OP_RESTORE, &target, &file, &force))
        return 1;
    int code = 1;
    DWORD error;
    HKEY key = nullptr;
    if (!ConnectTarget(&target, &error)) {
        ReportError(error, OP_RESTORE);
        goto done;
    }
    error = (DWORD)RegOpenKeyExW(target.RootKey, target.Subkey, 0, ACCESS_FULL | target.View, &key);
    if (error == 0)
        error = enable_privilege(18);
    if (error == 0)
        error = (DWORD)RegRestoreKeyW(key, file, 8);
    code = hive_result(error, OP_RESTORE);
done:
    if (key)
        RegCloseKey(key);
    Release(file);
    FreeTarget(&target);
    return code;
}

int DoLoad(const ARGS *args)
{
    if (args->Count >= 3 && IsHelp(args->Items[2])) {
        if (args->Count == 3) {
            Usage(OP_LOAD);
            return 0;
        }
        return SyntaxError(OP_LOAD);
    }
    TARGET target;
    PWSTR file;
    BOOL force = FALSE;
    if (!parse_hive_command(args, OP_LOAD, &target, &file, &force))
        return 1;
    int code = 1;
    DWORD error;
    if (!ConnectTarget(&target, &error)) {
        ReportError(error, OP_LOAD);
    } else {
        error = enable_privilege(18);
        if (error == 0)
            error = (DWORD)RegLoadKeyW(target.RootKey, target.Subkey, file);
        code = hive_result(error, OP_LOAD);
    }
    Release(file);
    FreeTarget(&target);
    return code;
}

int DoUnload(const ARGS *args)
{
    if (args->Count == 3 && IsHelp(args->Items[2])) {
        Usage(OP_UNLOAD);
        return 0;
    }
    if (args->Count != 3)
        return SyntaxError(OP_UNLOAD);
    TARGET target;
    if (!ParseKeyArgument(args->Items[2], OP_UNLOAD, &target))
        return 1;
    int code = 1;
    DWORD error;
    if (!*target.Subkey) {
        SyntaxError(OP_UNLOAD);
    } else if (!ConnectTarget(&target, &error)) {
        ReportError(error, OP_UNLOAD);
    } else {
        error = enable_privilege(18);
        if (error == 0)
            error = (DWORD)RegUnLoadKeyW(target.RootKey, target.Subkey);
        code = hive_result(error, OP_UNLOAD);
    }
    FreeTarget(&target);
    return code;
}

/* ------------------------------------------------------------------ flags */

typedef struct KEY_FLAGS {
    ULONG Wow64;
    ULONG Key;
    ULONG Control;
} KEY_FLAGS;

#define KEY_FLAG_VIRTUALIZE 2
#define KEY_FLAG_SILENT 4
#define KEY_FLAG_RECURSE 8

typedef NTSTATUS(NTAPI *SET_INFORMATION_KEY)(HANDLE key, ULONG klass, PVOID info, ULONG length);

/* not every processor's ntdll exports it, so it is looked up. */
static NTSTATUS set_key_flags(HANDLE key, ULONG flags)
{
    SET_INFORMATION_KEY call = (SET_INFORMATION_KEY)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationKey");
    return call ? call(key, 2, &flags, sizeof(flags)) : (NTSTATUS)0xC0000002;
}

static int flags_one(HKEY key, PCWSTR path, BOOL set, ULONG wanted, BOOL recursive, DWORD view)
{
    if (set) {
        if (set_key_flags(key, wanted) < 0) {
            Err(L"ERROR: Failed to access \"%s\".\n", path);
            return 1;
        }
    } else {
        KEY_FLAGS flags;
        ULONG returned;
        if (NtQueryKey(key, 5, &flags, sizeof(flags), &returned) < 0) {
            Err(L"ERROR: Failed to access \"%s\".\n", path);
            return 1;
        }
        Out(L"\n%s\n", path);
        Out(L"\tREG_KEY_DONT_VIRTUALIZE: %s\n", flags.Control & KEY_FLAG_VIRTUALIZE ? L"SET" : L"CLEAR");
        Out(L"\tREG_KEY_DONT_SILENT_FAIL: %s\n", flags.Control & KEY_FLAG_SILENT ? L"SET" : L"CLEAR");
        Out(L"\tREG_KEY_RECURSE_FLAG: %s\n\n", flags.Control & KEY_FLAG_RECURSE ? L"SET" : L"CLEAR");
    }
    if (!recursive)
        return 0;
    DWORD subkeys = 0, longest = 0;
    if (RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, &longest, nullptr, nullptr, nullptr, nullptr, nullptr,
                         nullptr) != ERROR_SUCCESS)
        return 0;
    SIZE_T path_length = wcslen(path);
    PWSTR child = Allocate((path_length + longest + 3) * sizeof(WCHAR));
    if (!child)
        return 1;
    memcpy(child, path, path_length * sizeof(WCHAR));
    child[path_length] = L'\\';
    int result = 0;
    for (DWORD i = 0; i < subkeys && !result; i++) {
        DWORD length = longest + 1;
        if (RegEnumKeyExW(key, i, child + path_length + 1, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            continue;
        HKEY sub;
        if (RegOpenKeyExW(key, child + path_length + 1, 0, (set ? ACCESS_FULL : KEY_READ) | view, &sub) == ERROR_SUCCESS) {
            result = flags_one(sub, child, set, wanted, TRUE, view);
            RegCloseKey(sub);
        }
    }
    Release(child);
    return result;
}

int DoFlags(const ARGS *args)
{
    int argc = args->Count;
    PWSTR *argv = args->Items;
    if (argc >= 3 && IsHelp(argv[2])) {
        if (argc == 3) {
            Usage(OP_FLAGS);
            return 0;
        }
        return SyntaxError(OP_FLAGS);
    }
    if (argc < 4)
        return SyntaxError(OP_FLAGS);
    TARGET target;
    if (!ParseKeyArgument(argv[2], OP_FLAGS, &target))
        return 1;
    int code = 1;
    if (target.Remote) {
        Err(L"ERROR: A remote machine was specified.\nType \"REG FLAGS /?\" for usage.\n");
        goto done;
    }
    if (_wcsnicmp(target.Full, L"HKEY_LOCAL_MACHINE\\SOFTWARE", 27) != 0) {
        Err(L"ERROR: This operation can only be performed on subkeys of HKLM\\Software.\nType \"REG FLAGS /?\" for usage.\n");
        goto done;
    }
    BOOL set;
    if (_wcsicmp(argv[3], L"QUERY") == 0)
        set = FALSE;
    else if (_wcsicmp(argv[3], L"SET") == 0)
        set = TRUE;
    else {
        SyntaxError(OP_FLAGS);
        goto done;
    }
    ULONG wanted = 0;
    BOOL recursive = FALSE, bad = FALSE;
    for (int i = 4; i < argc && !bad; i++) {
        if (IsSwitch(argv[i], L"s")) {
            if (recursive)
                bad = TRUE;
            recursive = TRUE;
        } else if (set && _wcsicmp(argv[i], L"DONT_VIRTUALIZE") == 0 && !(wanted & KEY_FLAG_VIRTUALIZE)) {
            wanted |= KEY_FLAG_VIRTUALIZE;
        } else if (set && _wcsicmp(argv[i], L"DONT_SILENT_FAIL") == 0 && !(wanted & KEY_FLAG_SILENT)) {
            wanted |= KEY_FLAG_SILENT;
        } else if (set && _wcsicmp(argv[i], L"RECURSE_FLAG") == 0 && !(wanted & KEY_FLAG_RECURSE)) {
            wanted |= KEY_FLAG_RECURSE;
        } else if (!ParseView(argv[i], &target.View)) {
            bad = TRUE;
        }
    }
    if (bad) {
        SyntaxError(OP_FLAGS);
        goto done;
    }
    HKEY key;
    DWORD error = (DWORD)RegOpenKeyExW(target.RootKey, target.Subkey, 0, (set ? ACCESS_FULL : KEY_READ) | target.View, &key);
    if (error != ERROR_SUCCESS) {
        ReportError(error, OP_FLAGS);
        goto done;
    }
    code = flags_one(key, target.Full, set, wanted, recursive, target.View);
    RegCloseKey(key);
    if (!code)
        ReportSuccess();
done:
    FreeTarget(&target);
    return code;
}
