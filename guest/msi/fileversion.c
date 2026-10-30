/*
 * the rules that decide whether a file of the package replaces the file on the machine. they are the file versioning
 * rules of the installer: the mode of REINSTALLMODE says which differences count, and what is compared is the version,
 * the languages, the hash of the package and the checksum of the file on the machine.
 */
#include "msi_private.h"
#include <mime/win32/winver.h>

/* what the file on the machine is, compared with the file of the package. */
enum { FILE_ABSENT = 0, FILE_OLDER = 1, FILE_EQUAL = 2, FILE_NEWER = 3, FILE_COMPANION = 4, FILE_LOCKED = 5, FILE_DENIED = 6 };

/* the same, as the installer logs it, odd numbers for a file that is there. */
enum { STATE_ABSENT = 0, STATE_OLDER = 1, STATE_EQUAL = 3, STATE_NEWER = 5, STATE_BAD_CHECKSUM = 7, STATE_FORCED = 9,
       STATE_COMPANION_ABSENT = 10, STATE_COMPANION_PRESENT = 11, STATE_LOCKED = 13, STATE_DENIED = 15 };

/* how the languages of the package and of the file compare. */
enum { LANGUAGE_NONE = 1, LANGUAGE_SUBSET = 2, LANGUAGE_SAME = 3, LANGUAGE_MIXED = 4, LANGUAGE_SUPERSET = 5,
       LANGUAGE_NO_MATCH = 6, LANGUAGE_INVALID = 11 };

enum { MAXIMUM_LANGUAGES = 100 };

/* four numbers of at most 16 bits, a missing number being zero. text that is not a version is a companion file. */
BOOL MimeMsiParseVersion(PCWSTR text, DWORD *high, DWORD *low)
{
    WORD parts[4] = {};
    *high = *low = 0;
    if (!text || !*text)
        return TRUE;
    for (ULONG i = 0; i < 4; i++) {
        if (*text == L'.')
            return FALSE;
        ULONG number = 0;
        while (*text >= L'0' && *text <= L'9') {
            number = number * 10 + (ULONG)(*text++ - L'0');
            if (number > 0xFFFF)
                return FALSE;
        }
        parts[i] = (WORD)number;
        if (*text == L'.')
            text++;
        else if (*text)
            return FALSE;
    }
    *high = ((DWORD)parts[0] << 16) | parts[1];
    *low = ((DWORD)parts[2] << 16) | parts[3];
    return TRUE;
}

typedef struct FILE_INFO {
    DWORD High, Low;
    WORD Languages[MAXIMUM_LANGUAGES];
    int LanguageCount;
} FILE_INFO;

/* what is wrong with the file when it cannot be read, otherwise the version and languages when it has them. */
static int read_file_info(PCWSTR path, FILE_INFO *info)
{
    info->High = info->Low = 0;
    info->LanguageCount = -1;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        return FILE_ABSENT;
    DWORD handle;
    DWORD size = GetFileVersionInfoSizeW(path, &handle);
    if (!size) {
        DWORD error = GetLastError();
        return error == ERROR_SHARING_VIOLATION ? FILE_LOCKED : error == ERROR_ACCESS_DENIED ? FILE_DENIED : -1;
    }
    void *data = MimeMsiAllocate(size);
    if (!data)
        return -1;
    int result = -1;
    if (GetFileVersionInfoW(path, 0, size, data)) {
        VS_FIXEDFILEINFO *fixed;
        UINT length;
        if (VerQueryValueW(data, L"\\", (void **)&fixed, &length) && length) {
            info->High = fixed->dwFileVersionMS;
            info->Low = fixed->dwFileVersionLS;
        }
        WORD *translation;
        if (VerQueryValueW(data, L"\\VarFileInfo\\Translation", (void **)&translation, &length)) {
            info->LanguageCount = 0;
            for (UINT offset = 0; offset + 4 <= length && info->LanguageCount < MAXIMUM_LANGUAGES; offset += 4) {
                WORD language = translation[offset / 2];
                BOOL listed = FALSE;
                for (int i = 0; i < info->LanguageCount; i++)
                    listed |= info->Languages[i] == language;
                if (!listed)
                    info->Languages[info->LanguageCount++] = language;
            }
        }
    } else {
        DWORD error = GetLastError();
        result = error == ERROR_SHARING_VIOLATION ? FILE_LOCKED : error == ERROR_ACCESS_DENIED ? FILE_DENIED : -1;
    }
    MimeMsiFree(data);
    return result;
}

/* the numbers of a language list such as 1033,1031. false when one is not a language. */
static BOOL parse_languages(PCWSTR text, WORD *languages, int *count)
{
    *count = 0;
    while (*text && *count < MAXIMUM_LANGUAGES) {
        ULONG number = 0;
        PCWSTR start = text;
        while (*text >= L'0' && *text <= L'9' && number <= 0xFFFF)
            number = number * 10 + (ULONG)(*text++ - L'0');
        if (text == start || number > 0xFFFF || (*text && *text != L','))
            return FALSE;
        languages[(*count)++] = (WORD)number;
        if (*text == L',')
            text++;
    }
    return TRUE;
}

static int compare_languages(PCWSTR language, const FILE_INFO *info)
{
    if (info->LanguageCount < 0)
        return LANGUAGE_NONE;
    WORD wanted[MAXIMUM_LANGUAGES];
    int wanted_count;
    if (!parse_languages(language ? language : L"", wanted, &wanted_count))
        return LANGUAGE_INVALID;
    int matches = 0;
    for (int i = 0; i < info->LanguageCount; i++)
        for (int j = 0; j < wanted_count; j++)
            matches += wanted[j] == info->Languages[i];
    if (!matches)
        return LANGUAGE_NO_MATCH;
    if (info->LanguageCount == wanted_count)
        return matches == wanted_count ? LANGUAGE_SAME : LANGUAGE_MIXED;
    if (info->LanguageCount < wanted_count)
        return matches == info->LanguageCount ? LANGUAGE_SUBSET : LANGUAGE_MIXED;
    return matches == wanted_count ? LANGUAGE_SUPERSET : LANGUAGE_MIXED;
}

static BOOL hash_matches(PCWSTR path, const MIME_MSI_FILE_HASH *hash)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data) || data.nFileSizeHigh || data.nFileSizeLow != hash->Size)
        return FALSE;
    MSIFILEHASHINFO found = {.dwFileHashInfoSize = sizeof(found)};
    return !MsiGetFileHashW(path, 0, &found) && !memcmp(found.dwData, hash->Words, sizeof(hash->Words));
}

/* a file counts as edited when it was written more than two seconds after it was created. */
static BOOL file_modified(PCWSTR path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data))
        return TRUE;
    ULONGLONG created = ((ULONGLONG)data.ftCreationTime.dwHighDateTime << 32) | data.ftCreationTime.dwLowDateTime;
    ULONGLONG written = ((ULONGLONG)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
    return written > created && written - created > 20000000;
}

static int compare_versions(DWORD old_high, DWORD old_low, DWORD new_high, DWORD new_low)
{
    if (old_high != new_high)
        return old_high < new_high ? FILE_OLDER : FILE_NEWER;
    if (old_low != new_low)
        return old_low < new_low ? FILE_OLDER : FILE_NEWER;
    return FILE_EQUAL;
}

static UINT check_file_version(PCWSTR path, PCWSTR version, PCWSTR language, const MIME_MSI_FILE_HASH *hash, int *result)
{
    DWORD high, low;
    if (!MimeMsiParseVersion(version, &high, &low)) {
        *result = FILE_COMPANION;
        return ERROR_SUCCESS;
    }
    BOOL versioned = high || low;
    FILE_INFO info;
    int status = read_file_info(path, &info);
    if (status >= 0) {
        *result = status == FILE_LOCKED ? (versioned ? FILE_LOCKED : FILE_NEWER) : status;
        return ERROR_SUCCESS;
    }
    if (info.High || info.Low || versioned) {
        *result = compare_versions(info.High, info.Low, high, low);
        if (*result != FILE_EQUAL)
            return ERROR_SUCCESS;
        int languages = compare_languages(language, &info);
        if (languages == LANGUAGE_INVALID)
            return ERROR_INSTALL_FAILURE;
        if (languages == LANGUAGE_SAME || (languages == LANGUAGE_NONE && !(language && *language)))
            *result = FILE_EQUAL;
        else
            *result = languages == LANGUAGE_SUPERSET ? FILE_NEWER : FILE_OLDER;
        return ERROR_SUCCESS;
    }
    if (file_modified(path))
        *result = FILE_NEWER;
    else if (!hash)
        *result = FILE_OLDER;
    else
        *result = hash_matches(path, hash) ? FILE_EQUAL : FILE_OLDER;
    return ERROR_SUCCESS;
}

/* the checksum in the header of a program or library against the one its contents give. a file that is neither is bad. */
static BOOL checksum_good(PCWSTR path)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    LARGE_INTEGER length;
    HANDLE mapping = GetFileSizeEx(file, &length) && length.QuadPart > 0x40 && length.QuadPart < 0x7FFFFFFF ?
                     CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
    CloseHandle(file);
    if (!mapping)
        return FALSE;
    const BYTE *view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(mapping);
    if (!view)
        return FALSE;
    SIZE_T size = (SIZE_T)length.QuadPart;
    BOOL good = FALSE;
    DWORD offset = *(const DWORD UNALIGNED *)(view + 0x3C);
    if (view[0] == 'M' && view[1] == 'Z' && offset < size - 0x60 - 4 && !memcmp(view + offset, "PE\0\0", 4)) {
        SIZE_T field = offset + 4 + 20 + 64;
        DWORD header = *(const DWORD UNALIGNED *)(view + field);
        ULONGLONG sum = 0;
        for (SIZE_T i = 0; i + 1 < size; i += 2) {
            if (i == field || i == field + 2)
                continue;
            sum += *(const WORD UNALIGNED *)(view + i);
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        if (size & 1)
            sum += view[size - 1];
        sum = (sum & 0xFFFF) + (sum >> 16);
        sum = (sum + (sum >> 16)) & 0xFFFF;
        good = header == (DWORD)(sum + size);
    }
    UnmapViewOfFile(view);
    return good;
}

/* whether the file of the package is to be written over the one on the machine, by the letters of the mode. */
UINT MimeMsiFileOverwrite(PCWSTR path, PCWSTR version, PCWSTR language, const MIME_MSI_FILE_HASH *hash, DWORD flags,
                          BOOL *overwrite)
{
    int result;
    UINT error = check_file_version(path, version, language, hash, &result);
    if (error)
        return error;
    int state;
    switch (result) {
    case FILE_OLDER: state = STATE_OLDER; break;
    case FILE_EQUAL: state = STATE_EQUAL; break;
    case FILE_NEWER: state = STATE_NEWER; break;
    case FILE_COMPANION: state = GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES ? STATE_COMPANION_ABSENT : STATE_COMPANION_PRESENT; break;
    case FILE_LOCKED: state = STATE_LOCKED; break;
    case FILE_DENIED: state = STATE_DENIED; break;
    default: state = STATE_ABSENT; break;
    }
    *overwrite = TRUE;
    if (flags & REINSTALLMODE_FILEREPLACE)
        return ERROR_SUCCESS;
    BOOL write = FALSE;
    BOOL present = state & 1;
    if ((flags & REINSTALLMODE_FILEMISSING) && !present)
        write = TRUE;
    if ((flags & REINSTALLMODE_FILEEXACT) && state != STATE_EQUAL)
        write = TRUE;
    if (flags & REINSTALLMODE_FILEOLDERVERSION) {
        if (flags & MIME_MSI_FILE_COMPANION)
            write |= state != STATE_NEWER;
        else
            write |= state != STATE_EQUAL && state != STATE_NEWER;
    }
    if ((flags & REINSTALLMODE_FILEEQUALVERSION) && state != STATE_NEWER)
        return ERROR_SUCCESS;
    if (!write && (flags & REINSTALLMODE_FILEVERIFY)) {
        if (!present)
            return ERROR_SUCCESS;
        write = !checksum_good(path);
    }
    *overwrite = write;
    return ERROR_SUCCESS;
}

/* a companion file follows its parent, whose file is judged by the letters with the companion rule. */
UINT MimeMsiCompanionOverwrite(PCWSTR parent_path, PCWSTR parent_version, PCWSTR parent_language, PCWSTR path,
                               const MIME_MSI_FILE_HASH *hash, DWORD flags, BOOL *overwrite)
{
    UINT error = MimeMsiFileOverwrite(parent_path, parent_version, parent_language, nullptr,
                                      flags | MIME_MSI_FILE_COMPANION, overwrite);
    if (!error && *overwrite && hash && hash_matches(path, hash))
        *overwrite = FALSE;
    return error;
}
