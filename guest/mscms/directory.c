#include "mscms_private.h"

#define LCS_sRGB 0x73524742
#define LCS_WINDOWS_COLOR_SPACE 0x57696E20
#define CLASS_MONITOR 0x6D6E7472

static const WCHAR default_profile[] = L"sRGB Color Space Profile.icm";
static const WCHAR icm_key[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ICM";

enum { SCOPE_SYSTEM = 0, SCOPE_USER = 1 };

/* ------------------------------------------------------------------ the color directory */

BOOL ColorDirectory(WCHAR *path, DWORD count)
{
    static const WCHAR tail[] = L"\\spool\\drivers\\color";
    UINT n = GetSystemDirectoryW(path, count);
    if (!n || n + sizeof(tail) / sizeof(WCHAR) > count)
        return FALSE;
    memcpy(path + n, tail, sizeof(tail));
    return TRUE;
}

BOOL ColorPath(const WCHAR *name, WCHAR *path, DWORD count)
{
    if (wcschr(name, L'\\') || wcschr(name, L'/') || wcschr(name, L':')) {
        if (wcslen(name) >= count)
            return FALSE;
        wcscpy(path, name);
        return TRUE;
    }
    if (!ColorDirectory(path, count))
        return FALSE;
    size_t n = wcslen(path);
    if (n + 1 + wcslen(name) >= count)
        return FALSE;
    path[n] = L'\\';
    wcscpy(path + n + 1, name);
    return TRUE;
}

/* the file part of a path. */
static const WCHAR *file_part(const WCHAR *path)
{
    const WCHAR *a = wcsrchr(path, L'\\'), *b = wcsrchr(path, L'/');
    const WCHAR *last = a > b ? a : b;
    return last ? last + 1 : path;
}

BOOL WINAPI GetColorDirectoryW(const WCHAR *machine, WCHAR *buffer, DWORD *size)
{
    if (machine || !size) {
        SetLastError(machine ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR path[MAX_PATH];
    if (!ColorDirectory(path, MAX_PATH))
        return FALSE;
    DWORD needed = (DWORD)(wcslen(path) + 1) * sizeof(WCHAR);
    if (!buffer || *size < needed) {
        *size = needed;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(buffer, path, needed);
    *size = needed;
    return TRUE;
}

BOOL WINAPI GetColorDirectoryA(const char *machine, char *buffer, DWORD *size)
{
    if (machine || !size) {
        SetLastError(machine ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR path[MAX_PATH];
    if (!ColorDirectory(path, MAX_PATH))
        return FALSE;
    return AnsiOut(path, buffer, size);
}

/* ------------------------------------------------------------------ the registry */

/* the key of the associations of a device, its name with the backslashes it may have turned to #. */
static void device_key(const WCHAR *device, WCHAR *key, DWORD count)
{
    static const WCHAR prefix[] = L"\\ProfileAssociations\\";
    size_t at = wcslen(icm_key);
    memcpy(key, icm_key, at * sizeof(WCHAR));
    memcpy(key + at, prefix, sizeof(prefix));
    at += sizeof(prefix) / sizeof(WCHAR) - 1;
    for (; *device && at + 1 < count; device++)
        key[at++] = *device == L'\\' ? L'#' : *device;
    key[at] = 0;
}

static HKEY scope_root(DWORD scope) { return scope == SCOPE_USER ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE; }

/* the profiles associated with a device, a list of names ending with an empty one, from the heap. */
static WCHAR *associations(DWORD scope, const WCHAR *device)
{
    WCHAR key[512];
    device_key(device, key, 512);
    DWORD size = 0;
    if (RegGetValueW(scope_root(scope), key, L"ICMProfile", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size) || size < 2)
        return nullptr;
    WCHAR *list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size + 2 * sizeof(WCHAR));
    if (list && RegGetValueW(scope_root(scope), key, L"ICMProfile", RRF_RT_REG_MULTI_SZ, nullptr, list, &size)) {
        HeapFree(GetProcessHeap(), 0, list);
        return nullptr;
    }
    if (list && !list[0]) {
        HeapFree(GetProcessHeap(), 0, list);
        return nullptr;
    }
    return list;
}

static DWORD list_bytes(const WCHAR *list)
{
    const WCHAR *p = list;
    while (*p)
        p += wcslen(p) + 1;
    return (DWORD)(p - list + 1) * sizeof(WCHAR);
}

/* adds a name to the list of a device, first when it is the default, or takes it out. */
static LSTATUS set_association(DWORD scope, const WCHAR *device, const WCHAR *profile, BOOL add, BOOL first)
{
    WCHAR *old = associations(scope, device);
    size_t extra = wcslen(profile) + 2;
    DWORD old_bytes = old ? list_bytes(old) : sizeof(WCHAR);
    WCHAR *list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, old_bytes + extra * sizeof(WCHAR));
    if (!list) {
        HeapFree(GetProcessHeap(), 0, old);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    WCHAR *at = list;
    BOOL found = FALSE;
    if (add && first) {
        wcscpy(at, profile);
        at += wcslen(profile) + 1;
    }
    for (const WCHAR *p = old; p && *p; p += wcslen(p) + 1) {
        if (!_wcsicmp(p, profile)) {
            found = TRUE;
            if (!(add && !first))
                continue;
        }
        wcscpy(at, p);
        at += wcslen(p) + 1;
    }
    if (add && !first && !found) {
        wcscpy(at, profile);
        at += wcslen(profile) + 1;
    }
    *at++ = 0;
    HeapFree(GetProcessHeap(), 0, old);
    if (!add && !found) {
        HeapFree(GetProcessHeap(), 0, list);
        return ERROR_PROFILE_NOT_ASSOCIATED_WITH_DEVICE;
    }
    WCHAR key[512];
    device_key(device, key, 512);
    LSTATUS s = RegSetKeyValueW(scope_root(scope), key, L"ICMProfile", REG_MULTI_SZ, list, (DWORD)(at - list) * sizeof(WCHAR));
    HeapFree(GetProcessHeap(), 0, list);
    return s;
}

static DWORD get_dword(HKEY root, const WCHAR *sub, const WCHAR *value, DWORD fallback)
{
    WCHAR key[512];
    wcscpy(key, icm_key);
    if (sub)
        wcscat(key, sub);
    DWORD v, size = sizeof(v);
    return RegGetValueW(root, key, value, RRF_RT_REG_DWORD, nullptr, &v, &size) ? fallback : v;
}

static BOOL set_dword(HKEY root, const WCHAR *sub, const WCHAR *value, DWORD v)
{
    WCHAR key[512];
    wcscpy(key, icm_key);
    if (sub)
        wcscat(key, sub);
    LSTATUS s = RegSetKeyValueW(root, key, value, REG_DWORD, &v, sizeof(v));
    if (s)
        SetLastError(s);
    return !s;
}

static BOOL per_user(const WCHAR *device)
{
    if (!device)
        return FALSE;
    WCHAR key[512];
    device_key(device, key, 512);
    DWORD v, size = sizeof(v);
    return !RegGetValueW(HKEY_CURRENT_USER, key, L"UsePerUserProfiles", RRF_RT_REG_DWORD, nullptr, &v, &size) && v;
}

/* ------------------------------------------------------------------ installing */

static BOOL install(const WCHAR *machine, const WCHAR *path)
{
    if (machine) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (!path) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR target[MAX_PATH];
    if (!ColorPath(file_part(path), target, MAX_PATH)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!_wcsicmp(target, path))
        return TRUE;
    return CopyFileW(path, target, FALSE);
}

BOOL WINAPI InstallColorProfileW(const WCHAR *machine, const WCHAR *path) { return install(machine, path); }

BOOL WINAPI InstallColorProfileA(const char *machine, const char *path)
{
    WCHAR *p = WideFromAnsi(path);
    BOOL ok = install(machine ? L"" : nullptr, p);
    HeapFree(GetProcessHeap(), 0, p);
    return ok;
}

static BOOL uninstall(const WCHAR *machine, const WCHAR *name, BOOL remove)
{
    if (machine) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    WCHAR path[MAX_PATH];
    if (!name || !ColorPath(file_part(name), path, MAX_PATH)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }
    return remove ? DeleteFileW(path) : TRUE;
}

BOOL WINAPI UninstallColorProfileW(const WCHAR *machine, const WCHAR *name, BOOL remove)
{
    return uninstall(machine, name, remove);
}

BOOL WINAPI UninstallColorProfileA(const char *machine, const char *name, BOOL remove)
{
    WCHAR *n = WideFromAnsi(name);
    BOOL ok = uninstall(machine ? L"" : nullptr, n, remove);
    HeapFree(GetProcessHeap(), 0, n);
    return ok;
}

/* ------------------------------------------------------------------ associations */

static BOOL associate(DWORD scope, const WCHAR *profile, const WCHAR *device, BOOL add)
{
    if (!profile || !device) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR path[MAX_PATH];
    if (add && (!ColorPath(file_part(profile), path, MAX_PATH) || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)) {
        SetLastError(ERROR_PROFILE_NOT_FOUND);
        return FALSE;
    }
    LSTATUS s = set_association(scope, device, file_part(profile), add, FALSE);
    if (s) {
        SetLastError(s);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI AssociateColorProfileWithDeviceW(const WCHAR *machine, const WCHAR *profile, const WCHAR *device)
{
    if (machine) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    return associate(SCOPE_SYSTEM, profile, device, TRUE);
}

BOOL WINAPI DisassociateColorProfileFromDeviceW(const WCHAR *machine, const WCHAR *profile, const WCHAR *device)
{
    if (machine) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    return associate(SCOPE_SYSTEM, profile, device, FALSE);
}

BOOL WINAPI AssociateColorProfileWithDeviceA(const char *machine, const char *profile, const char *device)
{
    if (machine) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    WCHAR *p = WideFromAnsi(profile), *d = WideFromAnsi(device);
    BOOL ok = associate(SCOPE_SYSTEM, p, d, TRUE);
    HeapFree(GetProcessHeap(), 0, p);
    HeapFree(GetProcessHeap(), 0, d);
    return ok;
}

BOOL WINAPI DisassociateColorProfileFromDeviceA(const char *machine, const char *profile, const char *device)
{
    if (machine) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    WCHAR *p = WideFromAnsi(profile), *d = WideFromAnsi(device);
    BOOL ok = associate(SCOPE_SYSTEM, p, d, FALSE);
    HeapFree(GetProcessHeap(), 0, p);
    HeapFree(GetProcessHeap(), 0, d);
    return ok;
}

BOOL WINAPI WcsAssociateColorProfileWithDevice(DWORD scope, const WCHAR *profile, const WCHAR *device)
{
    return associate(scope, profile, device, TRUE);
}

BOOL WINAPI WcsDisassociateColorProfileFromDevice(DWORD scope, const WCHAR *profile, const WCHAR *device)
{
    return associate(scope, profile, device, FALSE);
}

/* ------------------------------------------------------------------ enumerating */

typedef struct ENUMTYPEW {
    DWORD dwSize, dwVersion, dwFields;
    const WCHAR *pDeviceName;
    DWORD dwMediaType, dwDitheringMode, dwResolution[2], dwCMMType, dwClass, dwDataColorSpace, dwConnectionSpace,
        dwSignature, dwPlatform, dwProfileFlags, dwManufacturer, dwModel, dwAttributes[2], dwRenderingIntent, dwCreator,
        dwDeviceClass;
} ENUMTYPEW;

typedef struct ENUMTYPEA {
    DWORD dwSize, dwVersion, dwFields;
    const char *pDeviceName;
    DWORD dwMediaType, dwDitheringMode, dwResolution[2], dwCMMType, dwClass, dwDataColorSpace, dwConnectionSpace,
        dwSignature, dwPlatform, dwProfileFlags, dwManufacturer, dwModel, dwAttributes[2], dwRenderingIntent, dwCreator,
        dwDeviceClass;
} ENUMTYPEA;

#define ET_DEVICENAME 0x00000001
#define ET_CMMTYPE 0x00000010
#define ET_CLASS 0x00000020
#define ET_DATACOLORSPACE 0x00000040
#define ET_CONNECTIONSPACE 0x00000080
#define ET_SIGNATURE 0x00000100
#define ET_PLATFORM 0x00000200
#define ET_PROFILEFLAGS 0x00000400
#define ET_MANUFACTURER 0x00000800
#define ET_MODEL 0x00001000
#define ET_ATTRIBUTES 0x00002000
#define ET_RENDERINGINTENT 0x00004000
#define ET_CREATOR 0x00008000

/* whether the header of a profile file fits what the enumeration asks for. */
static BOOL matches(const WCHAR *name, const ENUMTYPEW *e)
{
    WCHAR path[MAX_PATH];
    if (!ColorPath(name, path, MAX_PATH))
        return FALSE;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    BYTE h[128];
    DWORD got = 0;
    BOOL ok = ReadFile(f, h, sizeof(h), &got, nullptr) && got == sizeof(h) && icc_be32(h + 36) == ICC_SIG('a', 'c', 's', 'p');
    CloseHandle(f);
    if (!ok)
        return FALSE;
    DWORD fields = e ? e->dwFields : 0;
    const struct {
        DWORD flag, want;
        int at;
    } checks[] = {
        {ET_CMMTYPE, e ? e->dwCMMType : 0, 4},          {ET_CLASS, e ? e->dwClass : 0, 12},
        {ET_DATACOLORSPACE, e ? e->dwDataColorSpace : 0, 16}, {ET_CONNECTIONSPACE, e ? e->dwConnectionSpace : 0, 20},
        {ET_SIGNATURE, e ? e->dwSignature : 0, 36},     {ET_PLATFORM, e ? e->dwPlatform : 0, 40},
        {ET_PROFILEFLAGS, e ? e->dwProfileFlags : 0, 44}, {ET_MANUFACTURER, e ? e->dwManufacturer : 0, 48},
        {ET_MODEL, e ? e->dwModel : 0, 52},             {ET_RENDERINGINTENT, e ? e->dwRenderingIntent : 0, 64},
        {ET_CREATOR, e ? e->dwCreator : 0, 80},
    };
    for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); i++)
        if ((fields & checks[i].flag) && icc_be32(h + checks[i].at) != checks[i].want)
            return FALSE;
    if ((fields & ET_ATTRIBUTES) && (icc_be32(h + 56) != e->dwAttributes[0] || icc_be32(h + 60) != e->dwAttributes[1]))
        return FALSE;
    return TRUE;
}

static BOOL profile_extension(const WCHAR *name)
{
    const WCHAR *dot = wcsrchr(name, L'.');
    return dot && (!_wcsicmp(dot, L".icm") || !_wcsicmp(dot, L".icc"));
}

/*
 * the names of the profiles that match, as a list ending with an empty name, into a heap buffer: those of the device
 * in their order when one is named, else every profile of the color directory.
 */
static WCHAR *enumerate(DWORD scope, const ENUMTYPEW *e, DWORD *bytes, DWORD *count)
{
    SIZE_T cap = 1024, used = 0;
    WCHAR *out = HeapAlloc(GetProcessHeap(), 0, cap * sizeof(WCHAR));
    if (!out)
        return nullptr;
    *count = 0;
    WCHAR *device_list = nullptr;
    if (e && (e->dwFields & ET_DEVICENAME) && e->pDeviceName) {
        device_list = associations(per_user(e->pDeviceName) ? SCOPE_USER : scope, e->pDeviceName);
        if (!device_list && e->dwClass == CLASS_MONITOR) {
            /* a display with nothing associated shows the default profile. */
            device_list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(default_profile) + sizeof(WCHAR));
            if (device_list)
                memcpy(device_list, default_profile, sizeof(default_profile));
        }
    }
    WIN32_FIND_DATAW fd;
    HANDLE find = INVALID_HANDLE_VALUE;
    WCHAR pattern[MAX_PATH];
    if (!device_list && ColorPath(L"*", pattern, MAX_PATH))
        find = FindFirstFileW(pattern, &fd);
    const WCHAR *next = device_list;
    for (;;) {
        const WCHAR *name;
        if (device_list) {
            if (!next || !*next)
                break;
            name = next;
            next += wcslen(next) + 1;
        } else {
            if (find == INVALID_HANDLE_VALUE)
                break;
            name = fd.cFileName;
        }
        if ((device_list || (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && profile_extension(name))) && matches(name, e)) {
            size_t n = wcslen(name) + 1;
            if (used + n + 1 > cap) {
                cap = (used + n + 1) * 2;
                WCHAR *grown = HeapReAlloc(GetProcessHeap(), 0, out, cap * sizeof(WCHAR));
                if (!grown)
                    break;
                out = grown;
            }
            memcpy(out + used, name, n * sizeof(WCHAR));
            used += n;
            (*count)++;
        }
        if (!device_list && !FindNextFileW(find, &fd)) {
            FindClose(find);
            find = INVALID_HANDLE_VALUE;
        }
    }
    if (find != INVALID_HANDLE_VALUE)
        FindClose(find);
    HeapFree(GetProcessHeap(), 0, device_list);
    out[used++] = 0;
    *bytes = (DWORD)used * sizeof(WCHAR);
    return out;
}

static BOOL enum_out(WCHAR *list, DWORD bytes, BYTE *buffer, DWORD *size)
{
    BOOL ok = buffer && *size >= bytes;
    if (ok)
        memcpy(buffer, list, bytes);
    else
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
    *size = bytes;
    HeapFree(GetProcessHeap(), 0, list);
    return ok;
}

BOOL WINAPI EnumColorProfilesW(const WCHAR *machine, ENUMTYPEW *e, BYTE *buffer, DWORD *size, DWORD *count)
{
    if (machine) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (!size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD bytes, n;
    WCHAR *list = enumerate(SCOPE_SYSTEM, e, &bytes, &n);
    if (!list) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    if (count)
        *count = n;
    return enum_out(list, bytes, buffer, size);
}

BOOL WINAPI EnumColorProfilesA(const char *machine, ENUMTYPEA *e, BYTE *buffer, DWORD *size, DWORD *count)
{
    if (machine) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (!size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ENUMTYPEW w;
    WCHAR *device = nullptr;
    if (e) {
        memcpy(&w, e, sizeof(w));
        device = WideFromAnsi(e->pDeviceName);
        w.pDeviceName = device;
    }
    DWORD bytes, n;
    WCHAR *list = enumerate(SCOPE_SYSTEM, e ? &w : nullptr, &bytes, &n);
    HeapFree(GetProcessHeap(), 0, device);
    if (!list) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    if (count)
        *count = n;
    int needed = WideCharToMultiByte(CP_ACP, 0, list, bytes / sizeof(WCHAR), nullptr, 0, nullptr, nullptr);
    BOOL ok = buffer && *size >= (DWORD)needed;
    if (ok)
        WideCharToMultiByte(CP_ACP, 0, list, bytes / sizeof(WCHAR), (char *)buffer, needed, nullptr, nullptr);
    else
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
    *size = needed;
    HeapFree(GetProcessHeap(), 0, list);
    return ok;
}

BOOL WINAPI WcsEnumColorProfilesSize(DWORD scope, ENUMTYPEW *e, DWORD *size)
{
    if (!e || !size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD bytes, n;
    WCHAR *list = enumerate(scope, e, &bytes, &n);
    if (!list) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    HeapFree(GetProcessHeap(), 0, list);
    *size = bytes;
    return TRUE;
}

BOOL WINAPI WcsEnumColorProfiles(DWORD scope, ENUMTYPEW *e, BYTE *buffer, DWORD size, DWORD *needed)
{
    if (!e || !buffer) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD bytes, n;
    WCHAR *list = enumerate(scope, e, &bytes, &n);
    if (!list) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    if (needed)
        *needed = bytes;
    return enum_out(list, bytes, buffer, &size);
}

/* ------------------------------------------------------------------ standard color spaces */

static BOOL standard_profile(DWORD space, WCHAR *path, DWORD count)
{
    if (space != LCS_sRGB && space != LCS_WINDOWS_COLOR_SPACE) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }
    WCHAR name[5] = {(WCHAR)(space >> 24), (WCHAR)(space >> 16 & 255), (WCHAR)(space >> 8 & 255), (WCHAR)(space & 255), 0};
    WCHAR key[512], set[MAX_PATH];
    wcscpy(key, icm_key);
    wcscat(key, L"\\StandardColorSpaces");
    DWORD size = sizeof(set);
    if (!RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_SZ, nullptr, set, &size))
        return ColorPath(set, path, count);
    return ColorPath(default_profile, path, count);
}

BOOL WINAPI GetStandardColorSpaceProfileW(const WCHAR *machine, DWORD space, WCHAR *buffer, DWORD *size)
{
    if (machine) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (!size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR path[MAX_PATH];
    if (!standard_profile(space, path, MAX_PATH))
        return FALSE;
    DWORD needed = (DWORD)(wcslen(path) + 1) * sizeof(WCHAR);
    if (!buffer || *size < needed) {
        *size = needed;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(buffer, path, needed);
    *size = needed;
    return TRUE;
}

BOOL WINAPI GetStandardColorSpaceProfileA(const char *machine, DWORD space, char *buffer, DWORD *size)
{
    if (machine) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (!size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR path[MAX_PATH];
    if (!standard_profile(space, path, MAX_PATH))
        return FALSE;
    return AnsiOut(path, buffer, size);
}

static BOOL set_standard(DWORD space, const WCHAR *profile)
{
    if (!profile) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR name[5] = {(WCHAR)(space >> 24), (WCHAR)(space >> 16 & 255), (WCHAR)(space >> 8 & 255), (WCHAR)(space & 255), 0};
    WCHAR key[512];
    wcscpy(key, icm_key);
    wcscat(key, L"\\StandardColorSpaces");
    LSTATUS s = RegSetKeyValueW(HKEY_LOCAL_MACHINE, key, name, REG_SZ, profile, (DWORD)(wcslen(profile) + 1) * sizeof(WCHAR));
    if (s)
        SetLastError(s);
    return !s;
}

BOOL WINAPI SetStandardColorSpaceProfileW(const WCHAR *machine, DWORD space, const WCHAR *profile)
{
    if (machine) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    return set_standard(space, profile);
}

BOOL WINAPI SetStandardColorSpaceProfileA(const char *machine, DWORD space, const char *profile)
{
    if (machine) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    WCHAR *p = WideFromAnsi(profile);
    BOOL ok = set_standard(space, p);
    HeapFree(GetProcessHeap(), 0, p);
    return ok;
}

/* ------------------------------------------------------------------ wcs defaults */

enum { CPT_ICC = 0, CPT_DMP, CPT_CAMP, CPT_GMMP };

/* the default profile of a device, or of the system when there is no device: only icc profiles are here. */
static BOOL default_profile_of(DWORD scope, const WCHAR *device, DWORD type, DWORD subtype, WCHAR *name, DWORD count)
{
    if (type > CPT_GMMP || subtype > 8) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (type != CPT_ICC) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }
    if (device) {
        WCHAR *list = associations(scope, device);
        if (!list && scope == SCOPE_USER)
            list = associations(SCOPE_SYSTEM, device);
        if (list) {
            BOOL fits = wcslen(list) < count;
            if (fits)
                wcscpy(name, list);
            HeapFree(GetProcessHeap(), 0, list);
            return fits;
        }
    } else {
        WCHAR key[512];
        wcscpy(key, icm_key);
        wcscat(key, L"\\Defaults");
        WCHAR value[16] = L"ICC-0";
        value[4] = (WCHAR)(L'0' + subtype);
        DWORD size = count * sizeof(WCHAR);
        if (!RegGetValueW(scope_root(scope), key, value, RRF_RT_REG_SZ, nullptr, name, &size))
            return TRUE;
    }
    wcscpy(name, default_profile);
    return TRUE;
}

BOOL WINAPI WcsGetDefaultColorProfileSize(DWORD scope, const WCHAR *device, DWORD type, DWORD subtype, DWORD id, DWORD *size)
{
    WCHAR name[MAX_PATH];
    if (!size) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!default_profile_of(scope, device, type, subtype, name, MAX_PATH))
        return FALSE;
    *size = (DWORD)(wcslen(name) + 1) * sizeof(WCHAR);
    return TRUE;
}

BOOL WINAPI WcsGetDefaultColorProfile(DWORD scope, const WCHAR *device, DWORD type, DWORD subtype, DWORD id, DWORD size,
                                      WCHAR *buffer)
{
    WCHAR name[MAX_PATH];
    if (!buffer) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!default_profile_of(scope, device, type, subtype, name, MAX_PATH))
        return FALSE;
    DWORD needed = (DWORD)(wcslen(name) + 1) * sizeof(WCHAR);
    if (size < needed) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(buffer, name, needed);
    return TRUE;
}

BOOL WINAPI WcsSetDefaultColorProfile(DWORD scope, const WCHAR *device, DWORD type, DWORD subtype, DWORD id,
                                      const WCHAR *profile)
{
    if (type > CPT_GMMP || subtype > 8) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (device) {
        if (!profile) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        LSTATUS s = set_association(scope, device, file_part(profile), TRUE, TRUE);
        if (s)
            SetLastError(s);
        return !s;
    }
    WCHAR key[512];
    wcscpy(key, icm_key);
    wcscat(key, L"\\Defaults");
    WCHAR value[16] = L"ICC-0";
    value[0] = type == CPT_ICC ? L'I' : type == CPT_DMP ? L'D' : type == CPT_CAMP ? L'C' : L'G';
    value[4] = (WCHAR)(L'0' + subtype);
    LSTATUS s;
    if (profile)
        s = RegSetKeyValueW(scope_root(scope), key, value, REG_SZ, file_part(profile),
                            (DWORD)(wcslen(file_part(profile)) + 1) * sizeof(WCHAR));
    else
        s = RegDeleteKeyValueW(scope_root(scope), key, value);
    if (s && s != ERROR_FILE_NOT_FOUND) {
        SetLastError(s);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI WcsGetUsePerUserProfiles(const WCHAR *device, DWORD device_class, BOOL *use)
{
    if (!device || !use) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *use = per_user(device);
    return TRUE;
}

BOOL WINAPI WcsSetUsePerUserProfiles(const WCHAR *device, DWORD device_class, BOOL use)
{
    if (!device) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR key[512];
    device_key(device, key, 512);
    DWORD v = use != 0;
    LSTATUS s = RegSetKeyValueW(HKEY_CURRENT_USER, key, L"UsePerUserProfiles", REG_DWORD, &v, sizeof(v));
    if (s)
        SetLastError(s);
    return !s;
}

BOOL WINAPI WcsGetDefaultRenderingIntent(DWORD scope, DWORD *intent)
{
    if (!intent) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *intent = get_dword(scope_root(scope), nullptr, L"RenderingIntent", ICC_INTENT_PERCEPTUAL);
    return TRUE;
}

BOOL WINAPI WcsSetDefaultRenderingIntent(DWORD scope, DWORD intent)
{
    if (intent > 3) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return set_dword(scope_root(scope), nullptr, L"RenderingIntent", intent);
}

BOOL WINAPI WcsGetCalibrationManagementState(BOOL *state)
{
    if (!state) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *state = get_dword(HKEY_LOCAL_MACHINE, L"\\Calibration", L"CalibrationManagementEnabled", 0) != 0;
    return TRUE;
}

BOOL WINAPI WcsSetCalibrationManagementState(BOOL state)
{
    return set_dword(HKEY_LOCAL_MACHINE, L"\\Calibration", L"CalibrationManagementEnabled", state != 0);
}

/* ------------------------------------------------------------------ displays, by adapter and source */

/* a display is named after its source, the adapter being the one mime has. */
static void display_name(UINT32 source, WCHAR *name)
{
    static const WCHAR prefix[] = L"DISPLAY";
    memcpy(name, prefix, sizeof(prefix));
    WCHAR digits[12];
    int n = 0;
    UINT32 v = source + 1;
    do {
        digits[n++] = (WCHAR)(L'0' + v % 10);
        v /= 10;
    } while (v);
    WCHAR *at = name + 7;
    while (n)
        *at++ = digits[--n];
    *at = 0;
}

static HRESULT hresult_last(void)
{
    DWORD e = GetLastError();
    return e ? HRESULT_FROM_WIN32(e) : E_FAIL;
}

HRESULT WINAPI ColorProfileAddDisplayAssociation(DWORD scope, const WCHAR *profile, LUID adapter, UINT32 source,
                                                 BOOL set_default, BOOL advanced)
{
    if (!profile)
        return E_INVALIDARG;
    WCHAR device[24];
    display_name(source, device);
    WCHAR path[MAX_PATH];
    if (!ColorPath(file_part(profile), path, MAX_PATH) || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        return HRESULT_FROM_WIN32(ERROR_PROFILE_NOT_FOUND);
    LSTATUS s = set_association(scope, device, file_part(profile), TRUE, set_default);
    return s ? HRESULT_FROM_WIN32(s) : S_OK;
}

HRESULT WINAPI ColorProfileRemoveDisplayAssociation(DWORD scope, const WCHAR *profile, LUID adapter, UINT32 source,
                                                    BOOL advanced)
{
    if (!profile)
        return E_INVALIDARG;
    WCHAR device[24];
    display_name(source, device);
    LSTATUS s = set_association(scope, device, file_part(profile), FALSE, FALSE);
    return s ? HRESULT_FROM_WIN32(s) : S_OK;
}

HRESULT WINAPI ColorProfileSetDisplayDefaultAssociation(DWORD scope, const WCHAR *profile, DWORD type, DWORD subtype,
                                                        LUID adapter, UINT32 source)
{
    if (!profile || type != CPT_ICC)
        return E_INVALIDARG;
    WCHAR device[24];
    display_name(source, device);
    LSTATUS s = set_association(scope, device, file_part(profile), TRUE, TRUE);
    return s ? HRESULT_FROM_WIN32(s) : S_OK;
}

/* the name of the default profile of a display, from LocalAlloc. */
HRESULT WINAPI ColorProfileGetDisplayDefault(DWORD scope, LUID adapter, UINT32 source, DWORD type, DWORD subtype,
                                             WCHAR **profile)
{
    if (!profile)
        return E_INVALIDARG;
    *profile = nullptr;
    WCHAR device[24], name[MAX_PATH];
    display_name(source, device);
    if (!default_profile_of(scope, device, type, subtype, name, MAX_PATH))
        return hresult_last();
    SIZE_T bytes = (wcslen(name) + 1) * sizeof(WCHAR);
    *profile = LocalAlloc(LMEM_FIXED, bytes);
    if (!*profile)
        return E_OUTOFMEMORY;
    memcpy(*profile, name, bytes);
    return S_OK;
}

/* the profiles associated with a display: an array of pointers followed by the names, all in one LocalAlloc. */
HRESULT WINAPI ColorProfileGetDisplayList(DWORD scope, LUID adapter, UINT32 source, WCHAR ***list, DWORD *count)
{
    if (!list || !count)
        return E_INVALIDARG;
    *list = nullptr;
    *count = 0;
    WCHAR device[24];
    display_name(source, device);
    WCHAR *names = associations(scope, device);
    if (!names)
        return S_OK;
    DWORD n = 0;
    for (const WCHAR *p = names; *p; p += wcslen(p) + 1)
        n++;
    DWORD bytes = list_bytes(names);
    WCHAR **block = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, n * sizeof(WCHAR *) + bytes);
    if (!block) {
        HeapFree(GetProcessHeap(), 0, names);
        return E_OUTOFMEMORY;
    }
    WCHAR *text = (WCHAR *)(block + n);
    memcpy(text, names, bytes);
    HeapFree(GetProcessHeap(), 0, names);
    for (DWORD i = 0; i < n; i++) {
        block[i] = text;
        text += wcslen(text) + 1;
    }
    *list = block;
    *count = n;
    return S_OK;
}

HRESULT WINAPI ColorProfileGetDisplayUserScope(LUID adapter, UINT32 source, DWORD *scope)
{
    if (!scope)
        return E_INVALIDARG;
    WCHAR device[24];
    display_name(source, device);
    *scope = per_user(device) ? SCOPE_USER : SCOPE_SYSTEM;
    return S_OK;
}

/* the display has no hardware color pipeline of its own: no gamma table and no microsoft hardware color. */
HRESULT WINAPI ColorProfileGetDeviceCapabilities(DWORD scope, LUID adapter, UINT32 source, DWORD type, void *out)
{
    if (!out)
        return E_INVALIDARG;
    switch (type) {
    case 1: {
        struct {
            UINT32 size;
            BOOL supports_vcgt;
        } *caps = out;
        caps->size = sizeof(*caps);
        caps->supports_vcgt = FALSE;
        return S_OK;
    }
    case 2: {
        struct {
            UINT32 size;
            BOOL supports_mhc2;
            UINT32 regamma_entries, rows, columns;
        } *caps = out;
        caps->size = sizeof(*caps);
        caps->supports_mhc2 = FALSE;
        caps->regamma_entries = caps->rows = caps->columns = 0;
        return S_OK;
    }
    }
    return E_INVALIDARG;
}
