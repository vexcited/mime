/*
 * item id lists in the windows format: each item is a 16-bit size followed by size - 2 bytes, and a zero size ends
 * the list.
 *
 * path items match what windows builds: a "This PC" class id item, type 0x1f, a drive item, type 0x2f with the root
 * path in ANSI, then a file entry per folder or file, types 0x31 and 0x32: size, dos time, attributes, the ANSI name
 * and a 0xbeef0004 version 9 extension block with the creation and access times and the UTF-16 name. lists from
 * windows shortcuts parse too, with extension blocks of version 3 and up and the UTF-16 names of types 0x35 and 0x36.
 * other items, like virtual folders, only name their class.
 */
#include "shell32_private.h"

#define ID_CLSID 0x1F
#define ID_DRIVE_MASK 0x70
#define ID_DRIVE 0x20
#define ID_FILE_ENTRY 0x30
#define ID_FOLDER_BIT 0x01
#define ID_FILE_BIT 0x02
#define ID_UNICODE_BIT 0x04
#define ID_NETWORK 0xC3

#define EXT_SIGNATURE 0xBEEF0004u
#define EXT_VERSION 9
#define EXT_FIXED_SIZE 46

#define FILE_HEADER 14

static const BYTE *item_next(const BYTE *item)
{
    return item + *(const USHORT *)item;
}

/* ------------------------------------------------------------------ item list functions */

SHSTDAPI UINT WINAPI ILGetSize(PCUIDLIST_RELATIVE pidl)
{
    if (!pidl)
        return 0;
    UINT size = sizeof(USHORT);
    for (const BYTE *p = (const BYTE *)pidl; *(const USHORT *)p; p = item_next(p))
        size += *(const USHORT *)p;
    return size;
}

SHSTDAPI PIDLIST_RELATIVE WINAPI ILClone(PCUIDLIST_RELATIVE pidl)
{
    if (!pidl)
        return nullptr;
    UINT size = ILGetSize(pidl);
    PIDLIST_RELATIVE copy = ShAlloc(size);
    if (copy)
        memcpy(copy, pidl, size);
    return copy;
}

SHSTDAPI void WINAPI ILFree(PIDLIST_RELATIVE pidl)
{
    ShFree(pidl);
}

SHSTDAPI PUIDLIST_RELATIVE WINAPI ILGetNext(PCUIDLIST_RELATIVE pidl)
{
    if (!pidl || !pidl->mkid.cb)
        return nullptr;
    return (PUIDLIST_RELATIVE)item_next((const BYTE *)pidl);
}

SHSTDAPI PUITEMID_CHILD WINAPI ILFindLastID(PCIDLIST_RELATIVE pidl)
{
    if (!pidl)
        return nullptr;
    const BYTE *last = (const BYTE *)pidl;
    for (const BYTE *p = last; *(const USHORT *)p; p = item_next(p))
        last = p;
    return (PUITEMID_CHILD)last;
}

SHSTDAPI BOOL WINAPI ILRemoveLastID(PUIDLIST_RELATIVE pidl)
{
    if (!pidl || !pidl->mkid.cb)
        return FALSE;
    *(USHORT *)ILFindLastID(pidl) = 0;
    return TRUE;
}

SHSTDAPI PIDLIST_RELATIVE WINAPI ILCloneFirst(PCUIDLIST_RELATIVE pidl)
{
    if (!pidl || !pidl->mkid.cb)
        return nullptr;
    USHORT size = pidl->mkid.cb;
    BYTE *copy = ShAlloc(size + sizeof(USHORT));
    if (copy) {
        memcpy(copy, pidl, size);
        *(USHORT *)(copy + size) = 0;
    }
    return (PIDLIST_RELATIVE)copy;
}

SHSTDAPI PIDLIST_ABSOLUTE WINAPI ILCombine(PCIDLIST_ABSOLUTE first, PCUIDLIST_RELATIVE second)
{
    if (!first && !second)
        return nullptr;
    if (!first)
        return ILClone(second);
    if (!second)
        return ILClone(first);
    UINT head = ILGetSize(first) - sizeof(USHORT), tail = ILGetSize(second);
    BYTE *list = ShAlloc(head + tail);
    if (list) {
        memcpy(list, first, head);
        memcpy(list + head, second, tail);
    }
    return (PIDLIST_ABSOLUTE)list;
}

SHSTDAPI PIDLIST_RELATIVE WINAPI ILAppendID(PIDLIST_RELATIVE pidl, const SHITEMID *id, BOOL append)
{
    if (!id)
        return pidl;
    UINT head = pidl ? ILGetSize(pidl) - sizeof(USHORT) : 0;
    UINT item = id->cb;
    BYTE *list = pidl ? HeapReAlloc(GetProcessHeap(), 0, pidl, head + item + sizeof(USHORT))
                      : ShAlloc(item + sizeof(USHORT));
    if (!list)
        return nullptr;
    if (append) {
        memcpy(list + head, id, item);
        *(USHORT *)(list + head + item) = 0;
    } else {
        memmove(list + item, list, head + sizeof(USHORT));
        memcpy(list, id, item);
    }
    return (PIDLIST_RELATIVE)list;
}

/* ------------------------------------------------------------------ item names */

static BOOL is_file_entry(const BYTE *item)
{
    return (item[2] & ID_DRIVE_MASK) == ID_FILE_ENTRY && (item[2] & (ID_FOLDER_BIT | ID_FILE_BIT));
}

static BOOL is_drive(const BYTE *item)
{
    return (item[2] & ID_DRIVE_MASK) == ID_DRIVE;
}

/* the last word of a file entry is the offset of its extension block. */
static const BYTE *find_extension(const BYTE *item)
{
    USHORT size = *(const USHORT *)item;
    if (size < FILE_HEADER + 2 + 8)
        return nullptr;
    USHORT offset = *(const USHORT *)(item + size - sizeof(USHORT));
    if (offset < FILE_HEADER || offset + 8 > size - sizeof(USHORT))
        return nullptr;
    const BYTE *ext = item + offset;
    if (*(const DWORD *)(ext + 4) != EXT_SIGNATURE || *(const USHORT *)ext + offset > size ||
        *(const USHORT *)(ext + 2) < 3)
        return nullptr;
    return ext;
}

/* prefers the UTF-16 name of the extension block when the item has one. */
static BOOL item_name(const BYTE *item, WCHAR *name, ULONG chars)
{
    USHORT size = *(const USHORT *)item;
    if (is_drive(item)) {
        ULONG n = 0;
        while (3 + n < size && item[3 + n] && n + 1 < chars) {
            name[n] = item[3 + n];
            n++;
        }
        name[n] = 0;
        return n != 0;
    }
    if (!is_file_entry(item))
        return FALSE;
    const BYTE *ext = find_extension(item);
    if (ext) {
        USHORT offset = *(const USHORT *)(ext + 16);
        const WCHAR *text = (const WCHAR *)(ext + offset);
        const WCHAR *limit = (const WCHAR *)(item + size - sizeof(USHORT));
        if (offset >= 18 && (const BYTE *)text < (const BYTE *)limit) {
            ULONG n = 0;
            while (text + n < limit && text[n] && n + 1 < chars) {
                name[n] = text[n];
                n++;
            }
            if (text + n < limit && !text[n] && n) {
                name[n] = 0;
                return TRUE;
            }
        }
    }
    const BYTE *limit = item + size;
    if (item[2] & ID_UNICODE_BIT) {
        const WCHAR *text = (const WCHAR *)(item + FILE_HEADER);
        ULONG n = 0;
        while ((const BYTE *)(text + n + 1) <= limit && text[n] && n + 1 < chars) {
            name[n] = text[n];
            n++;
        }
        name[n] = 0;
        return n != 0;
    }
    ULONG length = 0;
    while (item + FILE_HEADER + length < limit && item[FILE_HEADER + length])
        length++;
    int n = MultiByteToWideChar(CP_ACP, 0, (const char *)item + FILE_HEADER, (int)length, name, (int)chars - 1);
    name[n > 0 ? n : 0] = 0;
    return n > 0;
}

BOOL ShIdListLastName(PCUIDLIST_ABSOLUTE pidl, LPWSTR name, ULONG chars)
{
    const BYTE *last = (const BYTE *)ILFindLastID(pidl);
    return last && *(const USHORT *)last && item_name(last, name, chars);
}

/* ------------------------------------------------------------------ building lists */

static void dos_time(const FILETIME *time, USHORT *date, USHORT *clock)
{
    FILETIME local;
    *date = *clock = 0;
    if ((time->dwLowDateTime || time->dwHighDateTime) && FileTimeToLocalFileTime(time, &local))
        FileTimeToDosDateTime(&local, date, clock);
}

static BYTE *put_word(BYTE *p, USHORT value)
{
    memcpy(p, &value, sizeof(value));
    return p + sizeof(value);
}

static BYTE *put_dword(BYTE *p, DWORD value)
{
    memcpy(p, &value, sizeof(value));
    return p + sizeof(value);
}

static PIDLIST_ABSOLUTE class_item(BYTE sort, const GUID *clsid)
{
    BYTE *list = ShAllocZero(4 + sizeof(GUID) + sizeof(USHORT));
    if (!list)
        return nullptr;
    put_word(list, 4 + sizeof(GUID));
    list[2] = ID_CLSID;
    list[3] = sort;
    memcpy(list + 4, clsid, sizeof(GUID));
    return (PIDLIST_ABSOLUTE)list;
}

static BYTE *append_item(BYTE *list, SIZE_T *used, const BYTE *item, SIZE_T size)
{
    BYTE *grown = HeapReAlloc(GetProcessHeap(), 0, list, *used + size + sizeof(USHORT));
    if (!grown) {
        ShFree(list);
        return nullptr;
    }
    memcpy(grown + *used - sizeof(USHORT), item, size);
    *used += size;
    *(USHORT *)(grown + *used - sizeof(USHORT)) = 0;
    return grown;
}

static BYTE *make_entry(LPCWSTR name, BOOL folder, const WIN32_FILE_ATTRIBUTE_DATA *data, SIZE_T *size)
{
    char ansi[MAX_PATH * 2];
    BOOL lossy = FALSE;
    int bytes = WideCharToMultiByte(CP_ACP, 0, name, -1, ansi, sizeof(ansi) - 1, "?", &lossy);
    if (bytes <= 0) {
        ansi[0] = 0;
        bytes = 1;
    }
    SIZE_T name_bytes = ((SIZE_T)bytes + 1) & ~(SIZE_T)1;
    SIZE_T wide_bytes = (wcslen(name) + 1) * sizeof(WCHAR);
    SIZE_T ext_size = EXT_FIXED_SIZE + wide_bytes + sizeof(USHORT);
    SIZE_T total = FILE_HEADER + name_bytes + ext_size;
    if (total > 0xFFFF)
        return nullptr;
    BYTE *item = ShAllocZero(total);
    if (!item)
        return nullptr;
    USHORT mdate, mtime, cdate, ctime, adate, atime;
    mdate = mtime = cdate = ctime = adate = atime = 0;
    DWORD attributes = folder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE, file_size = 0;
    if (data) {
        dos_time(&data->ftLastWriteTime, &mdate, &mtime);
        dos_time(&data->ftCreationTime, &cdate, &ctime);
        dos_time(&data->ftLastAccessTime, &adate, &atime);
        attributes = data->dwFileAttributes;
        file_size = data->nFileSizeHigh ? 0xFFFFFFFF : data->nFileSizeLow;
    }
    BYTE *p = put_word(item, (USHORT)total);
    *p++ = folder ? ID_FILE_ENTRY | ID_FOLDER_BIT : ID_FILE_ENTRY | ID_FILE_BIT;
    *p++ = 0;
    p = put_dword(p, folder ? 0 : file_size);
    p = put_word(p, mdate);
    p = put_word(p, mtime);
    p = put_word(p, (USHORT)attributes);
    memcpy(p, ansi, (SIZE_T)bytes);
    p = item + FILE_HEADER + name_bytes;
    BYTE *ext = p;
    p = put_word(p, (USHORT)ext_size);
    p = put_word(p, EXT_VERSION);
    p = put_dword(p, EXT_SIGNATURE);
    p = put_word(p, cdate);
    p = put_word(p, ctime);
    p = put_word(p, adate);
    p = put_word(p, atime);
    put_word(p, EXT_FIXED_SIZE);
    memcpy(ext + EXT_FIXED_SIZE, name, wide_bytes);
    put_word(item + total - sizeof(USHORT), (USHORT)(FILE_HEADER + name_bytes));
    *size = total;
    return item;
}

static BYTE *make_drive(WCHAR letter, SIZE_T *size)
{
    *size = 25;
    BYTE *item = ShAllocZero(*size);
    if (item) {
        put_word(item, 25);
        item[2] = ID_DRIVE | 0x0F;
        item[3] = (BYTE)letter;
        item[4] = ':';
        item[5] = '\\';
    }
    return item;
}

/* the UNC share root item, which comes before the folders of a network path. */
static BYTE *make_share(LPCWSTR share, SIZE_T length, SIZE_T *size)
{
    char ansi[MAX_PATH * 2];
    WCHAR text[MAX_PATH];
    if (length >= MAX_PATH)
        return nullptr;
    memcpy(text, share, length * sizeof(WCHAR));
    text[length] = 0;
    int bytes = WideCharToMultiByte(CP_ACP, 0, text, -1, ansi, sizeof(ansi) - 1, "?", nullptr);
    if (bytes <= 0)
        return nullptr;
    *size = ((SIZE_T)5 + bytes + 1) & ~(SIZE_T)1;
    BYTE *item = ShAllocZero(*size);
    if (item) {
        put_word(item, (USHORT)*size);
        item[2] = ID_NETWORK;
        item[3] = 1;
        memcpy(item + 4, ansi, (SIZE_T)bytes);
    }
    return item;
}

static const GUID g_computer = {0x20D04FE0, 0x3AEA, 0x1069, {0xA2, 0xD8, 0x08, 0x00, 0x2B, 0x30, 0x30, 0x9D}};
static const GUID g_network = {0xF02C1A0D, 0xBE21, 0x4350, {0x88, 0xB0, 0x73, 0x67, 0xFC, 0x96, 0xEF, 0x3C}};
static const GUID g_users_files = {0x59031A47, 0x3F72, 0x44A7, {0x89, 0xC5, 0x55, 0x95, 0xFE, 0x6B, 0x30, 0xEE}};

PIDLIST_ABSOLUTE ShComputerList(void)
{
    return class_item(0x50, &g_computer);
}

PIDLIST_ABSOLUTE ShClassList(BYTE sort, const GUID *clsid)
{
    return class_item(sort, clsid);
}

/* the path need not exist. when it does, the disk gives times, sizes and attributes. */
PIDLIST_ABSOLUTE ShIdListFromPath(LPCWSTR path)
{
    SIZE_T length = wcslen(path);
    SIZE_T pos;
    BYTE *list;
    SIZE_T used;
    if (length >= 3 && path[1] == L':' && path[2] == L'\\' &&
        ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z'))) {
        list = (BYTE *)class_item(0x50, &g_computer);
        if (!list)
            return nullptr;
        used = ILGetSize((PCUIDLIST_RELATIVE)list);
        SIZE_T size;
        BYTE *drive = make_drive(path[0] >= L'a' ? path[0] - 32 : path[0], &size);
        if (!drive) {
            ShFree(list);
            return nullptr;
        }
        list = append_item(list, &used, drive, size);
        ShFree(drive);
        pos = 3;
    } else if (length >= 5 && path[0] == L'\\' && path[1] == L'\\') {
        LPCWSTR p = path + 2;
        int parts = 0;
        while (*p && parts < 2) {
            while (*p && *p != L'\\')
                p++;
            parts++;
            if (*p && parts < 2)
                p++;
        }
        if (parts < 2)
            return nullptr;
        list = (BYTE *)class_item(0x58, &g_network);
        if (!list)
            return nullptr;
        used = ILGetSize((PCUIDLIST_RELATIVE)list);
        SIZE_T size;
        BYTE *share = make_share(path, (SIZE_T)(p - path), &size);
        if (!share) {
            ShFree(list);
            return nullptr;
        }
        list = append_item(list, &used, share, size);
        ShFree(share);
        pos = (SIZE_T)(p - path);
        if (path[pos] == L'\\')
            pos++;
    } else {
        return nullptr;
    }
    if (!list)
        return nullptr;
    WCHAR *full = ShAlloc((length + 1) * sizeof(WCHAR));
    if (!full) {
        ShFree(list);
        return nullptr;
    }
    memcpy(full, path, (length + 1) * sizeof(WCHAR));
    while (list && pos < length) {
        SIZE_T end = pos;
        while (end < length && full[end] != L'\\')
            end++;
        if (end == pos) {
            pos++;
            continue;
        }
        if (end - pos >= MAX_PATH) {
            ShFree(list);
            list = nullptr;
            break;
        }
        WCHAR name[MAX_PATH];
        memcpy(name, full + pos, (end - pos) * sizeof(WCHAR));
        name[end - pos] = 0;
        WCHAR saved = full[end];
        full[end] = 0;
        WIN32_FILE_ATTRIBUTE_DATA data;
        BOOL exists = GetFileAttributesExW(full, GetFileExInfoStandard, &data);
        full[end] = saved;
        BOOL last = end >= length || !full[end + 1];
        BOOL folder = exists ? (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
                             : !last || full[length - 1] == L'\\';
        SIZE_T size;
        BYTE *item = make_entry(name, folder, exists ? &data : nullptr, &size);
        if (!item) {
            ShFree(list);
            list = nullptr;
            break;
        }
        list = append_item(list, &used, item, size);
        ShFree(item);
        pos = end + 1;
    }
    ShFree(full);
    return (PIDLIST_ABSOLUTE)list;
}

PIDLIST_ABSOLUTE ShChildList(PCIDLIST_ABSOLUTE folder, const WIN32_FIND_DATAW *find)
{
    WIN32_FILE_ATTRIBUTE_DATA data = {find->dwFileAttributes, find->ftCreationTime, find->ftLastAccessTime,
                                      find->ftLastWriteTime,  find->nFileSizeHigh,  find->nFileSizeLow};
    SIZE_T size;
    BYTE *entry = make_entry(find->cFileName, (find->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, &data, &size);
    if (!entry)
        return nullptr;
    SIZE_T head = ILGetSize(folder) - sizeof(USHORT);
    BYTE *list = ShAlloc(head + size + sizeof(USHORT));
    if (list) {
        memcpy(list, folder, head);
        memcpy(list + head, entry, size);
        *(USHORT *)(list + head + size) = 0;
    }
    ShFree(entry);
    return (PIDLIST_ABSOLUTE)list;
}

/* ------------------------------------------------------------------ reading lists */

/* the folder path of a class id item, if it has one. */
static BOOL class_path(const BYTE *item, WCHAR *path)
{
    const GUID *clsid = (const GUID *)(item + 4);
    if (*(const USHORT *)item < 4 + sizeof(GUID))
        return FALSE;
    const GUID *folder = IsEqualGUID(clsid, &g_users_files) ? &FOLDERID_Profile : clsid;
    PWSTR text;
    if (FAILED(SHGetKnownFolderPath(folder, KF_FLAG_DONT_VERIFY, nullptr, &text)))
        return FALSE;
    BOOL ok = wcslen(text) < MAX_PATH;
    if (ok)
        wcscpy(path, text);
    ShFree(text);
    return ok;
}

BOOL ShIsComputerItem(const BYTE *item)
{
    return item[2] == ID_CLSID && *(const USHORT *)item >= 4 + sizeof(GUID) &&
           IsEqualGUID((const GUID *)(item + 4), &g_computer);
}

BOOL ShPathFromIdList(PCUIDLIST_ABSOLUTE pidl, LPWSTR path, DWORD chars)
{
    if (!path || !chars)
        return FALSE;
    path[0] = 0;
    if (!pidl)
        return FALSE;
    WCHAR buffer[MAX_PATH * 2];
    SIZE_T length = 0;
    const BYTE *item = (const BYTE *)pidl;
    if (!*(const USHORT *)item) {
        PWSTR desktop;
        if (FAILED(SHGetKnownFolderPath(&FOLDERID_Desktop, KF_FLAG_DONT_VERIFY, nullptr, &desktop)))
            return FALSE;
        SIZE_T n = wcslen(desktop);
        BOOL ok = n < chars;
        if (ok)
            wcscpy(path, desktop);
        ShFree(desktop);
        return ok;
    }
    if (item[2] == ID_CLSID) {
        if (ShIsComputerItem(item)) {
            item = item_next(item);
            if (!*(const USHORT *)item || !is_drive(item))
                return FALSE;
        } else if (class_path(item, buffer)) {
            length = wcslen(buffer);
            item = item_next(item);
        } else if (*(const USHORT *)item >= 4 + sizeof(GUID) && IsEqualGUID((const GUID *)(item + 4), &g_network)) {
            item = item_next(item);
            if (!*(const USHORT *)item || item[2] != ID_NETWORK)
                return FALSE;
        } else {
            return FALSE;
        }
    }
    for (; *(const USHORT *)item; item = item_next(item)) {
        WCHAR name[MAX_PATH];
        if (item[2] == ID_NETWORK && !length) {
            int n = MultiByteToWideChar(CP_ACP, 0, (const char *)item + 4, -1, buffer, MAX_PATH);
            if (n <= 1)
                return FALSE;
            length = (SIZE_T)n - 1;
            continue;
        }
        if (!item_name(item, name, MAX_PATH))
            return FALSE;
        SIZE_T n = wcslen(name);
        if (is_drive(item)) {
            if (length)
                return FALSE;
            memcpy(buffer, name, (n + 1) * sizeof(WCHAR));
            length = n;
            continue;
        }
        if (length + n + 2 >= RTL_NUMBER_OF(buffer))
            return FALSE;
        if (length && buffer[length - 1] != L'\\')
            buffer[length++] = L'\\';
        memcpy(buffer + length, name, (n + 1) * sizeof(WCHAR));
        length += n;
    }
    if (length >= chars)
        return FALSE;
    memcpy(path, buffer, (length + 1) * sizeof(WCHAR));
    return TRUE;
}

/* file system lists compare by path, the rest by bytes. */
SHSTDAPI BOOL WINAPI ILIsEqual(PCIDLIST_ABSOLUTE a, PCIDLIST_ABSOLUTE b)
{
    if (a == b)
        return TRUE;
    if (!a || !b)
        return FALSE;
    WCHAR path_a[MAX_PATH], path_b[MAX_PATH];
    if (ShPathFromIdList(a, path_a, MAX_PATH) && ShPathFromIdList(b, path_b, MAX_PATH))
        return !_wcsicmp(path_a, path_b);
    UINT size = ILGetSize(a);
    return size == ILGetSize(b) && !memcmp(a, b, size);
}

/* items match by kind and name. */
static BOOL same_item(const BYTE *a, const BYTE *b)
{
    WCHAR name_a[MAX_PATH], name_b[MAX_PATH];
    if (item_name(a, name_a, MAX_PATH) && item_name(b, name_b, MAX_PATH))
        return !_wcsicmp(name_a, name_b);
    return *(const USHORT *)a == *(const USHORT *)b && !memcmp(a, b, *(const USHORT *)a);
}

SHSTDAPI PUIDLIST_RELATIVE WINAPI ILFindChild(PCIDLIST_ABSOLUTE parent, PCIDLIST_ABSOLUTE child)
{
    if (!parent || !child)
        return nullptr;
    const BYTE *p = (const BYTE *)parent, *c = (const BYTE *)child;
    while (*(const USHORT *)p) {
        if (!*(const USHORT *)c || !same_item(p, c))
            return nullptr;
        p = item_next(p);
        c = item_next(c);
    }
    return (PUIDLIST_RELATIVE)c;
}

SHSTDAPI BOOL WINAPI ILIsParent(PCIDLIST_ABSOLUTE parent, PCIDLIST_ABSOLUTE child, BOOL immediate)
{
    PUIDLIST_RELATIVE rest = ILFindChild(parent, child);
    if (!rest || !rest->mkid.cb)
        return FALSE;
    return !immediate || !ILGetNext(rest)->mkid.cb;
}

SHSTDAPI PIDLIST_ABSOLUTE WINAPI ILCreateFromPathA(PCSTR path)
{
    LPWSTR wide = ShWiden(path);
    if (!wide)
        return nullptr;
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(wide);
    ShFree(wide);
    return pidl;
}

/* stream format: a 16-bit size that counts the terminating zero item, then the bytes. */
SHSTDAPI HRESULT WINAPI ILSaveToStream(IStream *stream, PCUIDLIST_RELATIVE pidl)
{
    UINT size = ILGetSize(pidl);
    if (!pidl)
        size = sizeof(USHORT);
    if (size > 0xFFFF)
        return TYPE_E_SIZETOOBIG;
    USHORT prefix = (USHORT)size;
    HRESULT hr = stream->lpVtbl->Write(stream, &prefix, sizeof(prefix), nullptr);
    if (SUCCEEDED(hr) && pidl)
        hr = stream->lpVtbl->Write(stream, pidl, size, nullptr);
    return hr;
}

SHSTDAPI HRESULT WINAPI ILLoadFromStreamEx(IStream *stream, PIDLIST_RELATIVE *pidl)
{
    *pidl = nullptr;
    USHORT size = 0;
    HRESULT hr = stream->lpVtbl->Read(stream, &size, sizeof(size), nullptr);
    if (FAILED(hr))
        return hr;
    if (!size)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    BYTE *list = ShAllocZero(size);
    if (!list)
        return HRESULT_FROM_WIN32(ERROR_OUTOFMEMORY);
    hr = stream->lpVtbl->Read(stream, list, size, nullptr);
    if (SUCCEEDED(hr)) {
        hr = E_FAIL;
        UINT used = sizeof(USHORT);
        const BYTE *item = list;
        while (size >= used && *(const USHORT *)item >= sizeof(USHORT) && *(const USHORT *)item <= size - used) {
            used += *(const USHORT *)item;
            item += *(const USHORT *)item;
        }
        if (used <= size && !*(const USHORT *)item) {
            *pidl = (PIDLIST_RELATIVE)list;
            return S_OK;
        }
    }
    ShFree(list);
    return hr;
}

/* nothing is checked on disk: a missing path is taken as a file. */
SHSTDAPI PIDLIST_ABSOLUTE WINAPI SHSimpleIDListFromPath(PCWSTR path)
{
    if (!path)
        return nullptr;
    WCHAR full[MAX_PATH];
    DWORD n = GetFullPathNameW(path, MAX_PATH, full, nullptr);
    if (!n || n >= MAX_PATH)
        return nullptr;
    return ShIdListFromPath(full);
}
