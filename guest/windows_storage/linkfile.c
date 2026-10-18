/*
 * the shortcut file format: the header, the target list, the link info
 * that names the volume of the target, the strings and the extra data
 * blocks, as [MS-SHLLINK] lays them out.
 *
 * what windows.storage does not understand it keeps and writes back, so a
 * shortcut made by something else survives a load and a save.
 */
#include "windows_storage_private.h"

#define ERROR_NOT_FOUND_HR HRESULT_FROM_WIN32(ERROR_NOT_FOUND)

void *LkAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void LkFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

LPWSTR LkDup(LPCWSTR text)
{
    if (!text)
        return nullptr;
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    LPWSTR copy = LkAlloc(bytes);
    if (copy)
        memcpy(copy, text, bytes);
    return copy;
}

/* a string cut to a buffer of chars characters, ended. */
void LkCopy(WCHAR *dst, LPCWSTR src, ULONG chars)
{
    ULONG n = 0;
    while (n + 1 < chars && src[n]) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;
}

/* replaces a string field, NULL clears it. */
void LkSetString(LPWSTR *field, LPCWSTR text)
{
    LPWSTR copy = LkDup(text);
    LkFree(*field);
    *field = copy;
}

static DWORD read32(const BYTE *p)
{
    DWORD value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static WORD read16(const BYTE *p)
{
    WORD value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static BYTE *write16(BYTE *p, WORD value)
{
    memcpy(p, &value, sizeof(value));
    return p + sizeof(value);
}

static BYTE *write32(BYTE *p, DWORD value)
{
    memcpy(p, &value, sizeof(value));
    return p + sizeof(value);
}

/* ------------------------------------------------------------------ extra data blocks */

LINK_BLOCK *LkFindBlock(const LINK *link, DWORD signature)
{
    for (LINK_BLOCK *b = link->Blocks; b; b = b->Next)
        if (read32(b->Data + 4) == signature)
            return b;
    return nullptr;
}

void LkRemoveBlock(LINK *link, DWORD signature)
{
    for (LINK_BLOCK **p = &link->Blocks; *p; p = &(*p)->Next) {
        if (read32((*p)->Data + 4) == signature) {
            LINK_BLOCK *gone = *p;
            *p = gone->Next;
            LkFree(gone);
            link->Dirty = TRUE;
            return;
        }
    }
}

/* adds a block, replacing the one with its signature. */
HRESULT LkSetBlock(LINK *link, const void *block)
{
    DWORD size = read32(block), signature = read32((const BYTE *)block + 4);
    if (size < 8 || size > 0x100000)
        return E_INVALIDARG;
    LINK_BLOCK *copy = LkAlloc(sizeof(*copy) + size);
    if (!copy)
        return E_OUTOFMEMORY;
    copy->Size = size;
    memcpy(copy->Data, block, size);
    LkRemoveBlock(link, signature);
    LINK_BLOCK **tail = &link->Blocks;
    while (*tail)
        tail = &(*tail)->Next;
    *tail = copy;
    link->Dirty = TRUE;
    return S_OK;
}

/* ------------------------------------------------------------------ resetting and reading */

void LkReset(LINK *link)
{
    if (link->Pidl)
        ILFree(link->Pidl);
    link->Pidl = nullptr;
    LkFree(link->Info);
    link->Info = nullptr;
    link->InfoSize = 0;
    LkFree(link->Name);
    LkFree(link->RelativePath);
    LkFree(link->WorkingDir);
    LkFree(link->Arguments);
    LkFree(link->IconLocation);
    link->Name = link->RelativePath = link->WorkingDir = link->Arguments = link->IconLocation = nullptr;
    while (link->Blocks) {
        LINK_BLOCK *next = link->Blocks->Next;
        LkFree(link->Blocks);
        link->Blocks = next;
    }
    if (link->Props) {
        link->Props->lpVtbl->Release(link->Props);
        link->Props = nullptr;
    }
    if (link->Serialized) {
        link->Serialized->lpVtbl->Release(link->Serialized);
        link->Serialized = nullptr;
    }
    link->Flags = 0;
    link->Attributes = 0;
    link->FileSize = 0;
    memset(&link->Created, 0, sizeof(link->Created));
    memset(&link->Accessed, 0, sizeof(link->Accessed));
    memset(&link->Written, 0, sizeof(link->Written));
    link->IconIndex = 0;
    link->ShowCmd = SW_SHOWNORMAL;
    link->HotKey = 0;
    link->Dirty = FALSE;
}

/* the item list of a shortcut: a size, then items to a zero size inside it. */
static HRESULT read_id_list(LINK *link, const BYTE *data, SIZE_T size, SIZE_T *offset)
{
    SIZE_T at = *offset;
    if (size - at < sizeof(WORD))
        return E_FAIL;
    SIZE_T list_size = read16(data + at);
    at += sizeof(WORD);
    if (!list_size)
        return ERROR_NOT_FOUND_HR;
    if (size - at < list_size)
        return E_FAIL;
    SIZE_T used = 0;
    BOOL terminated = FALSE;
    while (list_size - used >= sizeof(WORD)) {
        WORD item = read16(data + at + used);
        if (!item) {
            terminated = TRUE;
            used += sizeof(WORD);
            break;
        }
        if (item < sizeof(WORD) || item > list_size - used)
            break;
        used += item;
    }
    if (!terminated || used > list_size)
        return E_FAIL;
    PIDLIST_ABSOLUTE pidl = LkAlloc(list_size);
    if (!pidl)
        return E_OUTOFMEMORY;
    memcpy(pidl, data + at, list_size);
    link->Pidl = pidl;
    *offset = at + list_size;
    return S_OK;
}

static HRESULT read_link_info(LINK *link, const BYTE *data, SIZE_T size, SIZE_T *offset)
{
    SIZE_T at = *offset;
    if (size - at < sizeof(DWORD))
        return E_FAIL;
    DWORD info_size = read32(data + at);
    if (info_size < 0x1C || info_size > size - at)
        return E_FAIL;
    DWORD header = read32(data + at + 4);
    if (header < 0x1C || header > info_size)
        return E_FAIL;
    link->Info = LkAlloc(info_size);
    if (!link->Info)
        return E_OUTOFMEMORY;
    memcpy(link->Info, data + at, info_size);
    link->InfoSize = info_size;
    *offset = at + info_size;
    return S_OK;
}

/* a counted string, in UTF-16 or the ANSI code page; what does not fit the limit is dropped. */
static HRESULT read_string(const BYTE *data, SIZE_T size, SIZE_T *offset, BOOL unicode, ULONG limit, LPWSTR *out)
{
    SIZE_T at = *offset;
    if (size - at < sizeof(WORD))
        return E_FAIL;
    ULONG count = read16(data + at);
    at += sizeof(WORD);
    SIZE_T bytes = unicode ? (SIZE_T)count * sizeof(WCHAR) : count;
    if (size - at < bytes)
        return E_FAIL;
    ULONG keep = limit && count > limit ? limit : count;
    LPWSTR text = LkAlloc((keep + 1) * sizeof(WCHAR));
    if (!text)
        return E_OUTOFMEMORY;
    if (unicode) {
        memcpy(text, data + at, keep * sizeof(WCHAR));
    } else if (keep) {
        int n = MultiByteToWideChar(CP_ACP, 0, (const char *)data + at, (int)keep, text, (int)keep);
        text[n > 0 ? n : 0] = 0;
    }
    LkFree(*out);
    *out = text;
    *offset = at + bytes;
    return S_OK;
}

HRESULT LkParse(LINK *link, const BYTE *data, SIZE_T size)
{
    LkReset(link);
    if (!size)
        return S_OK;
    if (size < sizeof(DWORD) || read32(data) != LNK_HEADER_SIZE || size < LNK_HEADER_SIZE)
        return E_FAIL;
    if (memcmp(data + 4, &CLSID_ShellLink, sizeof(GUID)))
        return E_FAIL;
    link->Flags = read32(data + 20);
    link->Attributes = read32(data + 24);
    memcpy(&link->Created, data + 28, 8);
    memcpy(&link->Accessed, data + 36, 8);
    memcpy(&link->Written, data + 44, 8);
    link->FileSize = read32(data + 52);
    link->IconIndex = (int)read32(data + 56);
    link->ShowCmd = read32(data + 60);
    if (link->ShowCmd != SW_SHOWNORMAL && link->ShowCmd != SW_SHOWMAXIMIZED && link->ShowCmd != SW_SHOWMINNOACTIVE)
        link->ShowCmd = SW_SHOWNORMAL;
    link->HotKey = read16(data + 64);
    SIZE_T at = LNK_HEADER_SIZE;
    HRESULT hr = S_OK;
    if (link->Flags & SLDF_HAS_ID_LIST) {
        hr = read_id_list(link, data, size, &at);
        if (FAILED(hr)) {
            link->Flags &= ~SLDF_HAS_ID_LIST;
            return hr;
        }
    }
    if (link->Flags & SLDF_HAS_LINK_INFO) {
        hr = read_link_info(link, data, size, &at);
        if (FAILED(hr))
            return hr;
        if (link->Flags & SLDF_FORCE_NO_LINKINFO) {
            LkFree(link->Info);
            link->Info = nullptr;
            link->InfoSize = 0;
        }
    }
    BOOL unicode = (link->Flags & SLDF_UNICODE) != 0;
    if ((link->Flags & SLDF_HAS_NAME) && FAILED(hr = read_string(data, size, &at, unicode, LNK_STRING_MAX, &link->Name)))
        return hr;
    if ((link->Flags & SLDF_HAS_RELPATH) &&
        FAILED(hr = read_string(data, size, &at, unicode, LNK_STRING_MAX, &link->RelativePath)))
        return hr;
    if ((link->Flags & SLDF_HAS_WORKINGDIR) &&
        FAILED(hr = read_string(data, size, &at, unicode, LNK_STRING_MAX, &link->WorkingDir)))
        return hr;
    if ((link->Flags & SLDF_HAS_ARGS) && FAILED(hr = read_string(data, size, &at, unicode, 0, &link->Arguments)))
        return hr;
    if ((link->Flags & SLDF_HAS_ICONLOCATION) &&
        FAILED(hr = read_string(data, size, &at, unicode, LNK_STRING_MAX, &link->IconLocation)))
        return hr;
    /* the blocks run to a size under four, or to the end of a file that left the terminator out. */
    while (size - at >= sizeof(DWORD)) {
        DWORD block = read32(data + at);
        if (block < sizeof(DWORD))
            break;
        if (block < 8 || block > size - at)
            break;
        if (FAILED(hr = LkSetBlock(link, data + at)))
            return hr;
        at += block;
    }
    link->Dirty = FALSE;
    return S_OK;
}

/* ------------------------------------------------------------------ writing */

static BYTE *put_string(BYTE *p, LPCWSTR text)
{
    SIZE_T length = wcslen(text);
    if (length > 0xFFFF)
        length = 0xFFFF;
    p = write16(p, (WORD)length);
    memcpy(p, text, length * sizeof(WCHAR));
    return p + length * sizeof(WCHAR);
}

static BOOL has_text(LPCWSTR text)
{
    return text && text[0];
}

BYTE *LkBuild(LINK *link, SIZE_T *size)
{
    BOOL by_environment = (link->Flags & SLDF_PREFER_ENVIRONMENT_PATH) != 0;
    DWORD flags = (link->Flags & 0x0FFFF780) | SLDF_UNICODE;
    UINT id_list = 0;
    if (link->Pidl && !by_environment) {
        id_list = ILGetSize(link->Pidl);
        if (id_list > 0xFFFF)
            return nullptr;
        flags |= SLDF_HAS_ID_LIST;
    }
    if (link->Info && !by_environment)
        flags |= SLDF_HAS_LINK_INFO;
    if (has_text(link->Name))
        flags |= SLDF_HAS_NAME;
    if (has_text(link->RelativePath))
        flags |= SLDF_HAS_RELPATH;
    if (has_text(link->WorkingDir))
        flags |= SLDF_HAS_WORKINGDIR;
    if (has_text(link->Arguments))
        flags |= SLDF_HAS_ARGS;
    if (has_text(link->IconLocation))
        flags |= SLDF_HAS_ICONLOCATION;

    SIZE_T total = LNK_HEADER_SIZE + sizeof(DWORD);
    if (flags & SLDF_HAS_ID_LIST)
        total += sizeof(WORD) + id_list;
    if (flags & SLDF_HAS_LINK_INFO)
        total += link->InfoSize;
    LPCWSTR strings[5] = {link->Name, link->RelativePath, link->WorkingDir, link->Arguments, link->IconLocation};
    for (int i = 0; i < 5; i++)
        if (flags & (SLDF_HAS_NAME << i))
            total += sizeof(WORD) + wcsnlen(strings[i], 0xFFFF) * sizeof(WCHAR);
    for (LINK_BLOCK *b = link->Blocks; b; b = b->Next)
        total += b->Size;

    BYTE *out = LkAlloc(total);
    if (!out)
        return nullptr;
    BYTE *p = out;
    p = write32(p, LNK_HEADER_SIZE);
    memcpy(p, &CLSID_ShellLink, sizeof(GUID));
    p += sizeof(GUID);
    p = write32(p, flags);
    p = write32(p, link->Attributes);
    memcpy(p, &link->Created, 8);
    memcpy(p + 8, &link->Accessed, 8);
    memcpy(p + 16, &link->Written, 8);
    p += 24;
    p = write32(p, link->FileSize);
    p = write32(p, (DWORD)link->IconIndex);
    p = write32(p, link->ShowCmd);
    p = write16(p, link->HotKey);
    p += 10;
    if (flags & SLDF_HAS_ID_LIST) {
        p = write16(p, (WORD)id_list);
        memcpy(p, link->Pidl, id_list);
        p += id_list;
    }
    if (flags & SLDF_HAS_LINK_INFO) {
        memcpy(p, link->Info, link->InfoSize);
        p += link->InfoSize;
    }
    for (int i = 0; i < 5; i++)
        if (flags & (SLDF_HAS_NAME << i))
            p = put_string(p, strings[i]);
    for (LINK_BLOCK *b = link->Blocks; b; b = b->Next) {
        memcpy(p, b->Data, b->Size);
        p += b->Size;
    }
    p = write32(p, 0);
    *size = (SIZE_T)(p - out);
    return out;
}

/* ------------------------------------------------------------------ link info */

/* a string of the link info that is in UTF-16 when the header has the second set of offsets. */
static BOOL info_text(const BYTE *info, DWORD size, DWORD offset, DWORD unicode_offset, DWORD header, LPWSTR out,
                      ULONG chars)
{
    out[0] = 0;
    if (header >= 0x24 && unicode_offset && unicode_offset < size) {
        const WCHAR *text = (const WCHAR *)(info + unicode_offset);
        ULONG n = 0;
        while ((const BYTE *)(text + n + 1) <= info + size && text[n] && n + 1 < chars) {
            out[n] = text[n];
            n++;
        }
        out[n] = 0;
        return TRUE;
    }
    if (!offset || offset >= size)
        return FALSE;
    ULONG length = 0;
    while (offset + length < size && info[offset + length])
        length++;
    int n = length ? MultiByteToWideChar(CP_ACP, 0, (const char *)info + offset, (int)length, out, (int)chars - 1) : 0;
    out[n > 0 ? n : 0] = 0;
    return TRUE;
}

/* the target path a link info names, a local path or a share path with its suffix. */
BOOL LkInfoPath(const BYTE *info, DWORD size, LPWSTR path, ULONG chars)
{
    if (!info || size < 0x1C || !chars)
        return FALSE;
    DWORD header = read32(info + 4), flags = read32(info + 8);
    WCHAR base[MAX_PATH], suffix[MAX_PATH];
    base[0] = suffix[0] = 0;
    DWORD suffix_offset = read32(info + 24);
    DWORD suffix_unicode = header >= 0x24 ? read32(info + 32) : 0;
    if (flags & 1) {
        DWORD base_unicode = header >= 0x24 ? read32(info + 28) : 0;
        if (!info_text(info, size, read32(info + 16), base_unicode, header, base, MAX_PATH))
            return FALSE;
    } else if (flags & 2) {
        DWORD network = read32(info + 20);
        if (network + 20 > size)
            return FALSE;
        const BYTE *n = info + network;
        DWORD net_name = read32(n + 8), net_unicode = 0;
        if (net_name > 0x14 && network + 28 <= size)
            net_unicode = read32(n + 20);
        if (!info_text(n, size - network, net_name, net_unicode, net_name > 0x14 ? 0x24 : 0x1C, base, MAX_PATH))
            return FALSE;
    } else {
        return FALSE;
    }
    info_text(info, size, suffix_offset, suffix_unicode, header, suffix, MAX_PATH);
    SIZE_T n = wcslen(base), s = wcslen(suffix);
    BOOL separator = (flags & 1) == 0 && s && n && base[n - 1] != L'\\';
    if (n + s + (separator ? 1 : 0) >= chars)
        return FALSE;
    memcpy(path, base, n * sizeof(WCHAR));
    if (separator)
        path[n++] = L'\\';
    memcpy(path + n, suffix, (s + 1) * sizeof(WCHAR));
    return TRUE;
}

/* the link info of a local path: the volume it is on and the path from its root, or NULL. */
BYTE *LkInfoCreate(LPCWSTR path, DWORD *info_size)
{
    *info_size = 0;
    if (!path[0] || path[1] != L':' || path[2] != L'\\')
        return nullptr;
    WCHAR root[4] = {path[0], L':', L'\\', 0};
    UINT type = GetDriveTypeW(root);
    if (type != DRIVE_REMOVABLE && type != DRIVE_FIXED && type != DRIVE_CDROM && type != DRIVE_RAMDISK)
        return nullptr;
    WCHAR label[MAX_PATH + 1];
    DWORD serial = 0;
    label[0] = 0;
    if (!GetVolumeInformationW(root, label, MAX_PATH, &serial, nullptr, nullptr, nullptr, 0))
        return nullptr;
    char ansi_label[MAX_PATH * 2], ansi_path[MAX_PATH * 4];
    BOOL lossy = FALSE, lossy_path = FALSE;
    int label_bytes = WideCharToMultiByte(CP_ACP, 0, label, -1, ansi_label, sizeof(ansi_label), "?", &lossy);
    int path_bytes = WideCharToMultiByte(CP_ACP, 0, path, -1, ansi_path, sizeof(ansi_path), "?", &lossy_path);
    if (label_bytes <= 0 || path_bytes <= 0)
        return nullptr;
    /* a path the code page cannot spell gets the second set of offsets, with the path in UTF-16. */
    DWORD header = lossy_path ? 0x24 : 0x1C;
    DWORD volume = 16 + (DWORD)label_bytes;
    DWORD base_offset = header + volume;
    DWORD suffix_offset = base_offset + (DWORD)path_bytes;
    DWORD total = suffix_offset + 1;
    DWORD base_unicode = 0, suffix_unicode = 0;
    SIZE_T wide_bytes = (wcslen(path) + 1) * sizeof(WCHAR);
    if (lossy_path) {
        base_unicode = total;
        suffix_unicode = base_unicode + (DWORD)wide_bytes;
        total = suffix_unicode + sizeof(WCHAR);
    }
    BYTE *info = LkAlloc(total);
    if (!info)
        return nullptr;
    BYTE *p = write32(info, total);
    p = write32(p, header);
    p = write32(p, 1);
    p = write32(p, header);
    p = write32(p, base_offset);
    p = write32(p, 0);
    p = write32(p, suffix_offset);
    if (lossy_path) {
        p = write32(p, base_unicode);
        p = write32(p, suffix_unicode);
    }
    p = write32(p, volume);
    p = write32(p, type);
    p = write32(p, serial);
    p = write32(p, 16);
    memcpy(p, ansi_label, (SIZE_T)label_bytes);
    memcpy(info + base_offset, ansi_path, (SIZE_T)path_bytes);
    if (lossy_path)
        memcpy(info + base_unicode, path, wide_bytes);
    *info_size = total;
    return info;
}
