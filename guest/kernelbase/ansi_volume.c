/* the ansi forms of volume and file calls that kernelbase has in the wide form: they convert, call and convert back. */
#include "kernelbase_private.h"

WINBASEAPI BOOL WINAPI GetVolumeNameForVolumeMountPointW(LPCWSTR mount_point, LPWSTR name, DWORD length);
WINBASEAPI BOOL WINAPI GetVolumePathNamesForVolumeNameW(LPCWSTR volume, LPWSTR names, DWORD length, PDWORD returned);
WINBASEAPI HANDLE WINAPI FindFirstVolumeW(LPWSTR name, DWORD length);
WINBASEAPI BOOL WINAPI FindNextVolumeW(HANDLE handle, LPWSTR name, DWORD length);
WINBASEAPI BOOL WINAPI FindVolumeClose(HANDLE handle);

/* text that fits the buffer of the caller, with the error windows gives when it does not. */
static BOOL put_ansi(PCWSTR wide, LPSTR out, DWORD size)
{
    int need = WideCharToMultiByte(KbFileApiCodePage(), 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (!need)
        return FALSE;
    if ((DWORD)need > size) {
        KbSetError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    return WideCharToMultiByte(KbFileApiCodePage(), 0, wide, -1, out, (int)size, nullptr, nullptr) != 0;
}

WINBASEAPI BOOL WINAPI GetVolumePathNameA(LPCSTR path, LPSTR volume, DWORD size)
{
    if (!path || !volume || !size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR wide = KbAnsiToWide(path);
    if (!wide)
        return FALSE;
    WCHAR root[MAX_PATH * 2];
    BOOL ok = GetVolumePathNameW(wide, root, RTL_NUMBER_OF(root));
    KbFree(wide);
    return ok && put_ansi(root, volume, size);
}

WINBASEAPI BOOL WINAPI GetVolumeNameForVolumeMountPointA(LPCSTR mount_point, LPSTR name, DWORD length)
{
    if (!mount_point || !name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR wide = KbAnsiToWide(mount_point);
    if (!wide)
        return FALSE;
    WCHAR volume[64];
    BOOL ok = GetVolumeNameForVolumeMountPointW(wide, volume, RTL_NUMBER_OF(volume));
    KbFree(wide);
    return ok && put_ansi(volume, name, length);
}

WINBASEAPI BOOL WINAPI GetVolumePathNamesForVolumeNameA(LPCSTR volume, LPSTR names, DWORD length, PDWORD returned)
{
    PWSTR wide = volume ? KbAnsiToWide(volume) : nullptr;
    if (volume && !wide)
        return FALSE;
    WCHAR list[MAX_PATH];
    DWORD got = 0;
    BOOL ok = GetVolumePathNamesForVolumeNameW(wide, list, RTL_NUMBER_OF(list), &got);
    KbFree(wide);
    if (!ok) {
        if (returned)
            *returned = got;
        return FALSE;
    }
    /* the multi string converted a name at a time. */
    DWORD used = 0;
    BOOL fits = TRUE;
    for (const WCHAR *name = list; *name; name += wcslen(name) + 1) {
        int bytes = WideCharToMultiByte(KbFileApiCodePage(), 0, name, -1, nullptr, 0, nullptr, nullptr);
        if (names && used + (DWORD)bytes + 1 <= length)
            WideCharToMultiByte(KbFileApiCodePage(), 0, name, -1, names + used, bytes, nullptr, nullptr);
        else
            fits = FALSE;
        used += (DWORD)bytes;
    }
    used++;
    if (returned)
        *returned = used;
    if (!fits || !names || used > length) {
        KbSetError(ERROR_MORE_DATA);
        return FALSE;
    }
    names[used - 1] = 0;
    return TRUE;
}


WINBASEAPI HANDLE WINAPI FindFirstVolumeA(LPSTR name, DWORD length)
{
    WCHAR volume[64];
    HANDLE wide = FindFirstVolumeW(volume, RTL_NUMBER_OF(volume));
    if (wide == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    if (!put_ansi(volume, name, length)) {
        FindVolumeClose(wide);
        return INVALID_HANDLE_VALUE;
    }
    return wide;
}

WINBASEAPI BOOL WINAPI FindNextVolumeA(HANDLE handle, LPSTR name, DWORD length)
{
    WCHAR volume[64];
    return FindNextVolumeW(handle, volume, RTL_NUMBER_OF(volume)) && put_ansi(volume, name, length);
}

WINBASEAPI BOOL WINAPI CopyFileExA(LPCSTR source, LPCSTR destination, LPPROGRESS_ROUTINE progress, LPVOID data, LPBOOL cancel,
                                   DWORD flags)
{
    PWSTR from = KbAnsiToWide(source), to = KbAnsiToWide(destination);
    BOOL ok = FALSE;
    if (from && to)
        ok = CopyFileExW(from, to, progress, data, cancel, flags);
    KbFree(from);
    KbFree(to);
    return ok;
}

WINBASEAPI BOOL WINAPI CreateDirectoryExA(LPCSTR template_dir, LPCSTR name, LPSECURITY_ATTRIBUTES sa)
{
    PWSTR from = KbAnsiToWide(template_dir), to = KbAnsiToWide(name);
    BOOL ok = FALSE;
    if (from && to)
        ok = CreateDirectoryExW(from, to, sa);
    KbFree(from);
    KbFree(to);
    return ok;
}

WINBASEAPI BOOL WINAPI MoveFileWithProgressA(LPCSTR source, LPCSTR destination, LPPROGRESS_ROUTINE progress, LPVOID data,
                                             DWORD flags)
{
    PWSTR from = KbAnsiToWide(source), to = destination ? KbAnsiToWide(destination) : nullptr;
    BOOL ok = FALSE;
    if (from && (to || !destination))
        ok = MoveFileWithProgressW(from, to, progress, data, flags);
    KbFree(from);
    KbFree(to);
    return ok;
}
