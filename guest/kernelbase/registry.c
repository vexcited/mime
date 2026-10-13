/*
 * registry functions over the Nt key services.
 */
#include "kernelbase_private.h"

NTSYSCALLAPI NTSTATUS NTAPI NtEnumerateKey(HANDLE key, ULONG index, ULONG klass, PVOID info, ULONG length,
                                           PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtEnumerateValueKey(HANDLE key, ULONG index, ULONG klass, PVOID info, ULONG length,
                                                PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtQueryKey(HANDLE key, ULONG klass, PVOID info, ULONG length, PULONG returned);
NTSYSCALLAPI NTSTATUS NTAPI NtSetValueKey(HANDLE key, PUNICODE_STRING name, ULONG title_index, ULONG type,
                                          PVOID data, ULONG size);
NTSYSCALLAPI NTSTATUS NTAPI NtDeleteKey(HANDLE key);
NTSYSAPI NTSTATUS NTAPI RtlOpenCurrentUser(ACCESS_MASK access, PHANDLE key);
NTSYSCALLAPI NTSTATUS NTAPI NtDeleteValueKey(HANDLE key, PUNICODE_STRING name);
NTSYSCALLAPI NTSTATUS NTAPI NtFlushKey(HANDLE key);
NTSYSCALLAPI NTSTATUS NTAPI NtNotifyChangeKey(HANDLE key, HANDLE event, PVOID apc_routine, PVOID apc_context,
                                              PIO_STATUS_BLOCK iosb, ULONG filter, BOOLEAN subtree, PVOID buffer,
                                              ULONG length, BOOLEAN async);

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
    NULL, /* HKEY_CURRENT_USER, the key of the token's user */
    L"\\Registry\\Machine",
    L"\\Registry\\User",
    NULL,
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Hardware Profiles\\Current",
    NULL,
    L"\\Registry\\User\\.Default\\Software\\Classes\\Local Settings",
};

static HANDLE g_predefined[PREDEFINED_COUNT];
static HANDLE g_overrides[PREDEFINED_COUNT];
static BOOL g_overridden[PREDEFINED_COUNT];

static LONG to_error(NTSTATUS status)
{
    return NT_SUCCESS(status) ? ERROR_SUCCESS : (LONG)RtlNtStatusToDosError(status);
}

static BOOL is_predefined(HKEY key)
{
    ULONG_PTR v = (ULONG_PTR)key;
    return (v & 0xFFFFFFFF80000000ull) == 0xFFFFFFFF80000000ull && (ULONG)v - PREDEFINED_FIRST < PREDEFINED_COUNT;
}

static LONG open_path(PCWSTR path, ACCESS_MASK access, HANDLE *out)
{
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES oa;
    RtlInitUnicodeString(&us, path);
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, NULL, NULL);
    return to_error(NtOpenKey(out, access, &oa));
}

/* resolves a key argument to an Nt handle; predefined keys are opened once and cached. */
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
    if (__atomic_compare_exchange_n(&g_predefined[index], &(HANDLE){NULL}, h, FALSE, __ATOMIC_ACQ_REL,
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
    if (options & ~(DWORD)REG_OPTION_OPEN_LINK)
        return ERROR_INVALID_PARAMETER;
    *result = NULL;
    if ((!subkey || !*subkey) && is_predefined(key)) {
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
                               root, NULL);
    HANDLE h;
    NTSTATUS status = NtOpenKey(&h, clean_access(access), &oa);
    if (!NT_SUCCESS(status) && !us.Length) {
        /* an empty name reopens the key itself. */
        status = NtDuplicateObject(NtCurrentProcess(), root, NtCurrentProcess(), &h, clean_access(access), 0, 0);
    }
    if (!NT_SUCCESS(status))
        return to_error(status);
    *result = (HKEY)h;
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
    status = create_key_path(&parent_key, KEY_CREATE_SUB_KEY, &parent_attributes, NULL,
                              options & ~(ULONG)REG_OPTION_CREATE_LINK, NULL);
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
    if (!result || !subkey || reserved)
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
                               root, sa ? sa->lpSecurityDescriptor : NULL);
    if (klass)
        RtlInitUnicodeString(&class_name, klass);
    HANDLE h;
    ULONG disp = 0;
    NTSTATUS status = create_key_path(&h, clean_access(access), &oa, klass ? &class_name : NULL,
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
    return RegCreateKeyExW(key, subkey, 0, NULL, 0, MAXIMUM_ALLOWED, NULL, result, NULL);
}

LSTATUS WINAPI RegCreateKeyA(HKEY key, LPCSTR subkey, HKEY *result)
{
    if (!result)
        return ERROR_INVALID_PARAMETER;
    if (!subkey || !*subkey) {
        *result = key;
        return ERROR_SUCCESS;
    }
    return RegCreateKeyExA(key, subkey, 0, NULL, 0, MAXIMUM_ALLOWED, NULL, result, NULL);
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
    if (!result)
        return ERROR_INVALID_PARAMETER;
    HANDLE h;
    LONG error = open_path(g_predefined_paths[1], clean_access(access), &h);
    *result = error ? HKEY_CURRENT_USER : (HKEY)h;
    return ERROR_SUCCESS;
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

/* queries one value into a heap buffer. */
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

LSTATUS WINAPI RegQueryValueExW(HKEY key, LPCWSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size)
{
    if (reserved || (data && !size))
        return ERROR_INVALID_PARAMETER;
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
        need = wchars ? (DWORD)WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)info->Data, wchars, NULL, 0, NULL, NULL) : 0;
        if (data) {
            if (*size < need)
                error = ERROR_MORE_DATA;
            else if (wchars)
                WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)info->Data, wchars, (LPSTR)data, (int)*size, NULL, NULL);
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
    /* a string missing its terminator gets one, like windows does. */
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
        int wchars = MultiByteToWideChar(CP_ACP, 0, (LPCSTR)data, (int)size, NULL, 0);
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
    if ((data && !size) || !(flags & RRF_RT_ANY) ||
        ((flags & RRF_RT_REG_EXPAND_SZ) && !(flags & RRF_NOEXPAND) && (flags & RRF_RT_ANY) != RRF_RT_ANY))
        return ERROR_INVALID_PARAMETER;
    DWORD capacity = data ? *size : 0;
    if (data && (flags & RRF_ZEROONFAILURE))
        memset(data, 0, capacity);
    HKEY opened = NULL;
    if (subkey && *subkey) {
        LONG error = RegOpenKeyExW(key, subkey, 0, KEY_QUERY_VALUE, &opened);
        if (error)
            return error;
        key = opened;
    }
    HANDLE h;
    LONG error = resolve_key(key, &h);
    KB_KEY_VALUE_PARTIAL *info = NULL;
    if (!error)
        error = query_value(h, value, &info);
    if (opened)
        RegCloseKey(opened);
    if (error)
        return error;
    DWORD actual_type = info->Type;
    PBYTE bytes = info->Data;
    DWORD length = info->DataLength;
    PWSTR expanded = NULL;
    if (actual_type == REG_EXPAND_SZ && !(flags & RRF_NOEXPAND)) {
        /* the expanded string is reported as REG_SZ. */
        DWORD chars = ExpandEnvironmentStringsW((LPCWSTR)bytes, NULL, 0);
        expanded = chars ? KbAlloc((SIZE_T)chars * sizeof(WCHAR)) : NULL;
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
    LONG error = RegGetValueW(key, wsub, wvalue, flags & ~(DWORD)RRF_ZEROONFAILURE, &wtype, NULL, &wsize);
    PBYTE wdata = NULL;
    if (!error) {
        wdata = KbAlloc(wsize + sizeof(WCHAR));
        error = wdata ? RegGetValueW(key, wsub, wvalue, flags & ~(DWORD)RRF_ZEROONFAILURE, &wtype, wdata, &wsize)
                      : ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!error) {
        DWORD need = wsize;
        if (is_string_type(wtype)) {
            int wchars = (int)(wsize / sizeof(WCHAR));
            need = (DWORD)WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)wdata, wchars, NULL, 0, NULL, NULL);
            if (data && *size >= need)
                WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)wdata, wchars, data, (int)*size, NULL, NULL);
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
        LONG error = RegCreateKeyExW(key, subkey, 0, NULL, 0, KEY_SET_VALUE, NULL, &target, NULL);
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
        LONG error = RegCreateKeyExA(key, subkey, 0, NULL, 0, KEY_SET_VALUE, NULL, &target, NULL);
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

static LONG query_buffer(NTSTATUS (*call)(HANDLE, ULONG, ULONG, PVOID, ULONG, PULONG), HANDLE key, ULONG index,
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
    int need = WideCharToMultiByte(CP_ACP, 0, wide, -1, NULL, 0, NULL, NULL);
    if (need <= 0)
        return ERROR_INVALID_PARAMETER;
    if (*out_size < (DWORD)need) {
        *out_size = (DWORD)need - 1;
        return ERROR_MORE_DATA;
    }
    WideCharToMultiByte(CP_ACP, 0, wide, -1, out, (int)*out_size, NULL, NULL);
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
    LONG error = RegEnumKeyExW(key, index, wname, &wname_size, NULL, klass ? wclass : NULL, klass ? &wclass_size : NULL,
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
    return RegEnumKeyExW(key, index, name, &n, NULL, NULL, NULL, NULL);
}

LSTATUS WINAPI RegEnumKeyA(HKEY key, DWORD index, LPSTR name, DWORD size)
{
    DWORD n = size;
    return RegEnumKeyExA(key, index, name, &n, NULL, NULL, NULL, NULL);
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
    LONG error = RegEnumValueW(key, index, wname, &wname_size, NULL, &wtype, NULL, &wdata_size);
    if (error)
        return error;
    PBYTE wdata = KbAlloc(wdata_size + sizeof(WCHAR));
    if (!wdata)
        return ERROR_NOT_ENOUGH_MEMORY;
    wname_size = RTL_NUMBER_OF(wname);
    error = RegEnumValueW(key, index, wname, &wname_size, NULL, &wtype, wdata, &wdata_size);
    if (!error)
        error = narrow_name(wname, name, name_size);
    if (!error) {
        DWORD need = wdata_size;
        if (is_string_type(wtype))
            need = wdata_size ? (DWORD)WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)wdata, (int)(wdata_size / sizeof(WCHAR)),
                                                           NULL, 0, NULL, NULL)
                              : 0;
        if (data) {
            if (*data_size < need)
                error = ERROR_MORE_DATA;
            else if (is_string_type(wtype) && need)
                WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)wdata, (int)(wdata_size / sizeof(WCHAR)), (LPSTR)data,
                                    (int)*data_size, NULL, NULL);
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

static NTSTATUS query_key_call(HANDLE key, ULONG index, ULONG klass, PVOID info, ULONG length, PULONG returned)
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
    LONG error = RegQueryInfoKeyW(key, klass ? wclass : NULL, klass ? &wclass_size : NULL, NULL, subkeys, max_subkey,
                                  max_class, values, max_value_name, max_value, security, last_write);
    if (!error && klass)
        error = narrow_name(wclass, klass, class_size);
    else if (!error && class_size)
        RegQueryInfoKeyW(key, NULL, class_size, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
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
        LONG error = RegEnumKeyExW(key, 0, name, &size, NULL, NULL, NULL, NULL);
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
        LONG error = RegEnumValueW(key, 0, name, &size, NULL, NULL, NULL, NULL);
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
    if (async && !event)
        return ERROR_INVALID_PARAMETER;
    HANDLE h;
    LONG error = resolve_key(key, &h);
    if (error)
        return error;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtNotifyChangeKey(h, event, NULL, NULL, &iosb, filter, (BOOLEAN)subtree, NULL, 0, (BOOLEAN)async);
    if (status == STATUS_PENDING)
        status = STATUS_SUCCESS;
    return to_error(status);
}
