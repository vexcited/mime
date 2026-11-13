/* registry functions over the NT key services. */
#include "kernelbase_private.h"

NTSYSAPI NTSTATUS NTAPI RtlOpenCurrentUser(ACCESS_MASK access, PHANDLE key);

#define KEY_BASIC_INFORMATION_CLASS 0
#define KEY_NODE_INFORMATION_CLASS 1
#define KEY_FULL_INFORMATION_CLASS 2
#define KEY_VALUE_FULL_INFORMATION_CLASS 1
#define KEY_VALUE_PARTIAL_INFORMATION_CLASS 2

#ifndef STATUS_NO_MORE_ENTRIES
#define STATUS_NO_MORE_ENTRIES ((NTSTATUS)0x8000001AL)
#endif

typedef struct KB_KEY_VALUE_PARTIAL {
    ULONG TitleIndex;
    ULONG Type;
    ULONG DataLength;
    UCHAR Data[1];
} KB_KEY_VALUE_PARTIAL;

typedef struct KB_KEY_VALUE_FULL {
    ULONG TitleIndex;
    ULONG Type;
    ULONG DataOffset;
    ULONG DataLength;
    ULONG NameLength;
    WCHAR Name[1];
} KB_KEY_VALUE_FULL;

typedef struct KB_KEY_NODE {
    LARGE_INTEGER LastWriteTime;
    ULONG TitleIndex;
    ULONG ClassOffset;
    ULONG ClassLength;
    ULONG NameLength;
    WCHAR Name[1];
} KB_KEY_NODE;

typedef struct KB_KEY_FULL {
    LARGE_INTEGER LastWriteTime;
    ULONG TitleIndex;
    ULONG ClassOffset;
    ULONG ClassLength;
    ULONG SubKeys;
    ULONG MaxNameLen;
    ULONG MaxClassLen;
    ULONG Values;
    ULONG MaxValueNameLen;
    ULONG MaxValueDataLen;
    WCHAR Class[1];
} KB_KEY_FULL;

#define PREDEFINED_FIRST 0x80000000u
#define PREDEFINED_COUNT 8

static const PCWSTR g_predefined_paths[PREDEFINED_COUNT] = {
    L"\\Registry\\Machine\\Software\\Classes",
    nullptr, /* HKEY_CURRENT_USER, the token user's key. */
    L"\\Registry\\Machine",
    L"\\Registry\\User",
    nullptr,
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Hardware Profiles\\Current",
    nullptr,
    L"\\Registry\\User\\.Default\\Software\\Classes\\Local Settings",
};

static HANDLE g_predefined[PREDEFINED_COUNT];
static HANDLE g_overrides[PREDEFINED_COUNT];
static BOOL g_overridden[PREDEFINED_COUNT];

static LONG to_error(NTSTATUS status)
{
    return NT_SUCCESS(status) ? ERROR_SUCCESS : (LONG)RtlNtStatusToDosError(status);
}

/* windows looks at the low half only, so a value that was not sign extended, as Go passes it, is predefined too. */
static BOOL is_predefined(HKEY key)
{
    ULONG_PTR v = (ULONG_PTR)key;
    return ((ULONG_PTR)(LONG_PTR)(LONG)v == v || (ULONG)v == v) && (ULONG)v - PREDEFINED_FIRST < PREDEFINED_COUNT;
}

static LONG open_path(PCWSTR path, ACCESS_MASK access, HANDLE *out)
{
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES oa;
    RtlInitUnicodeString(&us, path);
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    return to_error(NtOpenKey(out, access, &oa));
}

/* predefined keys are opened once and cached. */
static LONG resolve_key(HKEY key, HANDLE *out)
{
    if (!key)
        return ERROR_INVALID_HANDLE;
    if (!is_predefined(key)) {
        *out = (HANDLE)key;
        return ERROR_SUCCESS;
    }
    ULONG index = (ULONG)(ULONG_PTR)key - PREDEFINED_FIRST;
    if (g_overridden[index]) {
        *out = g_overrides[index];
        return ERROR_SUCCESS;
    }
    if (g_predefined[index]) {
        *out = g_predefined[index];
        return ERROR_SUCCESS;
    }
    HANDLE h;
    LONG error;
    if (index == 1)
        error = to_error(RtlOpenCurrentUser(MAXIMUM_ALLOWED, &h));
    else if (!g_predefined_paths[index])
        return ERROR_INVALID_HANDLE;
    else
        error = open_path(g_predefined_paths[index], MAXIMUM_ALLOWED, &h);
    if (error)
        return error;
    if (__atomic_compare_exchange_n(&g_predefined[index], &(HANDLE){nullptr}, h, FALSE, __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE)) {
        *out = h;
    } else {
        NtClose(h);
        *out = g_predefined[index];
    }
    return ERROR_SUCCESS;
}

static ACCESS_MASK clean_access(REGSAM access)
{
    return access & ~(REGSAM)(KEY_WOW64_32KEY | KEY_WOW64_64KEY);
}

/* ------------------------------------------------------------------ opening and creating */

LSTATUS WINAPI RegOpenKeyExW(HKEY key, LPCWSTR subkey, DWORD options, REGSAM access, HKEY *result)
{
    if (!result)
        return ERROR_INVALID_PARAMETER;
    if ((options & ~(DWORD)REG_OPTION_OPEN_LINK) || (access & KEY_WOW64_RES) == KEY_WOW64_RES)
        return ERROR_INVALID_PARAMETER;
    *result = nullptr;
    if ((!subkey || !*subkey) && is_predefined(key) && !(access & KEY_WOW64_RES)) {
        HANDLE h;
        LONG error = resolve_key(key, &h);
        if (!error)
            *result = key;
        return error;
    }
    HANDLE root;
    LONG error = resolve_key(key, &root);
    if (error)
        return error;
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES oa;
    RtlInitUnicodeString(&us, subkey ? subkey : L"");
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE | ((options & REG_OPTION_OPEN_LINK) ? OBJ_OPENLINK : 0),
                               root, nullptr);
    HANDLE h;
    NTSTATUS status = NtOpenKey(&h, access, &oa);
    if (!NT_SUCCESS(status) && !us.Length && !(access & KEY_WOW64_RES)) {
        /* an empty name reopens the key itself. */
        status = NtDuplicateObject(NtCurrentProcess(), root, NtCurrentProcess(), &h, clean_access(access), 0, 0);
    }
    if (!NT_SUCCESS(status))
        return to_error(status);
    *result = (HKEY)h;
    return ERROR_SUCCESS;
}

/* a new handle with the same access, predefined keys included. */
LSTATUS WINAPI RegDuplicateHKey(HKEY key, HKEY *result)
{
    if (!result)
        return ERROR_INVALID_PARAMETER;
    *result = nullptr;
    HANDLE source, copy;
    LONG error = resolve_key(key, &source);
    if (error)
        return error;
    NTSTATUS status = NtDuplicateObject(NtCurrentProcess(), source, NtCurrentProcess(), &copy, 0, 0, DUPLICATE_SAME_ACCESS);
    if (!NT_SUCCESS(status))
        return to_error(status);
    *result = (HKEY)copy;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegOpenKeyExA(HKEY key, LPCSTR subkey, DWORD options, REGSAM access, HKEY *result)
{
    PWSTR w = KbAnsiToWideCp(CP_ACP, subkey);
    if (subkey && !w)
        return ERROR_NOT_ENOUGH_MEMORY;
    LONG error = RegOpenKeyExW(key, w, options, access, result);
    KbFree(w);
    return error;
}

LSTATUS WINAPI RegOpenKeyW(HKEY key, LPCWSTR subkey, HKEY *result)
{
    if (!result)
        return ERROR_INVALID_PARAMETER;
    if (!subkey || !*subkey) {
        *result = key;
        return ERROR_SUCCESS;
    }
    return RegOpenKeyExW(key, subkey, 0, MAXIMUM_ALLOWED, result);
}

LSTATUS WINAPI RegOpenKeyA(HKEY key, LPCSTR subkey, HKEY *result)
{
    if (!result)
        return ERROR_INVALID_PARAMETER;
    if (!subkey || !*subkey) {
        *result = key;
        return ERROR_SUCCESS;
    }
    return RegOpenKeyExA(key, subkey, 0, MAXIMUM_ALLOWED, result);
}

/* win32 creates missing parents; the native service creates only the final key. */
static NTSTATUS create_key_path(PHANDLE key, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
                                PUNICODE_STRING klass, ULONG options, PULONG disposition)
{
    NTSTATUS status = NtCreateKey(key, access, attributes, 0, klass, options, disposition);
    if (status != STATUS_OBJECT_NAME_NOT_FOUND && status != STATUS_OBJECT_PATH_NOT_FOUND)
        return status;

    UNICODE_STRING parent = *attributes->ObjectName;
    ULONG split = parent.Length / sizeof(WCHAR);
    while (split && parent.Buffer[split - 1] == '\\')
        split--;
    while (split && parent.Buffer[split - 1] != '\\')
        split--;
    if (!split)
        return status;
    parent.Length = (USHORT)((split - 1) * sizeof(WCHAR));
    while (parent.Length && parent.Buffer[parent.Length / sizeof(WCHAR) - 1] == '\\')
        parent.Length -= sizeof(WCHAR);
    if (!parent.Length)
        return status;

    OBJECT_ATTRIBUTES parent_attributes = *attributes;
    parent_attributes.ObjectName = &parent;
    parent_attributes.Attributes &= ~OBJ_OPENLINK;
    HANDLE parent_key;
    status = create_key_path(&parent_key, KEY_CREATE_SUB_KEY | (access & KEY_WOW64_RES), &parent_attributes, nullptr,
                              options & ~(ULONG)REG_OPTION_CREATE_LINK, nullptr);
    if (!NT_SUCCESS(status))
        return status;

    UNICODE_STRING leaf = *attributes->ObjectName;
    leaf.Buffer += split;
    leaf.Length -= (USHORT)(split * sizeof(WCHAR));
    leaf.MaximumLength = leaf.Length;
    OBJECT_ATTRIBUTES leaf_attributes = *attributes;
    leaf_attributes.RootDirectory = parent_key;
    leaf_attributes.ObjectName = &leaf;
    status = NtCreateKey(key, access, &leaf_attributes, 0, klass, options, disposition);
    NtClose(parent_key);
    return status;
}

LSTATUS WINAPI RegCreateKeyExW(HKEY key, LPCWSTR subkey, DWORD reserved, LPWSTR klass, DWORD options, REGSAM access,
                               const SECURITY_ATTRIBUTES *sa, HKEY *result, LPDWORD disposition)
{
    if (!result || !subkey || reserved || (access & KEY_WOW64_RES) == KEY_WOW64_RES)
        return ERROR_INVALID_PARAMETER;
    if (options & ~(DWORD)(REG_OPTION_VOLATILE | REG_OPTION_CREATE_LINK | REG_OPTION_BACKUP_RESTORE |
                           REG_OPTION_OPEN_LINK))
        return ERROR_INVALID_PARAMETER;
    if (subkey[0] == '\\')
        return ERROR_BAD_PATHNAME;
    HANDLE root;
    LONG error = resolve_key(key, &root);
    if (error)
        return error;
    UNICODE_STRING name, class_name;
    OBJECT_ATTRIBUTES oa;
    RtlInitUnicodeString(&name, subkey);
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | ((options & REG_OPTION_OPEN_LINK) ? OBJ_OPENLINK : 0),
                               root, sa ? sa->lpSecurityDescriptor : nullptr);
    if (klass)
        RtlInitUnicodeString(&class_name, klass);
    HANDLE h;
    ULONG disp = 0;
    NTSTATUS status = create_key_path(&h, access, &oa, klass ? &class_name : nullptr,
                                       options & ~(DWORD)REG_OPTION_OPEN_LINK, &disp);
    if (!NT_SUCCESS(status))
        return to_error(status);
    *result = (HKEY)h;
    if (disposition)
        *disposition = disp;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegCreateKeyExA(HKEY key, LPCSTR subkey, DWORD reserved, LPSTR klass, DWORD options, REGSAM access,
                               const SECURITY_ATTRIBUTES *sa, HKEY *result, LPDWORD disposition)
{
    if (!subkey)
        return ERROR_INVALID_PARAMETER;
    PWSTR wsub = KbAnsiToWideCp(CP_ACP, subkey);
    PWSTR wclass = KbAnsiToWideCp(CP_ACP, klass);
    LONG error = ERROR_NOT_ENOUGH_MEMORY;
    if (wsub && (!klass || wclass))
        error = RegCreateKeyExW(key, wsub, reserved, wclass, options, access, sa, result, disposition);
    KbFree(wsub);
    KbFree(wclass);
    return error;
}

LSTATUS WINAPI RegCreateKeyW(HKEY key, LPCWSTR subkey, HKEY *result)
{
    if (!result)
        return ERROR_INVALID_PARAMETER;
    if (!subkey || !*subkey) {
        *result = key;
        return ERROR_SUCCESS;
    }
    return RegCreateKeyExW(key, subkey, 0, nullptr, 0, MAXIMUM_ALLOWED, nullptr, result, nullptr);
}

LSTATUS WINAPI RegCreateKeyA(HKEY key, LPCSTR subkey, HKEY *result)
{
    if (!result)
        return ERROR_INVALID_PARAMETER;
    if (!subkey || !*subkey) {
        *result = key;
        return ERROR_SUCCESS;
    }
    return RegCreateKeyExA(key, subkey, 0, nullptr, 0, MAXIMUM_ALLOWED, nullptr, result, nullptr);
}

LSTATUS WINAPI RegCloseKey(HKEY key)
{
    if (!key)
        return ERROR_INVALID_HANDLE;
    if (is_predefined(key))
        return ERROR_SUCCESS;
    return to_error(NtClose((HANDLE)key));
}

LSTATUS WINAPI RegOpenCurrentUser(REGSAM access, HKEY *result)
{
    if (!result || (access & KEY_WOW64_RES) == KEY_WOW64_RES)
        return ERROR_INVALID_PARAMETER;
    *result = nullptr;
    return to_error(RtlOpenCurrentUser(access, (PHANDLE)result));
}

LSTATUS WINAPI RegOverridePredefKey(HKEY key, HKEY new_key)
{
    if (!is_predefined(key))
        return ERROR_INVALID_HANDLE;
    ULONG index = (ULONG)(ULONG_PTR)key - PREDEFINED_FIRST;
    if (!new_key) {
        g_overridden[index] = FALSE;
        return ERROR_SUCCESS;
    }
    HANDLE source;
    LONG error = resolve_key(new_key, &source);
    if (error)
        return error;
    HANDLE copy;
    NTSTATUS status = NtDuplicateObject(NtCurrentProcess(), source, NtCurrentProcess(), &copy, 0, 0,
                                        DUPLICATE_SAME_ACCESS);
    if (!NT_SUCCESS(status))
        return to_error(status);
    if (g_overridden[index])
        NtClose(g_overrides[index]);
    g_overrides[index] = copy;
    g_overridden[index] = TRUE;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegDisablePredefinedCacheEx(void)
{
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ values */

static LONG query_value(HANDLE key, LPCWSTR name, KB_KEY_VALUE_PARTIAL **out)
{
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, name ? name : L"");
    ULONG length = 256;
    for (;;) {
        KB_KEY_VALUE_PARTIAL *info = KbAlloc(length);
        if (!info)
            return ERROR_NOT_ENOUGH_MEMORY;
        ULONG returned = 0;
        NTSTATUS status = NtQueryValueKey(key, &us, KEY_VALUE_PARTIAL_INFORMATION_CLASS, info, length, &returned);
        if (NT_SUCCESS(status)) {
            *out = info;
            return ERROR_SUCCESS;
        }
        KbFree(info);
        if ((status == STATUS_BUFFER_OVERFLOW || status == STATUS_BUFFER_TOO_SMALL) && returned > length) {
            length = returned;
            continue;
        }
        if (status == STATUS_BUFFER_OVERFLOW || status == STATUS_BUFFER_TOO_SMALL) {
            length *= 2;
            if (length > 0x10000000)
                return ERROR_OUTOFMEMORY;
            continue;
        }
        return to_error(status);
    }
}

LSTATUS KbQueryPerformanceData(LPCWSTR name, LPDWORD type, LPBYTE data, LPDWORD size);

LSTATUS WINAPI RegQueryValueExW(HKEY key, LPCWSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size)
{
    if (reserved || (data && !size))
        return ERROR_INVALID_PARAMETER;
    if (key == HKEY_PERFORMANCE_DATA)
        return KbQueryPerformanceData(name, type, data, size);
    HANDLE h;
    LONG error = resolve_key(key, &h);
    if (error)
        return error;
    KB_KEY_VALUE_PARTIAL *info;
    error = query_value(h, name, &info);
    if (error)
        return error;
    if (type)
        *type = info->Type;
    if (data) {
        if (*size < info->DataLength)
            error = ERROR_MORE_DATA;
        else
            memcpy(data, info->Data, info->DataLength);
    }
    if (size)
        *size = info->DataLength;
    KbFree(info);
    return error;
}

static BOOL is_string_type(DWORD type)
{
    return type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ;
}

LSTATUS WINAPI RegQueryValueExA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size)
{
    if (reserved || (data && !size))
        return ERROR_INVALID_PARAMETER;
    if (key == HKEY_PERFORMANCE_DATA) {
        PWSTR wide = KbAnsiToWideCp(CP_ACP, name);
        if (name && !wide)
            return ERROR_NOT_ENOUGH_MEMORY;
        LSTATUS status = KbQueryPerformanceData(wide, type, data, size);
        KbFree(wide);
        return status;
    }
    HANDLE h;
    LONG error = resolve_key(key, &h);
    if (error)
        return error;
    PWSTR wname = KbAnsiToWideCp(CP_ACP, name);
    if (name && !wname)
        return ERROR_NOT_ENOUGH_MEMORY;
    KB_KEY_VALUE_PARTIAL *info;
    error = query_value(h, wname, &info);
    KbFree(wname);
    if (error)
        return error;
    if (type)
        *type = info->Type;
    DWORD need = info->DataLength;
    if (is_string_type(info->Type)) {
        int wchars = (int)(info->DataLength / sizeof(WCHAR));
        need = wchars ? (DWORD)WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)info->Data, wchars, nullptr, 0, nullptr, nullptr) : 0;
        if (data) {
            if (*size < need)
                error = ERROR_MORE_DATA;
            else if (wchars)
                WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)info->Data, wchars, (LPSTR)data, (int)*size, nullptr, nullptr);
        }
    } else if (data) {
        if (*size < need)
            error = ERROR_MORE_DATA;
        else
            memcpy(data, info->Data, need);
    }
    if (size)
        *size = need;
    KbFree(info);
    return error;
}

LSTATUS WINAPI RegSetValueExW(HKEY key, LPCWSTR name, DWORD reserved, DWORD type, const BYTE *data, DWORD size)
{
    if (!data && size)
        return ERROR_NOACCESS;
    HANDLE h;
    LONG error = resolve_key(key, &h);
    if (error)
        return error;
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, name ? name : L"");
    /* a string missing its terminator gets one, as Windows does. */
    if (is_string_type(type) && data && size >= sizeof(WCHAR) && (size & 1) == 0 &&
        ((LPCWSTR)data)[size / sizeof(WCHAR) - 1] != 0) {
        PBYTE copy = KbAlloc(size + sizeof(WCHAR));
        if (!copy)
            return ERROR_NOT_ENOUGH_MEMORY;
        memcpy(copy, data, size);
        ((PWSTR)copy)[size / sizeof(WCHAR)] = 0;
        error = to_error(NtSetValueKey(h, &us, 0, type, copy, size + sizeof(WCHAR)));
        KbFree(copy);
        return error;
    }
    return to_error(NtSetValueKey(h, &us, 0, type, (PVOID)data, size));
}

LSTATUS WINAPI RegSetValueExA(HKEY key, LPCSTR name, DWORD reserved, DWORD type, const BYTE *data, DWORD size)
{
    if (!data && size)
        return ERROR_NOACCESS;
    PWSTR wname = KbAnsiToWideCp(CP_ACP, name);
    if (name && !wname)
        return ERROR_NOT_ENOUGH_MEMORY;
    LONG error;
    if (is_string_type(type) && data && size) {
        int wchars = MultiByteToWideChar(CP_ACP, 0, (LPCSTR)data, (int)size, nullptr, 0);
        PWSTR wdata = KbAlloc(((SIZE_T)wchars + 1) * sizeof(WCHAR));
        if (!wdata) {
            KbFree(wname);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        MultiByteToWideChar(CP_ACP, 0, (LPCSTR)data, (int)size, wdata, wchars);
        error = RegSetValueExW(key, wname, reserved, type, (const BYTE *)wdata, (DWORD)wchars * sizeof(WCHAR));
        KbFree(wdata);
    } else {
        error = RegSetValueExW(key, wname, reserved, type, data, size);
    }
    KbFree(wname);
    return error;
}

static DWORD type_flag(DWORD type)
{
    switch (type) {
    case REG_NONE:
        return RRF_RT_REG_NONE;
    case REG_SZ:
        return RRF_RT_REG_SZ;
    case REG_EXPAND_SZ:
        return RRF_RT_REG_EXPAND_SZ;
    case REG_BINARY:
        return RRF_RT_REG_BINARY;
    case REG_DWORD:
        return RRF_RT_REG_DWORD;
    case REG_MULTI_SZ:
        return RRF_RT_REG_MULTI_SZ;
    case REG_QWORD:
        return RRF_RT_REG_QWORD;
    default:
        return 0;
    }
}

LSTATUS WINAPI RegGetValueW(HKEY key, LPCWSTR subkey, LPCWSTR value, DWORD flags, LPDWORD type, PVOID data,
                            LPDWORD size)
{
    /* an expandable value is expanded and reported as a string, so a request for expandable ones alone finds none. */
    DWORD view_flags = flags & (RRF_SUBKEY_WOW6432KEY | RRF_SUBKEY_WOW6464KEY);
    if ((data && !size) || !(flags & RRF_RT_ANY) ||
        view_flags == (RRF_SUBKEY_WOW6432KEY | RRF_SUBKEY_WOW6464KEY))
        return ERROR_INVALID_PARAMETER;
    DWORD capacity = data ? *size : 0;
    if (data && (flags & RRF_ZEROONFAILURE))
        memset(data, 0, capacity);
    HKEY opened = nullptr;
    if (subkey && *subkey) {
        REGSAM view = view_flags == RRF_SUBKEY_WOW6432KEY ? KEY_WOW64_32KEY :
                      view_flags == RRF_SUBKEY_WOW6464KEY ? KEY_WOW64_64KEY : 0;
        LONG error = RegOpenKeyExW(key, subkey, 0, KEY_QUERY_VALUE | view, &opened);
        if (error)
            return error;
        key = opened;
    }
    HANDLE h;
    LONG error = resolve_key(key, &h);
    KB_KEY_VALUE_PARTIAL *info = nullptr;
    if (!error)
        error = query_value(h, value, &info);
    if (opened)
        RegCloseKey(opened);
    if (error)
        return error;
    DWORD actual_type = info->Type;
    PBYTE bytes = info->Data;
    DWORD length = info->DataLength;
    PWSTR expanded = nullptr;
    if (actual_type == REG_EXPAND_SZ && !(flags & RRF_NOEXPAND)) {
        /* the expanded string is reported as REG_SZ. */
        DWORD chars = ExpandEnvironmentStringsW((LPCWSTR)bytes, nullptr, 0);
        expanded = chars ? KbAlloc((SIZE_T)chars * sizeof(WCHAR)) : nullptr;
        if (expanded) {
            ExpandEnvironmentStringsW((LPCWSTR)bytes, expanded, chars);
            bytes = (PBYTE)expanded;
            length = chars * sizeof(WCHAR);
        }
        actual_type = REG_SZ;
    }
    DWORD allowed = flags & RRF_RT_ANY;
    BOOL type_ok = (type_flag(actual_type) & allowed) != 0 || allowed == RRF_RT_ANY;
    /* binary data of the right size counts as a dword or qword. */
    if (actual_type == REG_BINARY && ((allowed & RRF_RT_DWORD) == RRF_RT_DWORD || (allowed & RRF_RT_QWORD) == RRF_RT_QWORD)) {
        BOOL dword_ok = (allowed & RRF_RT_REG_DWORD) && length == 4;
        BOOL qword_ok = (allowed & RRF_RT_REG_QWORD) && length == 8;
        type_ok = (allowed & RRF_RT_REG_BINARY) || dword_ok || qword_ok;
    }
    if (!type_ok) {
        error = ERROR_UNSUPPORTED_TYPE;
    } else {
        /* strings returned without a terminator gain one. */
        BOOL add_terminator = is_string_type(actual_type) &&
                              (length < sizeof(WCHAR) || ((LPCWSTR)bytes)[length / sizeof(WCHAR) - 1] != 0);
        DWORD need = length + (add_terminator ? sizeof(WCHAR) : 0);
        if (data) {
            if (capacity < need) {
                error = ERROR_MORE_DATA;
            } else {
                memcpy(data, bytes, length);
                if (add_terminator)
                    ((PWSTR)data)[length / sizeof(WCHAR)] = 0;
            }
        }
        if (size)
            *size = need;
    }
    if (type)
        *type = actual_type;
    if (error && data && (flags & RRF_ZEROONFAILURE))
        memset(data, 0, capacity);
    KbFree(expanded);
    KbFree(info);
    return error;
}

LSTATUS WINAPI RegGetValueA(HKEY key, LPCSTR subkey, LPCSTR value, DWORD flags, LPDWORD type, PVOID data, LPDWORD size)
{
    if (data && !size)
        return ERROR_INVALID_PARAMETER;
    PWSTR wsub = KbAnsiToWideCp(CP_ACP, subkey);
    PWSTR wvalue = KbAnsiToWideCp(CP_ACP, value);
    DWORD wsize = 0, wtype = 0;
    LONG error = RegGetValueW(key, wsub, wvalue, flags & ~(DWORD)RRF_ZEROONFAILURE, &wtype, nullptr, &wsize);
    PBYTE wdata = nullptr;
    if (!error) {
        wdata = KbAlloc(wsize + sizeof(WCHAR));
        error = wdata ? RegGetValueW(key, wsub, wvalue, flags & ~(DWORD)RRF_ZEROONFAILURE, &wtype, wdata, &wsize)
                      : ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!error) {
        DWORD need = wsize;
        if (is_string_type(wtype)) {
            int wchars = (int)(wsize / sizeof(WCHAR));
            need = (DWORD)WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)wdata, wchars, nullptr, 0, nullptr, nullptr);
            if (data && *size >= need)
                WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)wdata, wchars, data, (int)*size, nullptr, nullptr);
        } else if (data && *size >= need) {
            memcpy(data, wdata, need);
        }
        if (data && *size < need)
            error = ERROR_MORE_DATA;
        if (size)
            *size = need;
        if (type)
            *type = wtype;
    }
    if (error && data && (flags & RRF_ZEROONFAILURE))
        memset(data, 0, *size);
    KbFree(wdata);
    KbFree(wsub);
    KbFree(wvalue);
    return error;
}

LSTATUS WINAPI RegSetKeyValueW(HKEY key, LPCWSTR subkey, LPCWSTR value, DWORD type, LPCVOID data, DWORD size)
{
    HKEY target = key;
    if (subkey && *subkey) {
        LONG error = RegCreateKeyExW(key, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &target, nullptr);
        if (error)
            return error;
    }
    LONG error = RegSetValueExW(target, value, 0, type, data, size);
    if (target != key)
        RegCloseKey(target);
    return error;
}

LSTATUS WINAPI RegSetKeyValueA(HKEY key, LPCSTR subkey, LPCSTR value, DWORD type, LPCVOID data, DWORD size)
{
    HKEY target = key;
    if (subkey && *subkey) {
        LONG error = RegCreateKeyExA(key, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &target, nullptr);
        if (error)
            return error;
    }
    LONG error = RegSetValueExA(target, value, 0, type, data, size);
    if (target != key)
        RegCloseKey(target);
    return error;
}

LSTATUS WINAPI RegDeleteValueW(HKEY key, LPCWSTR name)
{
    HANDLE h;
    LONG error = resolve_key(key, &h);
    if (error)
        return error;
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, name ? name : L"");
    return to_error(NtDeleteValueKey(h, &us));
}

LSTATUS WINAPI RegDeleteValueA(HKEY key, LPCSTR name)
{
    PWSTR w = KbAnsiToWideCp(CP_ACP, name);
    if (name && !w)
        return ERROR_NOT_ENOUGH_MEMORY;
    LONG error = RegDeleteValueW(key, w);
    KbFree(w);
    return error;
}

LSTATUS WINAPI RegDeleteKeyValueW(HKEY key, LPCWSTR subkey, LPCWSTR value)
{
    HKEY target = key;
    if (subkey && *subkey) {
        LONG error = RegOpenKeyExW(key, subkey, 0, KEY_SET_VALUE, &target);
        if (error)
            return error;
    }
    LONG error = RegDeleteValueW(target, value);
    if (target != key)
        RegCloseKey(target);
    return error;
}

LSTATUS WINAPI RegDeleteKeyValueA(HKEY key, LPCSTR subkey, LPCSTR value)
{
    PWSTR wsub = KbAnsiToWideCp(CP_ACP, subkey);
    PWSTR wvalue = KbAnsiToWideCp(CP_ACP, value);
    LONG error = RegDeleteKeyValueW(key, wsub, wvalue);
    KbFree(wsub);
    KbFree(wvalue);
    return error;
}

/* ------------------------------------------------------------------ enumeration */

static LONG copy_name(LPCWSTR name, ULONG bytes, LPWSTR out, LPDWORD out_size)
{
    DWORD chars = bytes / sizeof(WCHAR);
    if (*out_size <= chars) {
        *out_size = chars;
        return ERROR_MORE_DATA;
    }
    memcpy(out, name, bytes);
    out[chars] = 0;
    *out_size = chars;
    return ERROR_SUCCESS;
}

static LONG query_buffer(NTSTATUS (NTAPI *call)(HANDLE, ULONG, ULONG, PVOID, ULONG, PULONG), HANDLE key, ULONG index,
                         ULONG klass, PVOID *out)
{
    ULONG length = 512;
    for (;;) {
        PVOID info = KbAlloc(length);
        if (!info)
            return ERROR_NOT_ENOUGH_MEMORY;
        ULONG returned = 0;
        NTSTATUS status = call(key, index, klass, info, length, &returned);
        if (NT_SUCCESS(status)) {
            *out = info;
            return ERROR_SUCCESS;
        }
        KbFree(info);
        if (status == STATUS_BUFFER_OVERFLOW || status == STATUS_BUFFER_TOO_SMALL) {
            length = returned > length ? returned : length * 2;
            if (length > 0x10000000)
                return ERROR_OUTOFMEMORY;
            continue;
        }
        if (status == STATUS_NO_MORE_ENTRIES)
            return ERROR_NO_MORE_ITEMS;
        return to_error(status);
    }
}

LSTATUS WINAPI RegEnumKeyExW(HKEY key, DWORD index, LPWSTR name, LPDWORD name_size, LPDWORD reserved, LPWSTR klass,
                             LPDWORD class_size, PFILETIME last_write)
{
    if (!name || !name_size || reserved || (klass && !class_size))
        return ERROR_INVALID_PARAMETER;
    HANDLE h;
    LONG error = resolve_key(key, &h);
    if (error)
        return error;
    KB_KEY_NODE *info;
    error = query_buffer(NtEnumerateKey, h, index, KEY_NODE_INFORMATION_CLASS, (PVOID *)&info);
    if (error)
        return error;
    error = copy_name(info->Name, info->NameLength, name, name_size);
    if (!error && klass)
        error = copy_name((LPCWSTR)((PBYTE)info + info->ClassOffset), info->ClassLength, klass, class_size);
    else if (!error && class_size)
        *class_size = info->ClassLength / sizeof(WCHAR);
    if (!error && last_write) {
        last_write->dwLowDateTime = info->LastWriteTime.LowPart;
        last_write->dwHighDateTime = (DWORD)info->LastWriteTime.HighPart;
    }
    KbFree(info);
    return error;
}

static LONG narrow_name(LPCWSTR wide, LPSTR out, LPDWORD out_size)
{
    int need = WideCharToMultiByte(CP_ACP, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 0)
        return ERROR_INVALID_PARAMETER;
    if (*out_size < (DWORD)need) {
        *out_size = (DWORD)need - 1;
        return ERROR_MORE_DATA;
    }
    WideCharToMultiByte(CP_ACP, 0, wide, -1, out, (int)*out_size, nullptr, nullptr);
    *out_size = (DWORD)need - 1;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegEnumKeyExA(HKEY key, DWORD index, LPSTR name, LPDWORD name_size, LPDWORD reserved, LPSTR klass,
                             LPDWORD class_size, PFILETIME last_write)
{
    if (!name || !name_size || reserved || (klass && !class_size))
        return ERROR_INVALID_PARAMETER;
    WCHAR wname[256], wclass[256];
    DWORD wname_size = RTL_NUMBER_OF(wname), wclass_size = RTL_NUMBER_OF(wclass);
    LONG error = RegEnumKeyExW(key, index, wname, &wname_size, nullptr, klass ? wclass : nullptr, klass ? &wclass_size : nullptr,
                               last_write);
    if (error)
        return error;
    error = narrow_name(wname, name, name_size);
    if (!error && klass)
        error = narrow_name(wclass, klass, class_size);
    return error;
}

LSTATUS WINAPI RegEnumKeyW(HKEY key, DWORD index, LPWSTR name, DWORD size)
{
    DWORD n = size;
    return RegEnumKeyExW(key, index, name, &n, nullptr, nullptr, nullptr, nullptr);
}

LSTATUS WINAPI RegEnumKeyA(HKEY key, DWORD index, LPSTR name, DWORD size)
{
    DWORD n = size;
    return RegEnumKeyExA(key, index, name, &n, nullptr, nullptr, nullptr, nullptr);
}

LSTATUS WINAPI RegEnumValueW(HKEY key, DWORD index, LPWSTR name, LPDWORD name_size, LPDWORD reserved, LPDWORD type,
                             LPBYTE data, LPDWORD data_size)
{
    if (!name || !name_size || reserved || (data && !data_size))
        return ERROR_INVALID_PARAMETER;
    HANDLE h;
    LONG error = resolve_key(key, &h);
    if (error)
        return error;
    KB_KEY_VALUE_FULL *info;
    error = query_buffer(NtEnumerateValueKey, h, index, KEY_VALUE_FULL_INFORMATION_CLASS, (PVOID *)&info);
    if (error)
        return error;
    error = copy_name(info->Name, info->NameLength, name, name_size);
    if (!error) {
        if (type)
            *type = info->Type;
        if (data) {
            if (*data_size < info->DataLength)
                error = ERROR_MORE_DATA;
            else
                memcpy(data, (PBYTE)info + info->DataOffset, info->DataLength);
        }
        if (data_size)
            *data_size = info->DataLength;
    }
    KbFree(info);
    return error;
}

LSTATUS WINAPI RegEnumValueA(HKEY key, DWORD index, LPSTR name, LPDWORD name_size, LPDWORD reserved, LPDWORD type,
                             LPBYTE data, LPDWORD data_size)
{
    if (!name || !name_size || reserved || (data && !data_size))
        return ERROR_INVALID_PARAMETER;
    WCHAR wname[16384];
    DWORD wname_size = RTL_NUMBER_OF(wname), wtype = 0, wdata_size = 0;
    LONG error = RegEnumValueW(key, index, wname, &wname_size, nullptr, &wtype, nullptr, &wdata_size);
    if (error)
        return error;
    PBYTE wdata = KbAlloc(wdata_size + sizeof(WCHAR));
    if (!wdata)
        return ERROR_NOT_ENOUGH_MEMORY;
    wname_size = RTL_NUMBER_OF(wname);
    error = RegEnumValueW(key, index, wname, &wname_size, nullptr, &wtype, wdata, &wdata_size);
    if (!error)
        error = narrow_name(wname, name, name_size);
    if (!error) {
        DWORD need = wdata_size;
        if (is_string_type(wtype))
            need = wdata_size ? (DWORD)WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)wdata, (int)(wdata_size / sizeof(WCHAR)),
                                                           nullptr, 0, nullptr, nullptr)
                              : 0;
        if (data) {
            if (*data_size < need)
                error = ERROR_MORE_DATA;
            else if (is_string_type(wtype) && need)
                WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)wdata, (int)(wdata_size / sizeof(WCHAR)), (LPSTR)data,
                                    (int)*data_size, nullptr, nullptr);
            else
                memcpy(data, wdata, need);
        }
        if (data_size)
            *data_size = need;
        if (type)
            *type = wtype;
    }
    KbFree(wdata);
    return error;
}

static NTSTATUS NTAPI query_key_call(HANDLE key, ULONG index, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    return NtQueryKey(key, klass, info, length, returned);
}

LSTATUS WINAPI RegQueryInfoKeyW(HKEY key, LPWSTR klass, LPDWORD class_size, LPDWORD reserved, LPDWORD subkeys,
                                LPDWORD max_subkey, LPDWORD max_class, LPDWORD values, LPDWORD max_value_name,
                                LPDWORD max_value, LPDWORD security, PFILETIME last_write)
{
    if (reserved || (klass && !class_size))
        return ERROR_INVALID_PARAMETER;
    HANDLE h;
    LONG error = resolve_key(key, &h);
    if (error)
        return error;
    KB_KEY_FULL *info;
    error = query_buffer(query_key_call, h, 0, KEY_FULL_INFORMATION_CLASS, (PVOID *)&info);
    if (error)
        return error;
    if (klass)
        error = copy_name((LPCWSTR)((PBYTE)info + info->ClassOffset), info->ClassLength, klass, class_size);
    else if (class_size)
        *class_size = info->ClassLength / sizeof(WCHAR);
    if (subkeys)
        *subkeys = info->SubKeys;
    if (max_subkey)
        *max_subkey = info->MaxNameLen / sizeof(WCHAR);
    if (max_class)
        *max_class = info->MaxClassLen / sizeof(WCHAR);
    if (values)
        *values = info->Values;
    if (max_value_name)
        *max_value_name = info->MaxValueNameLen / sizeof(WCHAR);
    if (max_value)
        *max_value = info->MaxValueDataLen;
    if (security)
        *security = 0;
    if (last_write) {
        last_write->dwLowDateTime = info->LastWriteTime.LowPart;
        last_write->dwHighDateTime = (DWORD)info->LastWriteTime.HighPart;
    }
    KbFree(info);
    return error;
}

LSTATUS WINAPI RegQueryInfoKeyA(HKEY key, LPSTR klass, LPDWORD class_size, LPDWORD reserved, LPDWORD subkeys,
                                LPDWORD max_subkey, LPDWORD max_class, LPDWORD values, LPDWORD max_value_name,
                                LPDWORD max_value, LPDWORD security, PFILETIME last_write)
{
    if (reserved || (klass && !class_size))
        return ERROR_INVALID_PARAMETER;
    WCHAR wclass[256];
    DWORD wclass_size = RTL_NUMBER_OF(wclass);
    LONG error = RegQueryInfoKeyW(key, klass ? wclass : nullptr, klass ? &wclass_size : nullptr, nullptr, subkeys, max_subkey,
                                  max_class, values, max_value_name, max_value, security, last_write);
    if (!error && klass)
        error = narrow_name(wclass, klass, class_size);
    else if (!error && class_size)
        RegQueryInfoKeyW(key, nullptr, class_size, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    return error;
}

/* ------------------------------------------------------------------ deletion and maintenance */

LSTATUS WINAPI RegDeleteKeyExW(HKEY key, LPCWSTR subkey, REGSAM access, DWORD reserved)
{
    if (!subkey)
        return ERROR_INVALID_PARAMETER;
    HKEY target;
    LONG error = RegOpenKeyExW(key, subkey, 0, DELETE | (access & (KEY_WOW64_32KEY | KEY_WOW64_64KEY)), &target);
    if (error)
        return error;
    if (is_predefined(target))
        return ERROR_ACCESS_DENIED;
    error = to_error(NtDeleteKey((HANDLE)target));
    RegCloseKey(target);
    return error;
}

LSTATUS WINAPI RegDeleteKeyW(HKEY key, LPCWSTR subkey)
{
    return RegDeleteKeyExW(key, subkey, 0, 0);
}

LSTATUS WINAPI RegDeleteKeyExA(HKEY key, LPCSTR subkey, REGSAM access, DWORD reserved)
{
    if (!subkey)
        return ERROR_INVALID_PARAMETER;
    PWSTR w = KbAnsiToWideCp(CP_ACP, subkey);
    if (!w)
        return ERROR_NOT_ENOUGH_MEMORY;
    LONG error = RegDeleteKeyExW(key, w, access, reserved);
    KbFree(w);
    return error;
}

LSTATUS WINAPI RegDeleteKeyA(HKEY key, LPCSTR subkey)
{
    return RegDeleteKeyExA(key, subkey, 0, 0);
}

static LONG delete_tree(HKEY key)
{
    WCHAR name[256];
    for (;;) {
        DWORD size = RTL_NUMBER_OF(name);
        LONG error = RegEnumKeyExW(key, 0, name, &size, nullptr, nullptr, nullptr, nullptr);
        if (error == ERROR_NO_MORE_ITEMS)
            break;
        if (error)
            return error;
        HKEY child;
        error = RegOpenKeyExW(key, name, 0, KEY_READ | KEY_SET_VALUE | DELETE, &child);
        if (error)
            return error;
        error = delete_tree(child);
        if (!error)
            error = to_error(NtDeleteKey((HANDLE)child));
        RegCloseKey(child);
        if (error)
            return error;
    }
    for (;;) {
        DWORD size = RTL_NUMBER_OF(name);
        LONG error = RegEnumValueW(key, 0, name, &size, nullptr, nullptr, nullptr, nullptr);
        if (error == ERROR_NO_MORE_ITEMS)
            return ERROR_SUCCESS;
        if (error)
            return error;
        error = RegDeleteValueW(key, name);
        if (error)
            return error;
    }
}

LSTATUS WINAPI RegDeleteTreeW(HKEY key, LPCWSTR subkey)
{
    HKEY target = key;
    if (subkey && *subkey) {
        LONG error = RegOpenKeyExW(key, subkey, 0, KEY_READ | KEY_SET_VALUE | DELETE, &target);
        if (error)
            return error;
    }
    LONG error = delete_tree(target);
    if (!error && target != key)
        error = to_error(NtDeleteKey((HANDLE)target));
    if (target != key)
        RegCloseKey(target);
    return error;
}

LSTATUS WINAPI RegDeleteTreeA(HKEY key, LPCSTR subkey)
{
    PWSTR w = KbAnsiToWideCp(CP_ACP, subkey);
    if (subkey && !w)
        return ERROR_NOT_ENOUGH_MEMORY;
    LONG error = RegDeleteTreeW(key, w);
    KbFree(w);
    return error;
}

LSTATUS WINAPI RegFlushKey(HKEY key)
{
    HANDLE h;
    LONG error = resolve_key(key, &h);
    if (error)
        return error;
    return to_error(NtFlushKey(h));
}

LSTATUS WINAPI RegNotifyChangeKeyValue(HKEY key, BOOL subtree, DWORD filter, HANDLE event, BOOL async)
{
    if (key == HKEY_PERFORMANCE_DATA)
        return ERROR_INVALID_HANDLE;
    if (async && !event)
        return ERROR_INVALID_PARAMETER;
    HANDLE h;
    LONG error = resolve_key(key, &h);
    if (error)
        return error;
    /* every request shares one status block, as on Windows, since the call returns before the answer comes. */
    static IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtNotifyChangeKey(h, event, nullptr, nullptr, &iosb, filter, (BOOLEAN)subtree, nullptr, 0, (BOOLEAN)async);
    return to_error(status);
}

/* loads a hive under HKLM or HKU, the kernel checks the caller's privilege. */
DECLSPEC_EXPORT LSTATUS WINAPI RegLoadKeyW(HKEY key, LPCWSTR subkey, LPCWSTR file)
{
    if (key == HKEY_PERFORMANCE_DATA)
        return ERROR_INVALID_HANDLE;
    HANDLE root;
    LONG error = resolve_key(key, &root);
    if (error)
        return error;
    UNICODE_STRING name, path;
    NTSTATUS status = RtlInitUnicodeStringEx(&name, subkey);
    if (NT_SUCCESS(status))
        status = RtlInitUnicodeStringEx(&path, file);
    if (!NT_SUCCESS(status))
        return to_error(status);
    UNICODE_STRING nt_path;
    if (!RtlDosPathNameToNtPathName_U(file, &nt_path, nullptr, nullptr))
        return ERROR_PATH_NOT_FOUND;
    OBJECT_ATTRIBUTES target, source;
    InitializeObjectAttributes(&target, &name, OBJ_CASE_INSENSITIVE, root, nullptr);
    InitializeObjectAttributes(&source, &nt_path, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    status = NtLoadKey(&target, &source);
    RtlFreeUnicodeString(&nt_path);
    return to_error(status);
}

DECLSPEC_EXPORT LSTATUS WINAPI RegLoadKeyA(HKEY key, LPCSTR subkey, LPCSTR file)
{
    PWSTR s = KbAnsiToWideCp(CP_ACP, subkey), f = KbAnsiToWideCp(CP_ACP, file);
    LONG error = (subkey && !s) || (file && !f) ? ERROR_NOT_ENOUGH_MEMORY : RegLoadKeyW(key, s, f);
    KbFree(s);
    KbFree(f);
    return error;
}

DECLSPEC_EXPORT LSTATUS WINAPI RegUnLoadKeyW(HKEY key, LPCWSTR subkey)
{
    if (key == HKEY_PERFORMANCE_DATA)
        return ERROR_INVALID_HANDLE;
    HANDLE root;
    LONG error = resolve_key(key, &root);
    if (error)
        return error;
    UNICODE_STRING name;
    NTSTATUS status = RtlInitUnicodeStringEx(&name, subkey);
    if (!NT_SUCCESS(status))
        return to_error(status);
    if (name.Length && !name.Buffer)
        return ERROR_INVALID_PARAMETER;
    OBJECT_ATTRIBUTES target;
    InitializeObjectAttributes(&target, &name, OBJ_CASE_INSENSITIVE, root, nullptr);
    return to_error(NtUnloadKey(&target));
}

DECLSPEC_EXPORT LSTATUS WINAPI RegUnLoadKeyA(HKEY key, LPCSTR subkey)
{
    PWSTR s = KbAnsiToWideCp(CP_ACP, subkey);
    LONG error = subkey && !s ? ERROR_NOT_ENOUGH_MEMORY : RegUnLoadKeyW(key, s);
    KbFree(s);
    return error;
}

/*
 * RegLoadMUIStringW: a value of the form "@file,-id" is loaded from that resource file. pcbData gets the size in bytes
 * with the terminator. A short buffer is ERROR_MORE_DATA, unless REG_MUI_STRING_TRUNCATE cuts and terminates the
 * string, in which case no size is asked for. A value that is no such reference, or of another type, is
 * ERROR_INVALID_DATA. The language directory is not searched, and packaged app strings "@{...}" are not supported.
 */
HRESULT WINAPI SHLoadIndirectString(LPCWSTR source, LPWSTR out, UINT size, void **reserved);

LSTATUS WINAPI RegLoadMUIStringW(HKEY key, LPCWSTR value, LPWSTR out, DWORD out_size, LPDWORD needed, DWORD flags,
                                 LPCWSTR directory)
{
    if ((out_size && (!out || (out_size & 1))) || (flags & ~REG_MUI_STRING_TRUNCATE))
        return ERROR_INVALID_PARAMETER;
    BOOL truncate = (flags & REG_MUI_STRING_TRUNCATE) != 0;
    if (truncate == (needed != nullptr))
        return ERROR_INVALID_PARAMETER;
    WCHAR reference[1024];
    DWORD bytes = sizeof(reference), type = 0;
    LONG error = RegGetValueW(key, nullptr, value, RRF_RT_ANY | RRF_NOEXPAND, &type, reference, &bytes);
    if (error)
        return error;
    if (type != REG_SZ && type != REG_EXPAND_SZ)
        return ERROR_INVALID_DATA;
    reference[RTL_NUMBER_OF(reference) - 1] = 0;
    /* only the digits after the comma belong to the reference. */
    WCHAR *comma = wcschr(reference, L',');
    if (comma && comma[1]) {
        WCHAR *digits = comma + 2;
        while (*digits >= L'0' && *digits <= L'9')
            digits++;
        *digits = 0;
    }
    if (reference[0] != L'@')
        return ERROR_INVALID_DATA;
    WCHAR text[4096];
    HRESULT hr = SHLoadIndirectString(reference, text, RTL_NUMBER_OF(text), nullptr);
    if (FAILED(hr))
        return HRESULT_FACILITY(hr) == FACILITY_WIN32 ? (LSTATUS)HRESULT_CODE(hr) : ERROR_INVALID_DATA;
    DWORD length = (DWORD)wcslen(text), size = (length + 1) * sizeof(WCHAR);
    if (needed)
        *needed = size;
    if (!out_size)
        return needed ? ERROR_MORE_DATA : ERROR_SUCCESS;
    if (size > out_size) {
        if (!truncate)
            return ERROR_MORE_DATA;
        length = out_size / sizeof(WCHAR) - 1;
    }
    memcpy(out, text, length * sizeof(WCHAR));
    out[length] = 0;
    return ERROR_SUCCESS;
}

/*
 * converts the value name and directory, asks for the wide string and converts it back. The size reported is in bytes
 * of the narrow string with its terminator, which is a guess.
 */
LSTATUS WINAPI RegLoadMUIStringA(HKEY key, LPCSTR value, LPSTR out, DWORD out_size, LPDWORD needed, DWORD flags,
                                 LPCSTR directory)
{
    if ((out_size && !out) || (flags & ~REG_MUI_STRING_TRUNCATE) || ((flags & REG_MUI_STRING_TRUNCATE) != 0) == (needed != nullptr))
        return ERROR_INVALID_PARAMETER;
    PWSTR wvalue = value ? KbAnsiToWide(value) : nullptr;
    PWSTR wdirectory = directory ? KbAnsiToWide(directory) : nullptr;
    if ((value && !wvalue) || (directory && !wdirectory)) {
        KbFree(wvalue);
        KbFree(wdirectory);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    WCHAR text[4096];
    DWORD wide_needed = 0;
    LSTATUS error = RegLoadMUIStringW(key, wvalue, text, sizeof(text), &wide_needed,
                                      0, wdirectory);
    KbFree(wvalue);
    KbFree(wdirectory);
    if (error)
        return error;
    int bytes = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (needed)
        *needed = (DWORD)bytes;
    if (!out_size)
        return needed ? ERROR_MORE_DATA : ERROR_SUCCESS;
    if ((DWORD)bytes > out_size) {
        if (!(flags & REG_MUI_STRING_TRUNCATE))
            return ERROR_MORE_DATA;
        char *wide_copy = KbAlloc((SIZE_T)bytes);
        if (!wide_copy)
            return ERROR_NOT_ENOUGH_MEMORY;
        WideCharToMultiByte(CP_ACP, 0, text, -1, wide_copy, bytes, nullptr, nullptr);
        memcpy(out, wide_copy, out_size - 1);
        out[out_size - 1] = 0;
        KbFree(wide_copy);
        return ERROR_SUCCESS;
    }
    WideCharToMultiByte(CP_ACP, 0, text, -1, out, (int)out_size, nullptr, nullptr);
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ copying and hive files */

static LSTATUS copy_values(HKEY from, HKEY to)
{
    DWORD values, name_max, data_max;
    LSTATUS error = RegQueryInfoKeyW(from, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &values, &name_max,
                                     &data_max, nullptr, nullptr);
    if (error)
        return error;
    WCHAR *name = KbAlloc((name_max + 1) * sizeof(WCHAR));
    BYTE *data = KbAlloc(data_max ? data_max : 1);
    if (!name || !data) {
        KbFree(name);
        KbFree(data);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    for (DWORD i = 0; i < values && !error; i++) {
        DWORD name_size = name_max + 1, data_size = data_max, type;
        error = RegEnumValueW(from, i, name, &name_size, nullptr, &type, data, &data_size);
        if (!error)
            error = RegSetValueExW(to, name, 0, type, data, data_size);
    }
    KbFree(name);
    KbFree(data);
    return error;
}

/* the copy keeps the source's class and values. */
static LSTATUS copy_key(HKEY from, HKEY to_parent, LPCWSTR name, HKEY *copy_from, HKEY *copy_to, int depth)
{
    HKEY source = from, target = to_parent;
    LSTATUS error = ERROR_SUCCESS;
    if (name) {
        error = RegOpenKeyExW(from, name, 0, KEY_READ, &source);
        if (error)
            return error;
        WCHAR klass[256];
        DWORD class_size = RTL_NUMBER_OF(klass);
        klass[0] = 0;
        if (RegQueryInfoKeyW(source, klass, &class_size, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                             nullptr, nullptr))
            klass[0] = 0;
        error = RegCreateKeyExW(to_parent, name, 0, klass, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &target,
                                nullptr);
        if (error) {
            RegCloseKey(source);
            return error;
        }
    }
    error = copy_values(source, target);
    if (!error && depth < 512) {
        DWORD subkeys, name_max;
        error = RegQueryInfoKeyW(source, nullptr, nullptr, nullptr, &subkeys, &name_max, nullptr, nullptr, nullptr, nullptr,
                                 nullptr, nullptr);
        WCHAR *child = error ? nullptr : KbAlloc((name_max + 1) * sizeof(WCHAR));
        if (!error && !child)
            error = ERROR_NOT_ENOUGH_MEMORY;
        for (DWORD i = 0; !error && i < subkeys; i++) {
            DWORD size = name_max + 1;
            error = RegEnumKeyExW(source, i, child, &size, nullptr, nullptr, nullptr, nullptr);
            if (!error)
                error = copy_key(source, target, child, nullptr, nullptr, depth + 1);
        }
        KbFree(child);
    } else if (!error) {
        error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (name) {
        RegCloseKey(source);
        RegCloseKey(target);
    }
    return error;
}

/*
 * RegCopyTree: subkeys are created with the class of their source. Security descriptors are not copied and nothing in
 * the destination is deleted.
 */
LSTATUS WINAPI RegCopyTreeW(HKEY source, LPCWSTR subkey, HKEY destination)
{
    HKEY from = source;
    LSTATUS error = ERROR_SUCCESS;
    if (subkey && *subkey) {
        error = RegOpenKeyExW(source, subkey, 0, KEY_READ, &from);
        if (error)
            return error;
    }
    error = copy_key(from, destination, nullptr, nullptr, nullptr, 0);
    if (from != source)
        RegCloseKey(from);
    return error;
}

LSTATUS WINAPI RegCopyTreeA(HKEY source, LPCSTR subkey, HKEY destination)
{
    PWSTR wide = subkey ? KbAnsiToWide(subkey) : nullptr;
    if (subkey && !wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    LSTATUS error = RegCopyTreeW(source, wide, destination);
    KbFree(wide);
    return error;
}

/*
 * RegQueryMultipleValues: the data are packed one after another in the buffer, each entry gets its length, type and
 * data pointer, and total gets the length. A short buffer is ERROR_MORE_DATA with the length needed, a missing name
 * ERROR_FILE_NOT_FOUND.
 */
LSTATUS WINAPI RegQueryMultipleValuesW(HKEY key, VALENTW *list, DWORD count, LPWSTR buffer, LPDWORD total)
{
    if ((count && !list) || count > 0xFFFF)
        return ERROR_INVALID_PARAMETER;
    DWORD room = total ? *total : 0, needed = 0;
    BOOL fits = buffer != nullptr;
    if (!buffer)
        room = 0;
    for (DWORD i = 0; i < count; i++) {
        DWORD size = 0, type;
        LSTATUS error = RegQueryValueExW(key, list[i].ve_valuename, nullptr, &type, nullptr, &size);
        if (error)
            return error;
        list[i].ve_type = type;
        list[i].ve_valuelen = size;
        needed += size;
    }
    if (total)
        *total = needed;
    if (!fits || needed > room)
        return count ? ERROR_MORE_DATA : ERROR_SUCCESS;
    BYTE *at = (BYTE *)buffer;
    for (DWORD i = 0; i < count; i++) {
        DWORD size = list[i].ve_valuelen, type;
        LSTATUS error = RegQueryValueExW(key, list[i].ve_valuename, nullptr, &type, at, &size);
        if (error)
            return error;
        list[i].ve_type = type;
        list[i].ve_valuelen = size;
        list[i].ve_valueptr = (DWORD_PTR)at;
        at += size;
    }
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegQueryMultipleValuesA(HKEY key, VALENTA *list, DWORD count, LPSTR buffer, LPDWORD total)
{
    if ((count && !list) || count > 0xFFFF)
        return ERROR_INVALID_PARAMETER;
    DWORD room = buffer && total ? *total : 0, needed = 0;
    for (DWORD i = 0; i < count; i++) {
        DWORD size = 0, type;
        LSTATUS error = RegQueryValueExA(key, list[i].ve_valuename, nullptr, &type, nullptr, &size);
        if (error)
            return error;
        list[i].ve_type = type;
        list[i].ve_valuelen = size;
        needed += size;
    }
    if (total)
        *total = needed;
    if (!buffer || needed > room)
        return count ? ERROR_MORE_DATA : ERROR_SUCCESS;
    BYTE *at = (BYTE *)buffer;
    for (DWORD i = 0; i < count; i++) {
        DWORD size = list[i].ve_valuelen, type;
        LSTATUS error = RegQueryValueExA(key, list[i].ve_valuename, nullptr, &type, at, &size);
        if (error)
            return error;
        list[i].ve_type = type;
        list[i].ve_valuelen = size;
        list[i].ve_valueptr = (DWORD_PTR)at;
        at += size;
    }
    return ERROR_SUCCESS;
}

/*
 * Windows loads a user's classes as the <sid>_Classes hive. There is none here, so the user's own Software\Classes key
 * is opened instead when the hive is not found.
 */
NTSYSAPI NTSTATUS NTAPI RtlConvertSidToUnicodeString(PUNICODE_STRING out, PVOID sid, BOOLEAN allocate);

LSTATUS WINAPI RegOpenUserClassesRoot(HANDLE token, DWORD options, REGSAM access, HKEY *result)
{
    if (!result || !token || options)
        return ERROR_INVALID_PARAMETER;
    /* TOKEN_USER starts with the pointer to the user's sid. */
    UCHAR buffer[256];
    ULONG length = 0;
    NTSTATUS status = NtQueryInformationToken(token, 1, buffer, sizeof(buffer), &length);
    if (!NT_SUCCESS(status))
        return RtlNtStatusToDosError(status);
    UNICODE_STRING sid;
    status = RtlConvertSidToUnicodeString(&sid, *(PVOID *)buffer, TRUE);
    if (!NT_SUCCESS(status))
        return RtlNtStatusToDosError(status);
    WCHAR path[200];
    KbFormat(path, RTL_NUMBER_OF(path), L"\\Registry\\User\\%wZ_Classes", &sid);
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, path);
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, &name, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    HANDLE handle;
    status = NtOpenKey(&handle, access, &attributes);
    if (status == STATUS_OBJECT_NAME_NOT_FOUND || status == STATUS_OBJECT_PATH_NOT_FOUND) {
        KbFormat(path, RTL_NUMBER_OF(path), L"\\Registry\\User\\%wZ\\Software\\Classes", &sid);
        RtlInitUnicodeString(&name, path);
        status = NtOpenKey(&handle, access, &attributes);
    }
    RtlFreeUnicodeString(&sid);
    if (!NT_SUCCESS(status))
        return RtlNtStatusToDosError(status);
    *result = (HKEY)handle;
    return ERROR_SUCCESS;
}

/*
 * there are no hive files here. Saving, restoring and replacing hives need a privilege no token holds and fail with
 * that. Loading an application hive needs none: it fails for a missing file and reports not supported otherwise.
 */
static LSTATUS hive_privilege(HKEY key, LPCWSTR file)
{
    if (key == HKEY_PERFORMANCE_DATA)
        return ERROR_INVALID_HANDLE;
    if (!file)
        return ERROR_INVALID_PARAMETER;
    return ERROR_PRIVILEGE_NOT_HELD;
}

LSTATUS WINAPI RegSaveKeyExW(HKEY key, LPCWSTR file, LPSECURITY_ATTRIBUTES attributes, DWORD flags)
{
    if (flags > 4)
        return ERROR_INVALID_PARAMETER;
    return hive_privilege(key, file);
}

LSTATUS WINAPI RegSaveKeyW(HKEY key, LPCWSTR file, LPSECURITY_ATTRIBUTES attributes)
{
    return hive_privilege(key, file);
}

LSTATUS WINAPI RegRestoreKeyW(HKEY key, LPCWSTR file, DWORD flags)
{
    return hive_privilege(key, file);
}

LSTATUS WINAPI RegReplaceKeyW(HKEY key, LPCWSTR subkey, LPCWSTR new_file, LPCWSTR old_file)
{
    return hive_privilege(key, new_file ? old_file : nullptr);
}

LSTATUS WINAPI RegSaveKeyExA(HKEY key, LPCSTR file, LPSECURITY_ATTRIBUTES attributes, DWORD flags)
{
    PWSTR wide = file ? KbAnsiToWide(file) : nullptr;
    LSTATUS error = file && !wide ? ERROR_NOT_ENOUGH_MEMORY : RegSaveKeyExW(key, wide, attributes, flags);
    KbFree(wide);
    return error;
}

LSTATUS WINAPI RegSaveKeyA(HKEY key, LPCSTR file, LPSECURITY_ATTRIBUTES attributes)
{
    return RegSaveKeyExA(key, file, attributes, 1);
}

LSTATUS WINAPI RegRestoreKeyA(HKEY key, LPCSTR file, DWORD flags)
{
    PWSTR wide = file ? KbAnsiToWide(file) : nullptr;
    LSTATUS error = file && !wide ? ERROR_NOT_ENOUGH_MEMORY : RegRestoreKeyW(key, wide, flags);
    KbFree(wide);
    return error;
}

LSTATUS WINAPI RegReplaceKeyA(HKEY key, LPCSTR subkey, LPCSTR new_file, LPCSTR old_file)
{
    PWSTR sub = subkey ? KbAnsiToWide(subkey) : nullptr, new_wide = new_file ? KbAnsiToWide(new_file) : nullptr,
          old_wide = old_file ? KbAnsiToWide(old_file) : nullptr;
    LSTATUS error = ERROR_NOT_ENOUGH_MEMORY;
    if ((!subkey || sub) && (!new_file || new_wide) && (!old_file || old_wide))
        error = RegReplaceKeyW(key, sub, new_wide, old_wide);
    KbFree(sub);
    KbFree(new_wide);
    KbFree(old_wide);
    return error;
}

LSTATUS WINAPI RegLoadAppKeyW(LPCWSTR file, HKEY *result, REGSAM access, DWORD options, DWORD reserved)
{
    if (reserved || (options & ~3u) || !result || !file)
        return ERROR_INVALID_PARAMETER;
    DWORD attributes = GetFileAttributesW(file);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return GetLastError();
    UNICODE_STRING nt_path;
    if (!RtlDosPathNameToNtPathName_U(file, &nt_path, nullptr, nullptr))
        return ERROR_PATH_NOT_FOUND;
    /* the root of the hive is a key of its own below \Registry\A, named at random. */
    LUID id;
    NTSTATUS status = NtAllocateLocallyUniqueId(&id);
    WCHAR name[80];
    KbFormat(name, RTL_NUMBER_OF(name), L"\\Registry\\A\\{%08x%08x}", id.HighPart, id.LowPart);
    UNICODE_STRING target_name;
    RtlInitUnicodeString(&target_name, name);
    OBJECT_ATTRIBUTES target, source;
    InitializeObjectAttributes(&target, &target_name, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    InitializeObjectAttributes(&source, &nt_path, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    HANDLE root = nullptr;
    if (NT_SUCCESS(status))
        status = NtLoadKeyEx(&target, &source, 0x10, nullptr, nullptr, access, &root, nullptr);
    RtlFreeUnicodeString(&nt_path);
    if (!NT_SUCCESS(status))
        return to_error(status);
    *result = (HKEY)root;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegLoadAppKeyA(LPCSTR file, HKEY *result, REGSAM access, DWORD options, DWORD reserved)
{
    PWSTR wide = file ? KbAnsiToWide(file) : nullptr;
    LSTATUS error = file && !wide ? ERROR_NOT_ENOUGH_MEMORY : RegLoadAppKeyW(wide, result, access, options, reserved);
    KbFree(wide);
    return error;
}
