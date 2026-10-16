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

/* removes "." and ".." components in place, never above the root of `start`. */
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
        /* root covers \\server\share\ */
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
        if (cwd->Length >= 2 * sizeof(WCHAR) && towupper(cwd->Buffer[0]) == drive) {
            memcpy(buf, cwd->Buffer, cwd->Length);
            n = cwd->Length / sizeof(WCHAR);
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

/* returns the length in bytes, or the needed size including the terminator if too small. */
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
    /* the stored path ends with a backslash; report it without, except for a drive root. */
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

/* searches `path` (';' separated) for `name` with an optional default extension. */
NTDLL_EXPORT ULONG NTAPI RtlDosSearchPath_U(PCWSTR path, PCWSTR name, PCWSTR extension, ULONG size, PWSTR buffer,
                                            PWSTR *file_part)
{
    WCHAR candidate[MAX_PATH * 2];
    RTL_PATH_TYPE type = RtlDetermineDosPathNameType_U(name);
    BOOLEAN has_ext = FALSE;
    for (PCWSTR p = name + wcslen(name); p > name && p[-1] != '\\' && p[-1] != '/'; p--)
        if (p[-1] == '.')
            has_ext = TRUE;
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
