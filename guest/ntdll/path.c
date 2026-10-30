/*
 * dos path handling: full path resolution, nt path conversion and the
 * process current directory.
 */
#include "ntdll_private.h"

static BOOLEAN is_sep(WCHAR c)
{
    return c == '\\' || c == '/';
}

NTDLL_EXPORT RTL_PATH_TYPE NTAPI RtlDetermineDosPathNameType_U(PCWSTR path)
{
    if (is_sep(path[0])) {
        if (!is_sep(path[1]))
            return RtlPathTypeRooted;
        if ((path[2] == '.' || path[2] == '?')) {
            if (is_sep(path[3]))
                return RtlPathTypeLocalDevice;
            if (!path[3])
                return RtlPathTypeRootLocalDevice;
        }
        return RtlPathTypeUncAbsolute;
    }
    if (path[0] && path[1] == ':')
        return is_sep(path[2]) ? RtlPathTypeDriveAbsolute : RtlPathTypeDriveRelative;
    return RtlPathTypeRelative;
}

/* never goes above root. */
static SIZE_T collapse(PWSTR path, SIZE_T root)
{
    SIZE_T in = root, out = root;
    SIZE_T len = wcslen(path);
    while (in < len) {
        SIZE_T end = in;
        while (end < len && path[end] != '\\')
            end++;
        SIZE_T n = end - in;
        if (n == 0 || (n == 1 && path[in] == '.')) {
            /* skip empty and current directory components. */
        } else if (n == 2 && path[in] == '.' && path[in + 1] == '.') {
            if (out > root) {
                out--;
                while (out > root && path[out - 1] != '\\')
                    out--;
            }
        } else {
            /* a component ending in a single period loses it: "Discord Inc.\x" is "Discord Inc\x". */
            if (end < len && path[end - 1] == '.' && path[end - 2] != '.')
                n--;
            memmove(path + out, path + in, n * sizeof(WCHAR));
            out += n;
            if (end < len)
                path[out++] = '\\';
        }
        in = end + 1;
    }
    /* keep a trailing separator only when the input had one. */
    if (out > root && path[out - 1] == '\\' && !(len && path[len - 1] == '\\'))
        out--;
    path[out] = 0;
    return out;
}

/* trailing dots and spaces are not part of win32 file names. */
static void trim_tail(PWSTR path, SIZE_T root)
{
    SIZE_T n = wcslen(path);
    while (n > root && (path[n - 1] == '.' || path[n - 1] == ' ') && !(n >= 2 && path[n - 2] == '.' && n - 1 == root))
        path[--n] = 0;
}

static NTSTATUS full_path(PCWSTR name, PWSTR out, SIZE_T out_chars, SIZE_T *length)
{
    PRTL_USER_PROCESS_PARAMETERS pp = NtCurrentPeb()->ProcessParameters;
    PCUNICODE_STRING cwd = &pp->CurrentDirectory.DosPath;
    WCHAR buf[MAX_PATH * 4];
    SIZE_T root = 0, n = 0;
    SIZE_T name_len = wcslen(name);
    if (name_len + MAX_PATH >= RTL_NUMBER_OF(buf))
        return STATUS_NAME_TOO_LONG;

    switch (RtlDetermineDosPathNameType_U(name)) {
    case RtlPathTypeLocalDevice:
        /* \\?\ paths are taken verbatim. */
        if (name[2] == '?') {
            memcpy(buf, name, (name_len + 1) * sizeof(WCHAR));
            n = name_len;
            goto done;
        }
        memcpy(buf, name, (name_len + 1) * sizeof(WCHAR));
        root = 4;
        break;
    case RtlPathTypeRootLocalDevice:
        wcscpy(buf, L"\\\\.\\");
        n = 4;
        goto done;
    case RtlPathTypeUncAbsolute: {
        memcpy(buf, name, (name_len + 1) * sizeof(WCHAR));
        /* root covers \\server\share\. */
        SIZE_T i = 2, seps = 0;
        while (buf[i] && seps < 2) {
            if (is_sep(buf[i]))
                seps++;
            i++;
        }
        root = i;
        break;
    }
    case RtlPathTypeDriveAbsolute:
        memcpy(buf, name, (name_len + 1) * sizeof(WCHAR));
        root = 3;
        break;
    case RtlPathTypeDriveRelative: {
        WCHAR drive = (WCHAR)towupper(name[0]);
        WCHAR variable[4] = {'=', drive, ':', 0};
        WCHAR kept[MAX_PATH];
        SIZE_T kept_chars = 0;
        if (cwd->Length >= 2 * sizeof(WCHAR) && towupper(cwd->Buffer[0]) == drive) {
            memcpy(buf, cwd->Buffer, cwd->Length);
            n = cwd->Length / sizeof(WCHAR);
        } else if (NT_SUCCESS(RtlQueryEnvironmentVariable(nullptr, variable, 3, kept, RTL_NUMBER_OF(kept) - 1, &kept_chars)) &&
                   kept_chars >= 2 && kept_chars < RTL_NUMBER_OF(kept) && towupper(kept[0]) == drive && kept[1] == ':') {
            /* the interpreter keeps the directory of each drive in a variable of that drive's name. */
            memcpy(buf, kept, kept_chars * sizeof(WCHAR));
            n = kept_chars;
        } else {
            buf[0] = drive;
            buf[1] = ':';
            n = 2;
        }
        if (!n || buf[n - 1] != '\\')
            buf[n++] = '\\';
        memcpy(buf + n, name + 2, (name_len - 1) * sizeof(WCHAR));
        root = 3;
        break;
    }
    case RtlPathTypeRooted:
        if (cwd->Length >= 2 * sizeof(WCHAR) && cwd->Buffer[1] == ':') {
            buf[0] = cwd->Buffer[0];
            buf[1] = ':';
            n = 2;
        }
        memcpy(buf + n, name, (name_len + 1) * sizeof(WCHAR));
        root = n + 1;
        break;
    default:
        memcpy(buf, cwd->Buffer, cwd->Length);
        n = cwd->Length / sizeof(WCHAR);
        if (!n || buf[n - 1] != '\\')
            buf[n++] = '\\';
        memcpy(buf + n, name, (name_len + 1) * sizeof(WCHAR));
        root = (n >= 3 && buf[1] == ':') ? 3 : 1;
        break;
    }
    for (PWSTR p = buf; *p; p++)
        if (*p == '/')
            *p = '\\';
    if (root >= 2 && buf[1] == ':')
        buf[0] = (WCHAR)towupper(buf[0]);
    n = collapse(buf, root);
    trim_tail(buf, root);
    n = wcslen(buf);
done:
    *length = n;
    if (n + 1 > out_chars)
        return STATUS_BUFFER_TOO_SMALL;
    memcpy(out, buf, (n + 1) * sizeof(WCHAR));
    return STATUS_SUCCESS;
}

/* returns the length in bytes, or the size needed with the terminator when the buffer is too small. */
NTDLL_EXPORT ULONG NTAPI RtlGetFullPathName_U(PCWSTR name, ULONG size, PWSTR buffer, PWSTR *file_part)
{
    if (!name || !*name)
        return 0;
    SIZE_T len;
    WCHAR tmp[MAX_PATH * 4];
    if (!NT_SUCCESS(full_path(name, tmp, RTL_NUMBER_OF(tmp), &len)))
        return 0;
    ULONG bytes = (ULONG)(len * sizeof(WCHAR));
    if (bytes + sizeof(WCHAR) > size)
        return bytes + sizeof(WCHAR);
    memcpy(buffer, tmp, bytes + sizeof(WCHAR));
    if (file_part) {
        PWSTR p = buffer + len;
        while (p > buffer && p[-1] != '\\')
            p--;
        *file_part = *p ? p : nullptr;
    }
    return bytes;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlDosPathNameToNtPathName_U(PCWSTR dos, PUNICODE_STRING nt, PCWSTR *file_part,
                                                         PVOID relative)
{
    WCHAR full[MAX_PATH * 4];
    SIZE_T len;
    if (!dos || !*dos || !NT_SUCCESS(full_path(dos, full, RTL_NUMBER_OF(full), &len)))
        return FALSE;
    PCWSTR prefix = L"\\??\\";
    PCWSTR rest = full;
    if (full[0] == '\\' && full[1] == '\\') {
        if (full[2] == '?' || full[2] == '.') {
            rest = full + 4;
        } else {
            prefix = L"\\??\\UNC\\";
            rest = full + 2;
        }
    }
    SIZE_T plen = wcslen(prefix), rlen = wcslen(rest);
    SIZE_T bytes = (plen + rlen + 1) * sizeof(WCHAR);
    if (bytes > 0xFFFE)
        return FALSE;
    PWSTR buf = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, bytes);
    if (!buf)
        return FALSE;
    memcpy(buf, prefix, plen * sizeof(WCHAR));
    memcpy(buf + plen, rest, (rlen + 1) * sizeof(WCHAR));
    nt->Buffer = buf;
    nt->Length = (USHORT)(bytes - sizeof(WCHAR));
    nt->MaximumLength = (USHORT)bytes;
    if (file_part) {
        PWSTR p = buf + plen + rlen;
        while (p > buf && p[-1] != '\\')
            p--;
        *file_part = *p ? p : nullptr;
    }
    return TRUE;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlDosPathNameToNtPathName_U_WithStatus(PCWSTR dos, PUNICODE_STRING nt, PCWSTR *file_part,
                                                                   PVOID relative)
{
    return RtlDosPathNameToNtPathName_U(dos, nt, file_part, relative) ? STATUS_SUCCESS : STATUS_OBJECT_NAME_INVALID;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlDosPathNameToRelativeNtPathName_U(PCWSTR dos, PUNICODE_STRING nt, PCWSTR *file_part,
                                                                PVOID relative)
{
    return RtlDosPathNameToNtPathName_U(dos, nt, file_part, nullptr);
}

NTDLL_EXPORT void NTAPI RtlReleaseRelativeName(PVOID relative)
{
}

NTDLL_EXPORT ULONG NTAPI RtlGetCurrentDirectory_U(ULONG size, PWSTR buffer)
{
    RtlAcquirePebLock();
    PCUNICODE_STRING cwd = &NtCurrentPeb()->ProcessParameters->CurrentDirectory.DosPath;
    ULONG len = cwd->Length;
    /* the stored path ends with a backslash, report it without one except for a drive root. */
    if (len > 3 * sizeof(WCHAR) && cwd->Buffer[len / sizeof(WCHAR) - 1] == '\\')
        len -= sizeof(WCHAR);
    ULONG result = len;
    if (len + sizeof(WCHAR) <= size) {
        memcpy(buffer, cwd->Buffer, len);
        buffer[len / sizeof(WCHAR)] = 0;
    } else {
        result = len + sizeof(WCHAR);
    }
    RtlReleasePebLock();
    return result;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlSetCurrentDirectory_U(PCUNICODE_STRING dir)
{
    WCHAR name[MAX_PATH * 2], full[MAX_PATH * 2];
    SIZE_T n = dir->Length / sizeof(WCHAR), len;
    if (n >= RTL_NUMBER_OF(name))
        return STATUS_NAME_TOO_LONG;
    memcpy(name, dir->Buffer, n * sizeof(WCHAR));
    name[n] = 0;
    NTSTATUS status = full_path(name, full, RTL_NUMBER_OF(full) - 1, &len);
    if (!NT_SUCCESS(status))
        return status;
    if (!len || full[len - 1] != '\\') {
        full[len++] = '\\';
        full[len] = 0;
    }
    /* the directory must exist. */
    UNICODE_STRING nt;
    if (!RtlDosPathNameToNtPathName_U(full, &nt, nullptr, nullptr))
        return STATUS_OBJECT_NAME_INVALID;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &nt, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    FILE_BASIC_INFORMATION info;
    status = NtQueryAttributesFile(&oa, &info);
    RtlFreeUnicodeString(&nt);
    if (!NT_SUCCESS(status))
        return status;
    if (!(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return STATUS_NOT_A_DIRECTORY;

    RtlAcquirePebLock();
    PUNICODE_STRING cwd = &NtCurrentPeb()->ProcessParameters->CurrentDirectory.DosPath;
    if ((len + 1) * sizeof(WCHAR) > cwd->MaximumLength) {
        RtlReleasePebLock();
        return STATUS_NAME_TOO_LONG;
    }
    memcpy(cwd->Buffer, full, (len + 1) * sizeof(WCHAR));
    cwd->Length = (USHORT)(len * sizeof(WCHAR));
    RtlReleasePebLock();
    return STATUS_SUCCESS;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlDoesFileExists_U(PCWSTR name)
{
    UNICODE_STRING nt;
    if (!RtlDosPathNameToNtPathName_U(name, &nt, nullptr, nullptr))
        return FALSE;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &nt, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    FILE_BASIC_INFORMATION info;
    NTSTATUS status = NtQueryAttributesFile(&oa, &info);
    RtlFreeUnicodeString(&nt);
    return NT_SUCCESS(status);
}

/* path is ';' separated, extension is an optional default, which a name with a dot anywhere does not get. */
NTDLL_EXPORT ULONG NTAPI RtlDosSearchPath_U(PCWSTR path, PCWSTR name, PCWSTR extension, ULONG size, PWSTR buffer,
                                            PWSTR *file_part)
{
    WCHAR candidate[MAX_PATH * 2];
    RTL_PATH_TYPE type = RtlDetermineDosPathNameType_U(name);
    BOOLEAN has_ext = wcschr(name, '.') != nullptr;
    if (type != RtlPathTypeRelative) {
        if (RtlDoesFileExists_U(name))
            return RtlGetFullPathName_U(name, size, buffer, file_part);
        return 0;
    }
    while (path && *path) {
        PCWSTR end = path;
        while (*end && *end != ';')
            end++;
        SIZE_T dlen = (SIZE_T)(end - path);
        SIZE_T nlen = wcslen(name);
        SIZE_T elen = (!has_ext && extension) ? wcslen(extension) : 0;
        if (dlen && dlen + nlen + elen + 2 < RTL_NUMBER_OF(candidate)) {
            memcpy(candidate, path, dlen * sizeof(WCHAR));
            SIZE_T n = dlen;
            if (candidate[n - 1] != '\\')
                candidate[n++] = '\\';
            memcpy(candidate + n, name, nlen * sizeof(WCHAR));
            n += nlen;
            if (elen)
                memcpy(candidate + n, extension, elen * sizeof(WCHAR));
            candidate[n + elen] = 0;
            if (RtlDoesFileExists_U(candidate))
                return RtlGetFullPathName_U(candidate, size, buffer, file_part);
        }
        path = *end ? end + 1 : end;
    }
    return 0;
}

/* the last component of a name has a dot. */
static BOOLEAN last_part_has_dot(PCUNICODE_STRING name)
{
    for (SIZE_T i = name->Length / sizeof(WCHAR); i > 0; i--) {
        WCHAR c = name->Buffer[i - 1];
        if (c == '\\' || c == '/')
            break;
        if (c == '.')
            return TRUE;
    }
    return FALSE;
}

static BOOLEAN exists_counted(PCWSTR buffer, SIZE_T chars)
{
    WCHAR name[MAX_PATH * 2];
    if (chars >= RTL_NUMBER_OF(name))
        return FALSE;
    memcpy(name, buffer, chars * sizeof(WCHAR));
    name[chars] = 0;
    return RtlDoesFileExists_U(name);
}

/* the full name of a found file into the caller's buffer, or the size it needs. */
static NTSTATUS search_result(PCWSTR found, PUNICODE_STRING static_string, PUNICODE_STRING dynamic_string,
                              PCUNICODE_STRING *full_name, SIZE_T *file_part, SIZE_T *needed)
{
    WCHAR full[MAX_PATH * 2];
    PWSTR part = nullptr;
    ULONG bytes = RtlGetFullPathName_U(found, sizeof(full), full, &part);
    if (!bytes || bytes >= sizeof(full))
        return STATUS_NAME_TOO_LONG;
    PUNICODE_STRING out = static_string;
    if (!out || bytes + sizeof(WCHAR) > out->MaximumLength) {
        if (!dynamic_string) {
            if (needed)
                *needed = bytes + sizeof(WCHAR);
            return STATUS_BUFFER_TOO_SMALL;
        }
        dynamic_string->Buffer = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, bytes + sizeof(WCHAR));
        if (!dynamic_string->Buffer)
            return STATUS_NO_MEMORY;
        dynamic_string->MaximumLength = (USHORT)(bytes + sizeof(WCHAR));
        out = dynamic_string;
    }
    memcpy(out->Buffer, full, bytes + sizeof(WCHAR));
    out->Length = (USHORT)bytes;
    if (full_name)
        *full_name = out;
    if (file_part)
        *file_part = part ? (SIZE_T)(part - full) : 0;
    return STATUS_SUCCESS;
}

/*
 * the counted search kernelbase's SearchPathW uses. flag 2 searches a name that is not relative, or starts with .\ or
 * ..\, as it is; flag 4 then also tries the default extension on such a name when it has a dot. a relative name is
 * looked for in each folder of the path, with the extension unless its last part has a dot.
 */
NTDLL_EXPORT NTSTATUS NTAPI RtlDosSearchPath_Ustr(ULONG flags, PCUNICODE_STRING path, PCUNICODE_STRING name,
                                                 PCUNICODE_STRING extension, PUNICODE_STRING static_string,
                                                 PUNICODE_STRING dynamic_string, PCUNICODE_STRING *full_name,
                                                 SIZE_T *file_part, SIZE_T *needed)
{
    if (full_name)
        *full_name = nullptr;
    if (file_part)
        *file_part = 0;
    if (needed)
        *needed = 0;
    if (dynamic_string) {
        dynamic_string->Length = dynamic_string->MaximumLength = 0;
        dynamic_string->Buffer = nullptr;
    }
    if ((flags & ~7u) || !path || !name || (static_string && dynamic_string && !full_name))
        return STATUS_INVALID_PARAMETER;
    WCHAR candidate[MAX_PATH * 2];
    SIZE_T name_chars = name->Length / sizeof(WCHAR);
    SIZE_T ext_chars = extension ? extension->Length / sizeof(WCHAR) : 0;
    if (name_chars + ext_chars + 1 >= RTL_NUMBER_OF(candidate))
        return STATUS_NAME_TOO_LONG;
    memcpy(candidate, name->Buffer, name_chars * sizeof(WCHAR));
    candidate[name_chars] = 0;
    RTL_PATH_TYPE type = RtlDetermineDosPathNameType_U(candidate);
    BOOLEAN dotted = (flags & 2) && name_chars >= 2 && name->Buffer[0] == '.' &&
                     (name->Buffer[1] == '\\' || name->Buffer[1] == '/' ||
                      (name_chars >= 3 && name->Buffer[1] == '.' && (name->Buffer[2] == '\\' || name->Buffer[2] == '/')));
    if (type != RtlPathTypeRelative || dotted) {
        if (exists_counted(name->Buffer, name_chars))
            return search_result(candidate, static_string, dynamic_string, full_name, file_part, needed);
        if (!ext_chars || (!(flags & 4) && last_part_has_dot(name)))
            return STATUS_NO_SUCH_FILE;
        memcpy(candidate + name_chars, extension->Buffer, ext_chars * sizeof(WCHAR));
        candidate[name_chars + ext_chars] = 0;
        if (RtlDoesFileExists_U(candidate))
            return search_result(candidate, static_string, dynamic_string, full_name, file_part, needed);
        return STATUS_NO_SUCH_FILE;
    }
    if (last_part_has_dot(name))
        ext_chars = 0;
    PCWSTR p = path->Buffer, end = path->Buffer + path->Length / sizeof(WCHAR);
    while (p < end) {
        PCWSTR stop = p;
        while (stop < end && *stop != ';')
            stop++;
        SIZE_T dir_chars = (SIZE_T)(stop - p), n = dir_chars;
        if (dir_chars + name_chars + ext_chars + 2 < RTL_NUMBER_OF(candidate)) {
            memcpy(candidate, p, dir_chars * sizeof(WCHAR));
            if (n && candidate[n - 1] != '\\' && candidate[n - 1] != '/')
                candidate[n++] = '\\';
            memcpy(candidate + n, name->Buffer, name_chars * sizeof(WCHAR));
            n += name_chars;
            if (ext_chars)
                memcpy(candidate + n, extension->Buffer, ext_chars * sizeof(WCHAR));
            candidate[n + ext_chars] = 0;
            if (RtlDoesFileExists_U(candidate))
                return search_result(candidate, static_string, dynamic_string, full_name, file_part, needed);
        }
        p = stop < end ? stop + 1 : stop;
    }
    return STATUS_NO_SUCH_FILE;
}

NTDLL_EXPORT ULONG NTAPI RtlIsDosDeviceName_U(PCWSTR name)
{
    static const PCWSTR devices[] = {L"CON", L"PRN", L"AUX", L"NUL", L"COM1", L"COM2", L"COM3", L"COM4",
                                     L"LPT1", L"LPT2", L"LPT3"};
    PCWSTR file = name + wcslen(name);
    while (file > name && !is_sep(file[-1]) && file[-1] != ':')
        file--;
    SIZE_T n = 0;
    while (file[n] && file[n] != '.' && file[n] != ':')
        n++;
    for (ULONG i = 0; i < RTL_NUMBER_OF(devices); i++) {
        if (wcslen(devices[i]) == n && !_wcsnicmp(file, devices[i], n))
            return (ULONG)(((file - name) * sizeof(WCHAR)) << 16 | (n * sizeof(WCHAR)));
    }
    return 0;
}
