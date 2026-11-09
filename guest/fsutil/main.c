/*
 * fsutil.exe: the file system utility. only the hardlink, reparse point and symbolic link evaluation commands are here.
 *
 *   fsutil hardlink create <new file> <existing file>
 *   fsutil hardlink list <file>
 *   fsutil reparsepoint query <file>
 *   fsutil reparsepoint delete <file>
 *   fsutil behavior query symlinkEvaluation
 *   fsutil behavior set symlinkEvaluation L2L:1 L2R:0 R2L:0 R2R:0
 *   fsutil volume diskFree [/csv] <volume pathname>
 *   fsutil volume flush <volume pathname>
 *   fsutil volume list
 *   fsutil volume queryLabel <volume pathname>
 *   fsutil volume setLabel <volume pathname> <label>
 *
 * a usage given for no arguments at all exits with 0, as fsutil does. a volume pathname is a drive, C:, or a folder on it, C:\Games, whose host file system diskFree tells. a list prints every name of the file as a path from the root of its volume, one to a line. the exit code is 0, or 1
 * when the command is wrong or the system refused it.
 */
#include "tool.h"

static const WCHAR kCommands[] =
    L"---- Commands Supported ----\n\n"
    L"behavior      Control file system behavior\n"
    L"hardlink      Hardlink management\n"
    L"reparsePoint  Reparse point management\n"
    L"volume        Volume management\n";

static const WCHAR kVolume[] =
    L"---- VOLUME Commands Supported ----\n\n"
    L"diskFree              Query the free space of a volume\n"
    L"flush                 Flush a volume\n"
    L"list                  List volumes\n"
    L"queryLabel            Query the label for a volume\n"
    L"setLabel              Set the label for a volume\n";

static const WCHAR kReparsePoint[] =
    L"---- REPARSEPOINT Commands Supported ----\n\n"
    L"delete          Delete a reparse point\n"
    L"query           Query a reparse point\n";

static const WCHAR kHardlink[] =
    L"---- HARDLINK Commands Supported ----\n\n"
    L"create        Create a hardlink\n"
    L"list          Enumerate hardlinks on a file\n";

static int error_exit(DWORD error)
{
    TU_STR text = {};
    tu_error_text(error, &text);
    tu_eprint(L"Error:  %s\n", tu_str_text(&text));
    tu_str_free(&text);
    return 1;
}

static int create(void)
{
    if (tu_argc != 5) {
        tu_print(L"Usage : fsutil hardlink create <new filename> <existing filename>\n"
                 L"        Eg : fsutil hardlink create c:\\foo.txt c:\\bar.txt\n");
        return 1;
    }
    if (!CreateHardLinkW(tu_argv[3], tu_argv[4], nullptr))
        return error_exit(GetLastError());
    tu_print(L"Hardlink created for %s <<===>> %s\n", tu_argv[3], tu_argv[4]);
    return 0;
}

static int list(void)
{
    if (tu_argc != 4) {
        tu_print(L"Usage : fsutil hardlink list <filename>\n"
                 L"        Eg : fsutil hardlink list c:\\foo.txt\n");
        return 1;
    }
    DWORD length = MAX_PATH;
    WCHAR *name = tu_alloc(length * sizeof(WCHAR));
    HANDLE find = FindFirstFileNameW(tu_argv[3], 0, &length, name);
    if (find == INVALID_HANDLE_VALUE && GetLastError() == ERROR_MORE_DATA) {
        tu_free(name);
        name = tu_alloc(length * sizeof(WCHAR));
        find = FindFirstFileNameW(tu_argv[3], 0, &length, name);
    }
    if (find == INVALID_HANDLE_VALUE) {
        tu_free(name);
        return error_exit(GetLastError());
    }
    for (;;) {
        tu_print(L"%s\n", name);
        DWORD size = length;
        if (!FindNextFileNameW(find, &size, name)) {
            if (GetLastError() != ERROR_MORE_DATA)
                break;
            tu_free(name);
            length = size;
            name = tu_alloc(length * sizeof(WCHAR));
            if (!FindNextFileNameW(find, &size, name))
                break;
        }
    }
    DWORD error = GetLastError();
    FindClose(find);
    tu_free(name);
    return error == ERROR_HANDLE_EOF ? 0 : error_exit(error);
}

/* the tags that have a name, and the line that tells each. */
static void tag_lines(DWORD tag)
{
    if (tag & 0x80000000u)
        tu_print(L"Tag value: Microsoft\n");
    if (tag & 0x20000000u)
        tu_print(L"Tag value: Name Surrogate\n");
    switch (tag) {
    case IO_REPARSE_TAG_SYMLINK: tu_print(L"Tag value: Symbolic Link\n"); break;
    case IO_REPARSE_TAG_MOUNT_POINT: tu_print(L"Tag value: Mount Point\n"); break;
    case IO_REPARSE_TAG_HSM: tu_print(L"Tag value: HSM\n"); break;
    case IO_REPARSE_TAG_SIS: tu_print(L"Tag value: SIS\n"); break;
    case IO_REPARSE_TAG_FILTER_MANAGER: tu_print(L"Tag value: Filter Manager\n"); break;
    case IO_REPARSE_TAG_DFS: tu_print(L"Tag value: DFS\n"); break;
    case IO_REPARSE_TAG_IIS_CACHE: tu_print(L"Tag value: IIS Caching Reparse Point\n"); break;
    default: break;
    }
}

static void hex_dump(const BYTE *data, DWORD length)
{
    for (DWORD row = 0; row < length; row += 16) {
        TU_STR line = {};
        tu_str_format(&line, L"%04x:  ", row);
        for (DWORD i = 0; i < 16; i++) {
            if (row + i < length)
                tu_str_format(&line, L"%02x ", data[row + i]);
            else
                tu_str_puts(&line, L"   ");
            if (i == 7 || i == 15)
                tu_str_putc(&line, L' ');
        }
        for (DWORD i = 0; i < 16 && row + i < length; i++)
            tu_str_putc(&line, data[row + i] >= 0x20 && data[row + i] < 0x7F ? (WCHAR)data[row + i] : L'.');
        tu_print(L"%s\n", tu_str_text(&line));
        tu_str_free(&line);
    }
}

static HANDLE open_reparse(const WCHAR *name, DWORD access)
{
    return CreateFileW(name, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
}

static int reparse_query(void)
{
    if (tu_argc != 4) {
        tu_print(L"Usage: fsutil reparsePoint query <filename>\n   Eg: fsutil reparsePoint query C:\\Server\n");
        return 1;
    }
    HANDLE h = open_reparse(tu_argv[3], FILE_READ_ATTRIBUTES);
    if (h == INVALID_HANDLE_VALUE)
        return error_exit(GetLastError());
    BYTE *buffer = tu_alloc(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    DWORD got = 0;
    BOOL ok = DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, nullptr, 0, buffer, MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &got, nullptr);
    DWORD error = GetLastError();
    CloseHandle(h);
    if (!ok) {
        tu_free(buffer);
        return error_exit(error);
    }
    REPARSE_DATA_BUFFER *b = (REPARSE_DATA_BUFFER *)buffer;
    tu_print(L"Reparse Tag Value : 0x%08x\n", b->ReparseTag);
    tag_lines(b->ReparseTag);
    tu_print(L"\n");
    if (got >= 8 && (b->ReparseTag == IO_REPARSE_TAG_SYMLINK || b->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT)) {
        BOOL mount = b->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT;
        const BYTE *names = mount ? (const BYTE *)b->MountPointReparseBuffer.PathBuffer
                                  : (const BYTE *)b->SymbolicLinkReparseBuffer.PathBuffer;
        USHORT so = b->SymbolicLinkReparseBuffer.SubstituteNameOffset, sl = b->SymbolicLinkReparseBuffer.SubstituteNameLength;
        USHORT po = b->SymbolicLinkReparseBuffer.PrintNameOffset, pl = b->SymbolicLinkReparseBuffer.PrintNameLength;
        if (names + so + sl > buffer + got || names + po + pl > buffer + got) {
            tu_print(L"The data in the reparse buffer is invalid!\n");
        } else {
            tu_print(L"Substitue Name offset: %d\nSubstitue Name length: %d\nPrint Name offset:     %d\nPrint Name Length:     %d\n",
                     so, sl, po, pl);
            tu_print(L"Substitute Name:       %.*s\n", sl / 2, (const WCHAR *)(names + so));
            tu_print(L"Print Name:            %.*s\n", pl / 2, (const WCHAR *)(names + po));
        }
        tu_print(L"\n");
    }
    tu_print(L"Reparse Data Length: 0x%-8x\n", b->ReparseDataLength);
    tu_print(L"Reparse Data:\n");
    if (got >= REPARSE_DATA_BUFFER_HEADER_SIZE)
        hex_dump(buffer + REPARSE_DATA_BUFFER_HEADER_SIZE, got - REPARSE_DATA_BUFFER_HEADER_SIZE);
    tu_print(L"\n");
    tu_free(buffer);
    return 0;
}

static int reparse_delete(void)
{
    if (tu_argc != 4) {
        tu_print(L"Usage: fsutil reparsePoint delete <filename>\n   Eg: fsutil reparsePoint delete C:\\Server\n");
        return 1;
    }
    HANDLE h = open_reparse(tu_argv[3], GENERIC_WRITE | FILE_READ_ATTRIBUTES);
    if (h == INVALID_HANDLE_VALUE)
        return error_exit(GetLastError());
    BYTE buffer[64] = {};
    DWORD got = 0;
    if (!DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, nullptr, 0, buffer, sizeof(buffer), &got, nullptr) &&
        GetLastError() != ERROR_MORE_DATA) {
        DWORD error = GetLastError();
        CloseHandle(h);
        return error_exit(error);
    }
    /* the header with the tag alone, and the guid of the tag when it is not Microsoft's. */
    BYTE request[REPARSE_GUID_DATA_BUFFER_HEADER_SIZE] = {};
    DWORD tag = ((REPARSE_DATA_BUFFER *)buffer)->ReparseTag, size = 8;
    memcpy(request, &tag, 4);
    if (!(tag & 0x80000000u)) {
        memcpy(request + 8, buffer + 8, 16);
        size = REPARSE_GUID_DATA_BUFFER_HEADER_SIZE;
    }
    DWORD returned;
    BOOL ok = DeviceIoControl(h, FSCTL_DELETE_REPARSE_POINT, request, size, nullptr, 0, &returned, nullptr);
    DWORD error = GetLastError();
    CloseHandle(h);
    return ok ? 0 : error_exit(error);
}

#define FILE_SYSTEM_KEY L"SYSTEM\\CurrentControlSet\\Control\\FileSystem"

static const struct {
    const WCHAR *Option, *Value, *Label;
    BOOL Default;
} kClasses[] = {
    {L"L2L", L"SymlinkLocalToLocalEvaluation", L"Local-to-local", TRUE},
    {L"L2R", L"SymlinkLocalToRemoteEvaluation", L"Local-to-remote", TRUE},
    {L"R2L", L"SymlinkRemoteToLocalEvaluation", L"Remote-to-local", FALSE},
    {L"R2R", L"SymlinkRemoteToRemoteEvaluation", L"Remote-to-remote", FALSE},
};

static BOOL class_enabled(HKEY key, int i)
{
    DWORD value = 0, size = sizeof(value), type;
    if (key && RegQueryValueExW(key, kClasses[i].Value, nullptr, &type, (BYTE *)&value, &size) == ERROR_SUCCESS && type == REG_DWORD)
        return value != 0;
    return kClasses[i].Default;
}

static int behavior(void)
{
    if (tu_argc < 4 || !tu_eq(tu_argv[3], L"symlinkEvaluation")) {
        tu_print(L"Usage: fsutil behavior query <option>\n   option: symlinkEvaluation\n");
        return 1;
    }
    HKEY key = nullptr;
    if (tu_eq(tu_argv[2], L"query")) {
        RegOpenKeyExW(HKEY_LOCAL_MACHINE, FILE_SYSTEM_KEY, 0, KEY_READ, &key);
        for (int i = 0; i < 4; i++)
            tu_print(L"%s symbolic link evaluation is: %s\n", kClasses[i].Label, class_enabled(key, i) ? L"ENABLED" : L"DISABLED");
        if (key)
            RegCloseKey(key);
        return 0;
    }
    if (!tu_eq(tu_argv[2], L"set")) {
        tu_print(L"Usage: fsutil behavior set symlinkEvaluation {L2L|L2R|R2L|R2R}:{0|1} [...]\n");
        return 1;
    }
    /* every argument is a class and 0 or 1, and nothing is written unless all of them are. */
    DWORD values[4] = {2, 2, 2, 2};
    for (int a = 4; a < tu_argc; a++) {
        BOOL known = FALSE;
        for (int i = 0; i < 4; i++) {
            SIZE_T n = wcslen(kClasses[i].Option);
            if (tu_eq_n(tu_argv[a], kClasses[i].Option, n) && tu_argv[a][n] == L':' &&
                (tu_argv[a][n + 1] == L'0' || tu_argv[a][n + 1] == L'1') && !tu_argv[a][n + 2]) {
                values[i] = (DWORD)(tu_argv[a][n + 1] - L'0');
                known = TRUE;
            }
        }
        if (!known) {
            tu_print(L"Usage: fsutil behavior set symlinkEvaluation {L2L|L2R|R2L|R2R}:{0|1} [...]\n");
            return 1;
        }
    }
    if (tu_argc < 5) {
        tu_print(L"Usage: fsutil behavior set symlinkEvaluation {L2L|L2R|R2L|R2R}:{0|1} [...]\n");
        return 1;
    }
    LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, FILE_SYSTEM_KEY, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS)
        return error_exit((DWORD)status);
    for (int i = 0; i < 4 && status == ERROR_SUCCESS; i++) {
        if (values[i] < 2)
            status = RegSetValueExW(key, kClasses[i].Value, 0, REG_DWORD, (const BYTE *)&values[i], sizeof(DWORD));
    }
    RegCloseKey(key);
    return status == ERROR_SUCCESS ? 0 : error_exit((DWORD)status);
}

/* \\.\ before a drive or folder, as fsutil opens it; a volume guid path stays, without its last backslash. */
static HANDLE open_volume(const WCHAR *name, DWORD access)
{
    SIZE_T n = wcslen(name);
    if (n + 1 > MAX_PATH) {
        tu_print(L"The specified drive is too long.\n");
        SetLastError(ERROR_SUCCESS);
        return INVALID_HANDLE_VALUE;
    }
    WCHAR path[MAX_PATH + 8];
    if (n > 4 && name[0] == L'\\' && (name[1] == L'\\' || name[1] == L'?') && name[2] == L'?' && name[3] == L'\\') {
        wcscpy(path, name);
        if (path[n - 1] == L'\\')
            path[n - 1] = 0;
    } else if (n > 4 && name[0] == L'\\' && name[1] == L'\\' && name[2] == L'.' && name[3] == L'\\') {
        wcscpy(path, name);
    } else {
        wcscpy(path, L"\\\\.\\");
        wcscat(path, name);
    }
    return CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                       nullptr);
}

/* nothing printed means open_volume told the error itself. */
static int volume_error(void)
{
    DWORD error = GetLastError();
    return error ? error_exit(error) : 1;
}

static BOOL local_ntfs(const WCHAR *name)
{
    WCHAR root[MAX_PATH + 2], fs[16];
    wcscpy(root, name);
    if (root[0] && root[wcslen(root) - 1] != L'\\')
        wcscat(root, L"\\");
    if (!GetVolumePathNameW(root, root, MAX_PATH) || GetDriveTypeW(root) != DRIVE_FIXED)
        return FALSE;
    return GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, nullptr, fs, RTL_NUMBER_OF(fs)) && tu_eq(fs, L"NTFS");
}

/* the number with the user's thousands separator, grouped as fsutil groups it. */
static void group(UINT64 value, WCHAR *out, int size)
{
    static WCHAR decimal[8], thousand[8], grouping[16];
    static NUMBERFMTW format;
    static BOOL ready;
    if (!ready) {
        DWORD lead = 1, order = 1;
        BOOL failed = !GetLocaleInfoEx(nullptr, LOCALE_RETURN_NUMBER | LOCALE_ILZERO, (WCHAR *)&lead, 2);
        GetLocaleInfoEx(nullptr, LOCALE_RETURN_NUMBER | LOCALE_INEGNUMBER, (WCHAR *)&order, 2);
        failed |= !GetLocaleInfoEx(nullptr, LOCALE_STHOUSAND, thousand, RTL_NUMBER_OF(thousand));
        failed |= !GetLocaleInfoEx(nullptr, LOCALE_SDECIMAL, decimal, RTL_NUMBER_OF(decimal));
        failed |= !GetLocaleInfoEx(nullptr, LOCALE_SGROUPING, grouping, RTL_NUMBER_OF(grouping));
        /* "3;2;0" is 32: the digits of each group up to the 0 that repeats the last one. */
        UINT groups = 0;
        for (WCHAR *p = grouping; *p && *p != L'0'; p++) {
            if (tu_is_digit(*p))
                groups = groups * 10 + (UINT)(*p - L'0');
        }
        format = failed ? (NUMBERFMTW){0, 1, 30, L".", L",", 1}
                        : (NUMBERFMTW){0, lead, groups, decimal, thousand, order};
        ready = TRUE;
    }
    WCHAR digits[24];
    TU_STR text = {};
    tu_str_format(&text, L"%llu", value);
    wcscpy(digits, tu_str_text(&text));
    tu_str_free(&text);
    if (!GetNumberFormatEx(nullptr, 0, digits, &format, out, size))
        wcscpy(out, digits);
}

typedef struct SIZE_TEXT {
    WCHAR Bytes[40];
    WCHAR Amount[24];
    const WCHAR *Unit;
} SIZE_TEXT;

/* the bytes, and the amount in the largest unit below 1024 of it, rounded to a tenth as %.1f does. */
static void size_text(UINT64 bytes, SIZE_TEXT *t, int *bytes_width, int *amount_width)
{
    static const WCHAR *const kUnits[] = {L" KB", L" MB", L" GB", L" TB", L" PB"};
    group(bytes, t->Bytes, RTL_NUMBER_OF(t->Bytes));
    int unit = 0;
    for (UINT64 v = bytes >> 10; v >= 1024 && unit < 4; v >>= 10)
        unit++;
    UINT64 divisor = 1ull << (10 * (unit + 1)), whole = bytes / divisor;
    UINT64 tenths = ((bytes % divisor) * 20 + divisor) / (2 * divisor);
    if (tenths == 10) {
        whole++;
        tenths = 0;
    }
    TU_STR text = {};
    tu_str_format(&text, L"%llu.%llu", whole, tenths);
    wcscpy(t->Amount, tu_str_text(&text));
    tu_str_free(&text);
    t->Unit = kUnits[unit];
    if ((int)wcslen(t->Bytes) > *bytes_width)
        *bytes_width = (int)wcslen(t->Bytes);
    if ((int)wcslen(t->Amount) > *amount_width)
        *amount_width = (int)wcslen(t->Amount);
}

static int disk_free(void)
{
    static const WCHAR *const kLabels[] = {
        L"Total free bytes", L"Total bytes", L"Total quota free bytes", L"Unavailable pool bytes",
        L"Quota unavailable pool bytes", L"Used bytes", L"Total Reserved bytes", L"Volume storage reserved bytes",
        L"Available committed bytes", L"Pool available bytes",
    };
    int first = 3;
    BOOL csv = FALSE;
    while (first < tu_argc && tu_eq(tu_argv[first], L"/csv")) {
        csv = TRUE;
        first++;
    }
    if (tu_argc - first != 1) {
        tu_print(L"Usage: fsutil volume diskFree <volume pathname>\n   Eg: fsutil volume diskFree C:\n");
        return tu_argc != first;
    }
    const WCHAR *name = tu_argv[first];
    if (wcslen(name) + 1 > MAX_PATH) {
        tu_print(L"The specified drive is too long.\n");
        return 1;
    }
    UINT64 values[10] = {};
    int count = 3;
    if (local_ntfs(name)) {
        HANDLE h = open_volume(name, GENERIC_READ);
        if (h == INVALID_HANDLE_VALUE)
            return volume_error();
        FILE_FS_FULL_SIZE_INFORMATION_EX info = {};
        IO_STATUS_BLOCK iosb;
        NTSTATUS status = NtQueryVolumeInformationFile(h, &iosb, &info, sizeof(info), FileFsFullSizeInformationEx);
        CloseHandle(h);
        if (status < 0)
            return error_exit(RtlNtStatusToDosError(status));
        UINT64 unit = (UINT64)info.SectorsPerAllocationUnit * info.BytesPerSector;
        const ULONGLONG units[10] = {
            info.ActualAvailableAllocationUnits, info.CallerTotalAllocationUnits, info.CallerAvailableAllocationUnits,
            info.ActualPoolUnavailableAllocationUnits, info.CallerPoolUnavailableAllocationUnits, info.UsedAllocationUnits,
            info.TotalReservedAllocationUnits, info.VolumeStorageReserveAllocationUnits,
            info.AvailableCommittedAllocationUnits, info.PoolAvailableAllocationUnits,
        };
        for (int i = 0; i < 10; i++)
            values[i] = units[i] * unit;
        count = 10;
    } else {
        WCHAR root[MAX_PATH + 2];
        wcscpy(root, name);
        wcscat(root, L"\\");
        ULARGE_INTEGER available, total, free;
        if (!GetDiskFreeSpaceExW(root, &available, &total, &free))
            return error_exit(GetLastError());
        values[0] = free.QuadPart;
        values[1] = total.QuadPart;
        values[2] = available.QuadPart;
    }
    if (csv) {
        tu_print(L"%llu,%llu,%llu\n", values[2], values[1], values[0]);
        return 0;
    }
    SIZE_TEXT texts[10];
    int bytes_width = 0, amount_width = 0;
    for (int i = 0; i < count; i++)
        size_text(values[i], &texts[i], &bytes_width, &amount_width);
    /* the labels line up on the widest of the report: the three line one of other volumes is narrower. */
    int label_width = count == 10 ? 32 : 24;
    for (int i = 0; i < count; i++)
        tu_print(L"%-*s: %*s (%*s%s)\n", label_width, kLabels[i], bytes_width, texts[i].Bytes, amount_width,
                 texts[i].Amount, texts[i].Unit);
    return 0;
}

static int volume_list(void)
{
    if (tu_argc != 3) {
        tu_print(L"Usage: fsutil volume list\n");
        return 1;
    }
    WCHAR volume[MAX_PATH + 8];
    HANDLE find = FindFirstVolumeW(volume, RTL_NUMBER_OF(volume));
    if (find == INVALID_HANDLE_VALUE)
        return error_exit(GetLastError());
    tu_print(L"Possible volumes and current mount points are:\n\n");
    do {
        DWORD length = 0;
        GetVolumePathNamesForVolumeNameW(volume, nullptr, 0, &length);
        WCHAR *names = tu_alloc((length + 2) * sizeof(WCHAR));
        if (GetVolumePathNamesForVolumeNameW(volume, names, length + 1, nullptr)) {
            tu_print(L"%s\n", volume);
            for (WCHAR *p = names; *p; p += wcslen(p) + 1)
                tu_print(L"%s\n", p);
        }
        tu_free(names);
        tu_print(L"\n");
    } while (FindNextVolumeW(find, volume, RTL_NUMBER_OF(volume)));
    DWORD error = GetLastError();
    FindVolumeClose(find);
    return error == ERROR_NO_MORE_FILES ? 0 : error_exit(error);
}

static int volume_flush(void)
{
    if (tu_argc != 4) {
        tu_print(L"Usage: fsutil volume flush <volume pathname>\n   Eg: fsutil volume flush C:\n");
        return tu_argc != 3;
    }
    HANDLE h = open_volume(tu_argv[3], FILE_WRITE_DATA);
    if (h == INVALID_HANDLE_VALUE)
        return volume_error();
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtFlushBuffersFile(h, &iosb);
    CloseHandle(h);
    return status < 0 ? error_exit(RtlNtStatusToDosError(status)) : 0;
}

/* FILE_FS_VOLUME_INFORMATION: creation time, serial, label length, supports objects, then the label at 18. */
#define VOLUME_INFO_SIZE 0x5A

static int volume_label(BOOL set)
{
    if (tu_argc != (set ? 5 : 4)) {
        if (set)
            tu_print(L"Usage: fsutil volume setLabel <volume pathname> <label>\n   Eg: fsutil volume flush C: \"my volume\"\n");
        else
            tu_print(L"Usage: fsutil volume queryLabel <volume pathname>\n   Eg: fsutil volume queryLabel C:\n");
        return tu_argc != 3;
    }
    HANDLE h = open_volume(tu_argv[3], FILE_WRITE_DATA);
    if (h == INVALID_HANDLE_VALUE)
        return volume_error();
    BYTE info[VOLUME_INFO_SIZE] = {};
    IO_STATUS_BLOCK iosb;
    NTSTATUS status;
    if (set) {
        /* longer than 33 characters is cut to 32, as fsutil does. */
        ULONG bytes = (ULONG)wcslen(tu_argv[4]) * sizeof(WCHAR);
        if (bytes > 0x42)
            bytes = 0x40;
        BYTE label[sizeof(ULONG) + 0x42] = {};
        memcpy(label, &bytes, sizeof(ULONG));
        memcpy(label + sizeof(ULONG), tu_argv[4], bytes);
        status = NtSetVolumeInformationFile(h, &iosb, label, sizeof(ULONG) + bytes, FileFsLabelInformation);
        if (status >= 0) {
            if (NtQueryVolumeInformationFile(h, &iosb, info, sizeof(info), FileFsVolumeInformation) < 0) {
                memcpy(info + 12, &bytes, sizeof(ULONG));
                memcpy(info + 18, tu_argv[4], bytes);
            }
        }
    } else {
        status = NtQueryVolumeInformationFile(h, &iosb, info, sizeof(info), FileFsVolumeInformation);
    }
    CloseHandle(h);
    if (status < 0)
        return error_exit(RtlNtStatusToDosError(status));
    ULONG length;
    memcpy(&length, info + 12, sizeof(ULONG));
    if (length > sizeof(info) - 18)
        length = sizeof(info) - 18;
    if (set)
        tu_print(L"Successfully set \"%.*s\" as volume label for \"%s\"\n", (int)(length / 2), (const WCHAR *)(info + 18),
                 tu_argv[3]);
    else
        tu_print(L"Volume Label for \"%s\" is \"%.*s\"\n", tu_argv[3], (int)(length / 2), (const WCHAR *)(info + 18));
    return 0;
}

static int volume(void)
{
    if (tu_argc == 2) {
        tu_puts(kVolume);
        return 0;
    }
    if (tu_eq(tu_argv[2], L"diskfree"))
        return disk_free();
    if (tu_eq(tu_argv[2], L"flush"))
        return volume_flush();
    if (tu_eq(tu_argv[2], L"list"))
        return volume_list();
    if (tu_eq(tu_argv[2], L"querylabel"))
        return volume_label(FALSE);
    if (tu_eq(tu_argv[2], L"setlabel"))
        return volume_label(TRUE);
    tu_puts(kVolume);
    return 1;
}

int tool_main(void)
{
    if (tu_argc == 1) {
        tu_puts(kCommands);
        return 0;
    }
    if (tu_eq(tu_argv[1], L"reparsepoint")) {
        if (tu_argc == 2) {
            tu_puts(kReparsePoint);
            return 0;
        }
        if (tu_eq(tu_argv[2], L"query"))
            return reparse_query();
        if (tu_eq(tu_argv[2], L"delete"))
            return reparse_delete();
        tu_puts(kReparsePoint);
        return 1;
    }
    if (tu_eq(tu_argv[1], L"volume"))
        return volume();
    if (tu_eq(tu_argv[1], L"behavior") && tu_argc >= 3)
        return behavior();
    if (!tu_eq(tu_argv[1], L"hardlink")) {
        tu_puts(kCommands);
        return 1;
    }
    if (tu_argc == 2) {
        tu_puts(kHardlink);
        return 0;
    }
    if (tu_eq(tu_argv[2], L"create"))
        return create();
    if (tu_eq(tu_argv[2], L"list"))
        return list();
    tu_puts(kHardlink);
    return 1;
}
