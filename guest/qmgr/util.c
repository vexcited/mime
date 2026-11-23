#include "qmgr.h"

WCHAR *QmDup(const WCHAR *text)
{
    if (!text)
        return nullptr;
    size_t size = (wcslen(text) + 1) * sizeof(WCHAR);
    WCHAR *copy = LocalAlloc(LMEM_FIXED, size);
    if (copy)
        memcpy(copy, text, size);
    return copy;
}

WCHAR *QmTaskDup(const WCHAR *text)
{
    if (!text)
        return nullptr;
    size_t size = (wcslen(text) + 1) * sizeof(WCHAR);
    WCHAR *copy = CoTaskMemAlloc(size);
    if (copy)
        memcpy(copy, text, size);
    return copy;
}

void QmReplace(WCHAR **slot, const WCHAR *text)
{
    WCHAR *copy = QmDup(text);
    if (*slot)
        LocalFree(*slot);
    *slot = copy;
}

UINT64 QmNow(void)
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    return QmTime(&now);
}

FILETIME QmFileTime(UINT64 value)
{
    FILETIME time = {(DWORD)value, (DWORD)(value >> 32)};
    return time;
}

UINT64 QmTime(const FILETIME *time)
{
    return (UINT64)time->dwHighDateTime << 32 | time->dwLowDateTime;
}

/* a sorted set of disjoint ranges, which a new range merges into. */
BOOL QmRangesAdd(RANGES *set, UINT64 offset, UINT64 length)
{
    if (!length)
        return TRUE;
    UINT64 end = offset + length;
    DWORD i = 0;
    while (i < set->count && set->items[i].InitialOffset + set->items[i].Length < offset)
        i++;
    DWORD k = i;
    while (k < set->count && set->items[k].InitialOffset <= end) {
        UINT64 start = set->items[k].InitialOffset, stop = start + set->items[k].Length;
        if (start < offset)
            offset = start;
        if (stop > end)
            end = stop;
        k++;
    }
    if (k == i) {
        if (set->count == set->capacity) {
            DWORD capacity = set->capacity ? set->capacity * 2 : 8;
            BG_FILE_RANGE *items = LocalAlloc(LMEM_FIXED, capacity * sizeof(*items));
            if (!items)
                return FALSE;
            if (set->count)
                memcpy(items, set->items, set->count * sizeof(*items));
            LocalFree(set->items);
            set->items = items;
            set->capacity = capacity;
        }
        memmove(&set->items[i + 1], &set->items[i], (set->count - i) * sizeof(*set->items));
        set->count++;
    } else if (k > i + 1) {
        memmove(&set->items[i + 1], &set->items[k], (set->count - k) * sizeof(*set->items));
        set->count -= k - i - 1;
    }
    set->items[i].InitialOffset = offset;
    set->items[i].Length = end - offset;
    return TRUE;
}

/* the bytes from offset up to end that the set holds. */
UINT64 QmRangesCovered(const RANGES *set, UINT64 offset, UINT64 end)
{
    UINT64 covered = 0;
    for (DWORD i = 0; i < set->count; i++) {
        UINT64 start = set->items[i].InitialOffset, stop = start + set->items[i].Length;
        if (start < offset)
            start = offset;
        if (stop > end)
            stop = end;
        if (stop > start)
            covered += stop - start;
    }
    return covered;
}

void QmRangesFree(RANGES *set)
{
    LocalFree(set->items);
    memset(set, 0, sizeof(*set));
}

/* GetProtocolType: http and https urls, and file urls or unc names for the file protocol. */
int QmProtocol(const WCHAR *remote)
{
    if (!remote)
        return PROTOCOL_NONE;
    if (remote[0] == '\\' && remote[1] == '\\' && remote[2] && remote[2] != '\\' && remote[2] != '?' &&
        remote[2] != '.') {
        const WCHAR *share = wcschr(remote + 2, '\\');
        return share && share[1] && share[1] != '\\' ? PROTOCOL_FILE : PROTOCOL_NONE;
    }
    if (!_wcsnicmp(remote, L"file:", 5)) {
        WCHAR *path = QmFilePath(remote);
        BOOL ok = path != nullptr;
        LocalFree(path);
        return ok ? PROTOCOL_FILE : PROTOCOL_NONE;
    }
    if (_wcsnicmp(remote, L"http://", 7) && _wcsnicmp(remote, L"https://", 8))
        return PROTOCOL_NONE;
    URL_COMPONENTS parts = {sizeof(parts)};
    parts.dwHostNameLength = (DWORD)-1;
    parts.dwUrlPathLength = (DWORD)-1;
    if (!WinHttpCrackUrl(remote, 0, 0, &parts) || !parts.dwHostNameLength)
        return PROTOCOL_NONE;
    return parts.nScheme == INTERNET_SCHEME_HTTPS ? PROTOCOL_HTTPS : PROTOCOL_HTTP;
}

static int hex(WCHAR c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* the path a file url or unc name names: file://server/share/x is \\server\share\x, file:///C:/x is C:\x. */
WCHAR *QmFilePath(const WCHAR *remote)
{
    if (remote[0] == '\\' && remote[1] == '\\')
        return QmDup(remote);
    if (_wcsnicmp(remote, L"file:", 5))
        return nullptr;
    const WCHAR *p = remote + 5;
    WCHAR *path = LocalAlloc(LMEM_FIXED, (wcslen(remote) + 3) * sizeof(WCHAR));
    if (!path)
        return nullptr;
    size_t n = 0;
    if (p[0] == '/' && p[1] == '/') {
        p += 2;
        if (*p == '/')
            p++;
        else if (!_wcsnicmp(p, L"localhost/", 10))
            p += 10;
        else {
            path[n++] = '\\';
            path[n++] = '\\';
        }
    } else if (*p == '/')
        p++;
    for (; *p; p++) {
        WCHAR c = *p;
        if (c == '%' && hex(p[1]) >= 0 && hex(p[2]) >= 0) {
            c = (WCHAR)(hex(p[1]) * 16 + hex(p[2]));
            p += 2;
        } else if (c == '/')
            c = '\\';
        path[n++] = c;
    }
    path[n] = 0;
    BOOL drive = n >= 3 && ((path[0] | 0x20) >= 'a' && (path[0] | 0x20) <= 'z') && path[1] == ':' && path[2] == '\\';
    BOOL unc = n >= 5 && path[0] == '\\' && path[1] == '\\' && path[2] != '\\' && wcschr(path + 2, '\\');
    if (!drive && !unc) {
        LocalFree(path);
        return nullptr;
    }
    return path;
}
