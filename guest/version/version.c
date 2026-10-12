/*
 * file version information: the RT_VERSION resource of a module copied into
 * a caller buffer, and VerQueryValue walking its VS_VERSIONINFO tree.
 *
 * the buffer layout is the one of windows: the resource rounded to a dword,
 * the "FE2X" signature, then as many bytes again where VerQueryValueA writes
 * the ansi copies of the strings it returns.
 */
#define MIME_BUILD_VERSION
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winver.h>

#define SIGNATURE_FE2X 0x58324546u

static WORD rd16(const BYTE *p)
{
    return (WORD)(p[0] | p[1] << 8);
}

/* the version resource of a module loaded as data, NULL with the windows error set. */
static const BYTE *find_version(HMODULE module, DWORD *size)
{
    HRSRC res = FindResourceW(module, MAKEINTRESOURCEW(VS_VERSION_INFO), MAKEINTRESOURCEW(16));
    HGLOBAL mem = res ? LoadResource(module, res) : NULL;
    const BYTE *data = mem ? LockResource(mem) : NULL;
    if (!data) {
        SetLastError(ERROR_RESOURCE_TYPE_NOT_FOUND);
        return NULL;
    }
    *size = SizeofResource(module, res);
    return data;
}

static HMODULE load_data(LPCWSTR file)
{
    UINT mode = SetErrorMode(SEM_FAILCRITICALERRORS);
    HMODULE module = LoadLibraryExW(file, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    SetErrorMode(mode);
    return module;
}

VERAPI DWORD WINAPI GetFileVersionInfoSizeExW(DWORD flags, LPCWSTR file, LPDWORD handle)
{
    if (handle)
        *handle = 0;
    HMODULE module = load_data(file);
    if (!module) {
        /* 16 bit ne files are where windows looks next; mime has none, so no resource. */
        if (GetLastError() != ERROR_FILE_NOT_FOUND)
            SetLastError(ERROR_RESOURCE_DATA_NOT_FOUND);
        return 0;
    }
    DWORD size = 0, error = 0;
    const BYTE *data = find_version(module, &size);
    if (!data) {
        error = ERROR_RESOURCE_TYPE_NOT_FOUND;
        size = 0;
    } else if (size < 0x5C || rd16(data) > size || rd16(data) > 0x8000 ||
               *(const DWORD UNALIGNED *)(data + 0x28) != VS_FFI_SIGNATURE) {
        error = ERROR_INVALID_DATA;
        size = 0;
    } else {
        size = (rd16(data) + 3u) & ~3u;
    }
    FreeLibrary(module);
    SetLastError(error);
    return size ? size * 2 + 4 : 0;
}

VERAPI DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR file, LPDWORD handle)
{
    return GetFileVersionInfoSizeExW(FILE_VER_GET_LOCALISED, file, handle);
}

VERAPI BOOL WINAPI GetFileVersionInfoExW(DWORD flags, LPCWSTR file, DWORD handle, DWORD size, LPVOID data)
{
    if (size < 0x48) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    HMODULE module = load_data(file);
    if (!module) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND)
            SetLastError(ERROR_RESOURCE_DATA_NOT_FOUND);
        return FALSE;
    }
    BOOL ok = FALSE;
    DWORD error = 0, res_size = 0;
    const BYTE *res;
    if (size < 0x5C) {
        error = ERROR_INSUFFICIENT_BUFFER;
    } else if (!(res = find_version(module, &res_size))) {
        error = ERROR_RESOURCE_TYPE_NOT_FOUND;
    } else if (res_size < 0x5C || rd16(res) > res_size || rd16(res) > 0x8000) {
        error = ERROR_INVALID_DATA;
    } else {
        DWORD n = rd16(res), room = ((size - 4) / 2) & ~3u;
        if (n > room)
            n = room;
        memcpy(data, res, n);
        DWORD aligned = (n + 3) & ~3u;
        *(DWORD UNALIGNED *)((BYTE *)data + aligned) = SIGNATURE_FE2X;
        *(WORD *)data = (WORD)aligned;
        ok = TRUE;
    }
    FreeLibrary(module);
    SetLastError(error);
    return ok;
}

VERAPI BOOL WINAPI GetFileVersionInfoW(LPCWSTR file, DWORD handle, DWORD size, LPVOID data)
{
    return GetFileVersionInfoExW(FILE_VER_GET_LOCALISED, file, handle, size, data);
}

/* one node of the tree: wLength, wValueLength, wType, the key, then the value and the children. */
typedef struct VER_NODE {
    const BYTE *Base;
    WORD Length, ValueLength, Type;
    const WCHAR *Key;
    SIZE_T KeyLength;
    const BYTE *Value, *Children, *End;
} VER_NODE;

static BOOL parse_node(const BYTE *p, const BYTE *limit, VER_NODE *n)
{
    if (limit - p < 8)
        return FALSE;
    n->Base = p;
    n->Length = rd16(p);
    n->ValueLength = rd16(p + 2);
    n->Type = rd16(p + 4);
    if (n->Length < 8 || n->Length > limit - p)
        return FALSE;
    n->End = p + n->Length;
    n->Key = (const WCHAR *)(p + 6);
    SIZE_T max = (n->Length - 6) / 2, k = 0;
    while (k < max && n->Key[k])
        k++;
    if (k == max)
        return FALSE;
    n->KeyLength = k;
    SIZE_T value = (6 + 2 * (k + 1) + 3) & ~(SIZE_T)3;
    if (value + n->ValueLength > n->Length)
        return FALSE;
    n->Value = p + value;
    SIZE_T bytes = n->Type == 1 ? 2 * (SIZE_T)n->ValueLength : n->ValueLength;
    SIZE_T children = (value + bytes + 3) & ~(SIZE_T)3;
    n->Children = p + (children < n->Length ? children : n->Length);
    return TRUE;
}

static BOOL query_value(const BYTE *block, LPCWSTR path, VER_NODE *found)
{
    /* 16 bit blocks, marked "VS" here, come from ne files mime cannot load. */
    if (rd16(block + 4) != 0)
        return FALSE;
    if ((WORD)(rd16(block) - 8) > 0x7FF8) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    VER_NODE node;
    if (!parse_node(block, block + rd16(block), &node)) {
        SetLastError(ERROR_RESOURCE_TYPE_NOT_FOUND);
        return FALSE;
    }
    for (;;) {
        while (*path == '\\')
            path++;
        if (!*path)
            break;
        SIZE_T len = 0;
        while (path[len] && path[len] != '\\')
            len++;
        const BYTE *p = node.Children;
        VER_NODE child;
        BOOL match = FALSE;
        while (p < node.End && parse_node(p, node.End, &child)) {
            if (child.KeyLength == len && !_wcsnicmp(child.Key, path, len)) {
                match = TRUE;
                break;
            }
            p += (child.Length + 3) & ~3u;
        }
        if (!match) {
            SetLastError(ERROR_RESOURCE_TYPE_NOT_FOUND);
            return FALSE;
        }
        node = child;
        path += len;
    }
    *found = node;
    return TRUE;
}

VERAPI BOOL WINAPI VerQueryValueW(LPCVOID block, LPCWSTR sub_block, LPVOID *buffer, PUINT length)
{
    if (length)
        *length = 0;
    if (!block || !sub_block || !buffer) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    VER_NODE node;
    if (!query_value(block, sub_block, &node))
        return FALSE;
    *buffer = (LPVOID)(node.ValueLength ? node.Value : (const BYTE *)(node.Key + node.KeyLength));
    if (length)
        *length = node.ValueLength;
    SetLastError(0);
    return TRUE;
}

VERAPI BOOL WINAPI VerQueryValueA(LPCVOID block, LPCSTR sub_block, LPVOID *buffer, PUINT length)
{
    if (length)
        *length = 0;
    if (!block || !sub_block || !buffer) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    int n = MultiByteToWideChar(CP_ACP, 0, sub_block, -1, NULL, 0);
    WCHAR stack[128], *path = n <= 128 ? stack : HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n * sizeof(WCHAR));
    if (!path) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    MultiByteToWideChar(CP_ACP, 0, sub_block, -1, path, n);
    VER_NODE node;
    BOOL ok = query_value(block, path, &node);
    if (path != stack)
        HeapFree(GetProcessHeap(), 0, path);
    if (!ok)
        return FALSE;
    const BYTE *base = block;
    if (node.Type != 1 || !node.ValueLength) {
        *buffer = (LPVOID)(node.ValueLength ? node.Value : (const BYTE *)(node.Key + node.KeyLength));
        if (length)
            *length = node.ValueLength;
        SetLastError(0);
        return TRUE;
    }
    /* strings go to the ansi area GetFileVersionInfo reserved after the signature, at the same offset. */
    DWORD aligned = (rd16(base) + 3u) & ~3u;
    if (*(const DWORD UNALIGNED *)(base + aligned) != SIGNATURE_FE2X) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    SIZE_T offset = (SIZE_T)(node.Value - base);
    char *dst = (char *)base + aligned + 4 + offset;
    int room = (int)(aligned - offset);
    int got = WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)node.Value, node.ValueLength, dst, room, NULL, NULL);
    if (got <= 0) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    if (dst[got - 1]) {
        if (got < room)
            dst[got++] = 0;
        else
            dst[got - 1] = 0;
    }
    *buffer = dst;
    if (length)
        *length = (UINT)got;
    SetLastError(0);
    return TRUE;
}

/* ansi entry points. */

static LPWSTR to_wide(LPCSTR s)
{
    if (!s)
        return NULL;
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    LPWSTR w = n > 0 ? HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n * sizeof(WCHAR)) : NULL;
    if (w)
        MultiByteToWideChar(CP_ACP, 0, s, -1, w, n);
    return w;
}

static void free_wide(LPWSTR w)
{
    if (w)
        HeapFree(GetProcessHeap(), 0, w);
}

VERAPI DWORD WINAPI GetFileVersionInfoSizeExA(DWORD flags, LPCSTR file, LPDWORD handle)
{
    LPWSTR w = to_wide(file);
    if (file && !w) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    DWORD r = GetFileVersionInfoSizeExW(flags, w, handle);
    free_wide(w);
    return r;
}

VERAPI DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR file, LPDWORD handle)
{
    return GetFileVersionInfoSizeExA(FILE_VER_GET_LOCALISED, file, handle);
}

VERAPI BOOL WINAPI GetFileVersionInfoExA(DWORD flags, LPCSTR file, DWORD handle, DWORD size, LPVOID data)
{
    LPWSTR w = to_wide(file);
    if (file && !w) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    BOOL r = GetFileVersionInfoExW(flags, w, handle, size, data);
    free_wide(w);
    return r;
}

VERAPI BOOL WINAPI GetFileVersionInfoA(LPCSTR file, DWORD handle, DWORD size, LPVOID data)
{
    return GetFileVersionInfoExA(FILE_VER_GET_LOCALISED, file, handle, size, data);
}

/* VerLanguageName: the localized display name of a language id, "Language Neutral" for 0. */
VERAPI DWORD WINAPI VerLanguageNameW(DWORD lang, LPWSTR buffer, DWORD size)
{
    WCHAR name[128];
    int n = 0;
    if (LOWORD(lang))
        n = GetLocaleInfoW(MAKELCID(LOWORD(lang), SORT_DEFAULT), LOCALE_SLOCALIZEDDISPLAYNAME, name, 128);
    if (n <= 0) {
        wcscpy(name, L"Language Neutral");
        n = (int)wcslen(name) + 1;
    }
    if (buffer && size) {
        DWORD copy = (DWORD)n - 1 < size - 1 ? (DWORD)n - 1 : size - 1;
        memcpy(buffer, name, copy * sizeof(WCHAR));
        buffer[copy] = 0;
    }
    return (DWORD)n - 1;
}

VERAPI DWORD WINAPI VerLanguageNameA(DWORD lang, LPSTR buffer, DWORD size)
{
    WCHAR name[128];
    DWORD n = VerLanguageNameW(lang, name, 128);
    if (buffer && size) {
        int got = WideCharToMultiByte(CP_ACP, 0, name, -1, buffer, (int)size, NULL, NULL);
        if (got <= 0)
            buffer[size - 1] = 0;
    }
    return n;
}

static BOOL file_in(LPCWSTR dir, LPCWSTR file)
{
    WCHAR path[MAX_PATH];
    if (!dir || wcslen(dir) + wcslen(file) + 2 > MAX_PATH)
        return FALSE;
    wcscpy(path, dir);
    SIZE_T n = wcslen(path);
    if (n && path[n - 1] != '\\')
        wcscat(path, L"\\");
    wcscat(path, file);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static DWORD copy_dir(LPCWSTR src, LPWSTR dst, PUINT len)
{
    SIZE_T n = src ? wcslen(src) : 0;
    UINT room = *len;
    *len = (UINT)n + 1;
    if (!dst || room < n + 1) {
        if (dst && room)
            dst[0] = 0;
        return VFF_BUFFTOOSMALL;
    }
    if (src)
        wcscpy(dst, src);
    else
        dst[0] = 0;
    return 0;
}

/*
 * VerFindFile: the directory holding the current copy of the file, searched
 * in the application then the windows and system directories, and where a
 * new copy belongs: the system directory for shared files, else the
 * application directory.
 */
VERAPI DWORD WINAPI VerFindFileW(DWORD flags, LPCWSTR file, LPCWSTR win_dir, LPCWSTR app_dir, LPWSTR cur_dir,
                                 PUINT cur_dir_len, LPWSTR dest_dir, PUINT dest_dir_len)
{
    if (!file || !cur_dir_len || !dest_dir_len)
        return VFF_BUFFTOOSMALL;
    WCHAR system[MAX_PATH], windows[MAX_PATH];
    GetSystemDirectoryW(system, MAX_PATH);
    if (!win_dir) {
        GetWindowsDirectoryW(windows, MAX_PATH);
        win_dir = windows;
    }
    LPCWSTR dest = (flags & VFFF_ISSHAREDFILE) ? system : (app_dir ? app_dir : win_dir);
    LPCWSTR cur = NULL;
    if (app_dir && file_in(app_dir, file))
        cur = app_dir;
    else if (file_in(win_dir, file))
        cur = win_dir;
    else if (file_in(system, file))
        cur = system;
    DWORD r = 0;
    if (cur && _wcsicmp(cur, dest))
        r |= VFF_CURNEDEST;
    r |= copy_dir(cur, cur_dir, cur_dir_len);
    r |= copy_dir(dest, dest_dir, dest_dir_len);
    return r;
}

VERAPI DWORD WINAPI VerFindFileA(DWORD flags, LPCSTR file, LPCSTR win_dir, LPCSTR app_dir, LPSTR cur_dir,
                                 PUINT cur_dir_len, LPSTR dest_dir, PUINT dest_dir_len)
{
    if (!cur_dir_len || !dest_dir_len)
        return VFF_BUFFTOOSMALL;
    LPWSTR wf = to_wide(file), ww = to_wide(win_dir), wa = to_wide(app_dir);
    WCHAR cur[MAX_PATH], dest[MAX_PATH];
    UINT cl = MAX_PATH, dl = MAX_PATH;
    DWORD r = VerFindFileW(flags, wf, ww, wa, cur, &cl, dest, &dl);
    free_wide(wf);
    free_wide(ww);
    free_wide(wa);
    r &= ~(DWORD)VFF_BUFFTOOSMALL;
    UINT croom = *cur_dir_len, droom = *dest_dir_len;
    int cn = WideCharToMultiByte(CP_ACP, 0, cur, -1, NULL, 0, NULL, NULL);
    int dn = WideCharToMultiByte(CP_ACP, 0, dest, -1, NULL, 0, NULL, NULL);
    *cur_dir_len = (UINT)cn;
    *dest_dir_len = (UINT)dn;
    if (!cur_dir || croom < (UINT)cn || !dest_dir || droom < (UINT)dn)
        return r | VFF_BUFFTOOSMALL;
    WideCharToMultiByte(CP_ACP, 0, cur, -1, cur_dir, (int)croom, NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, dest, -1, dest_dir, (int)droom, NULL, NULL);
    return r;
}

static void join(WCHAR *out, LPCWSTR dir, LPCWSTR file)
{
    out[0] = 0;
    if (dir && *dir) {
        lstrcpynW(out, dir, MAX_PATH);
        SIZE_T n = wcslen(out);
        if (n && out[n - 1] != '\\' && n + 1 < MAX_PATH)
            wcscat(out, L"\\");
    }
    SIZE_T n = wcslen(out);
    lstrcpynW(out + n, file, (int)(MAX_PATH - n));
}

/*
 * VerInstallFile: copies the source over the destination, without the version
 * comparisons and the compressed sources windows also handles.
 */
VERAPI DWORD WINAPI VerInstallFileW(DWORD flags, LPCWSTR src_file, LPCWSTR dest_file, LPCWSTR src_dir,
                                    LPCWSTR dest_dir, LPCWSTR cur_dir, LPWSTR tmp_file, PUINT tmp_file_len)
{
    if (!src_file || !dest_file)
        return VIF_CANNOTREADSRC;
    WCHAR src[MAX_PATH], dst[MAX_PATH];
    join(src, src_dir, src_file);
    join(dst, dest_dir, dest_file);
    if (GetFileAttributesW(src) == INVALID_FILE_ATTRIBUTES)
        return VIF_CANNOTREADSRC;
    if (!CopyFileW(src, dst, FALSE)) {
        DWORD e = GetLastError();
        if (e == ERROR_SHARING_VIOLATION)
            return VIF_SHARINGVIOLATION;
        if (e == ERROR_ACCESS_DENIED)
            return VIF_ACCESSVIOLATION;
        return VIF_CANNOTCREATE;
    }
    if (tmp_file_len)
        *tmp_file_len = 0;
    if (cur_dir && *cur_dir && !(flags & VIFF_DONTDELETEOLD) && _wcsicmp(cur_dir, dest_dir ? dest_dir : L"")) {
        WCHAR old[MAX_PATH];
        join(old, cur_dir, dest_file);
        if (!DeleteFileW(old))
            return VIF_CANNOTDELETECUR;
    }
    return 0;
}

VERAPI DWORD WINAPI VerInstallFileA(DWORD flags, LPCSTR src_file, LPCSTR dest_file, LPCSTR src_dir,
                                    LPCSTR dest_dir, LPCSTR cur_dir, LPSTR tmp_file, PUINT tmp_file_len)
{
    LPWSTR a = to_wide(src_file), b = to_wide(dest_file), c = to_wide(src_dir), d = to_wide(dest_dir),
           e = to_wide(cur_dir);
    DWORD r = VerInstallFileW(flags, a, b, c, d, e, NULL, NULL);
    if (tmp_file_len)
        *tmp_file_len = 0;
    free_wide(a);
    free_wide(b);
    free_wide(c);
    free_wide(d);
    free_wide(e);
    return r;
}
