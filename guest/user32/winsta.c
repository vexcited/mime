/*
 * window stations and desktops, objects of win32k reached by handle.
 */
#include "user32_private.h"

typedef BOOL(CALLBACK *NAMEENUMPROCA)(LPSTR name, LPARAM param);

static void name_attributes(OBJECT_ATTRIBUTES *oa, UNICODE_STRING *us, LPCWSTR name, HANDLE root, BOOL inherit)
{
    RtlInitUnicodeString(us, name ? name : L"");
    InitializeObjectAttributes(oa, us, OBJ_CASE_INSENSITIVE | (inherit ? OBJ_INHERIT : 0), root, nullptr);
}

static BOOL inherits(LPSECURITY_ATTRIBUTES sa)
{
    return sa && sa->bInheritHandle;
}

WINUSERAPI HWINSTA WINAPI CreateWindowStationW(LPCWSTR name, DWORD flags, ACCESS_MASK access, LPSECURITY_ATTRIBUTES sa)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING us;
    name_attributes(&oa, &us, name, nullptr, inherits(sa));
    if (sa)
        oa.SecurityDescriptor = sa->lpSecurityDescriptor;
    return NtUserCreateWindowStation(&oa, access, nullptr, 0, nullptr, nullptr, flags);
}

WINUSERAPI HWINSTA WINAPI CreateWindowStationA(LPCSTR name, DWORD flags, ACCESS_MASK access, LPSECURITY_ATTRIBUTES sa)
{
    WCHAR w[MAX_PATH];
    if (name)
        MultiByteToWideChar(CP_ACP, 0, name, -1, w, MAX_PATH);
    return CreateWindowStationW(name ? w : nullptr, flags, access, sa);
}

WINUSERAPI HWINSTA WINAPI OpenWindowStationW(LPCWSTR name, BOOL inherit, ACCESS_MASK access)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING us;
    name_attributes(&oa, &us, name, nullptr, inherit);
    return NtUserOpenWindowStation(&oa, access);
}

WINUSERAPI HWINSTA WINAPI OpenWindowStationA(LPCSTR name, BOOL inherit, ACCESS_MASK access)
{
    WCHAR w[MAX_PATH];
    if (name)
        MultiByteToWideChar(CP_ACP, 0, name, -1, w, MAX_PATH);
    return OpenWindowStationW(name ? w : nullptr, inherit, access);
}

WINUSERAPI BOOL WINAPI CloseWindowStation(HWINSTA handle)
{
    return NtUserCloseWindowStation(handle);
}

WINUSERAPI HWINSTA WINAPI GetProcessWindowStation(void)
{
    return NtUserGetProcessWindowStation();
}

WINUSERAPI BOOL WINAPI SetProcessWindowStation(HWINSTA handle)
{
    return NtUserSetProcessWindowStation(handle);
}

/* a new desktop belongs to the caller's window station. */
WINUSERAPI HDESK WINAPI CreateDesktopExW(LPCWSTR name, LPCWSTR device, PVOID devmode, DWORD flags,
                                         ACCESS_MASK access, LPSECURITY_ATTRIBUTES sa, ULONG heap_kb, PVOID reserved)
{
    if (device || devmode) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING us;
    name_attributes(&oa, &us, name, GetProcessWindowStation(), inherits(sa));
    if (sa)
        oa.SecurityDescriptor = sa->lpSecurityDescriptor;
    return NtUserCreateDesktopEx(&oa, nullptr, nullptr, flags, access, heap_kb);
}

WINUSERAPI HDESK WINAPI CreateDesktopW(LPCWSTR name, LPCWSTR device, PVOID devmode, DWORD flags, ACCESS_MASK access,
                                       LPSECURITY_ATTRIBUTES sa)
{
    return CreateDesktopExW(name, device, devmode, flags, access, sa, 0, nullptr);
}

WINUSERAPI HDESK WINAPI CreateDesktopExA(LPCSTR name, LPCSTR device, PVOID devmode, DWORD flags,
                                         ACCESS_MASK access, LPSECURITY_ATTRIBUTES sa, ULONG heap_kb, PVOID reserved)
{
    WCHAR w[MAX_PATH];
    if (name)
        MultiByteToWideChar(CP_ACP, 0, name, -1, w, MAX_PATH);
    if (device || devmode) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return CreateDesktopExW(name ? w : nullptr, nullptr, nullptr, flags, access, sa, heap_kb, reserved);
}

WINUSERAPI HDESK WINAPI CreateDesktopA(LPCSTR name, LPCSTR device, PVOID devmode, DWORD flags, ACCESS_MASK access,
                                       LPSECURITY_ATTRIBUTES sa)
{
    return CreateDesktopExA(name, device, devmode, flags, access, sa, 0, nullptr);
}

WINUSERAPI HDESK WINAPI OpenDesktopW(LPCWSTR name, DWORD flags, BOOL inherit, ACCESS_MASK access)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING us;
    name_attributes(&oa, &us, name, GetProcessWindowStation(), inherit);
    return NtUserOpenDesktop(&oa, flags, access);
}

WINUSERAPI HDESK WINAPI OpenDesktopA(LPCSTR name, DWORD flags, BOOL inherit, ACCESS_MASK access)
{
    WCHAR w[MAX_PATH];
    if (name)
        MultiByteToWideChar(CP_ACP, 0, name, -1, w, MAX_PATH);
    return OpenDesktopW(name ? w : nullptr, flags, inherit, access);
}

WINUSERAPI HDESK WINAPI OpenInputDesktop(DWORD flags, BOOL inherit, ACCESS_MASK access)
{
    return NtUserOpenInputDesktop(flags, inherit, access);
}

WINUSERAPI BOOL WINAPI CloseDesktop(HDESK handle)
{
    return NtUserCloseDesktop(handle);
}

WINUSERAPI HDESK WINAPI GetThreadDesktop(DWORD thread)
{
    return NtUserGetThreadDesktop(thread, nullptr);
}

WINUSERAPI BOOL WINAPI SetThreadDesktop(HDESK handle)
{
    return NtUserSetThreadDesktop(handle);
}

WINUSERAPI BOOL WINAPI SwitchDesktop(HDESK handle)
{
    return NtUserSwitchDesktop(handle, 0, 0);
}

WINUSERAPI BOOL WINAPI GetUserObjectInformationW(HANDLE object, int index, PVOID info, DWORD length, LPDWORD needed)
{
    return NtUserGetObjectInformation(object, (DWORD)index, info, length, needed);
}

/* the name and type come back in the ansi code page, their sizes counted in its bytes. */
WINUSERAPI BOOL WINAPI GetUserObjectInformationA(HANDLE object, int index, PVOID info, DWORD length, LPDWORD needed)
{
    if (index != 2 && index != 3)
        return GetUserObjectInformationW(object, index, info, length, needed);
    WCHAR wide[128];
    DWORD bytes;
    if (!GetUserObjectInformationW(object, index, wide, sizeof(wide), &bytes))
        return FALSE;
    int n = WideCharToMultiByte(CP_ACP, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (needed)
        *needed = (DWORD)n;
    if ((DWORD)n > length) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    WideCharToMultiByte(CP_ACP, 0, wide, -1, info, n, nullptr, nullptr);
    return TRUE;
}

WINUSERAPI BOOL WINAPI SetUserObjectInformationW(HANDLE object, int index, PVOID info, DWORD length)
{
    return NtUserSetObjectInformation(object, (DWORD)index, info, length);
}

WINUSERAPI BOOL WINAPI SetUserObjectInformationA(HANDLE object, int index, PVOID info, DWORD length)
{
    return SetUserObjectInformationW(object, index, info, length);
}

/* the names of a list, each given to the callback until it returns zero, whose value is the result. */
static BOOL enum_names(HWINSTA winsta, void *proc, LPARAM param, BOOL ansi)
{
    ULONG needed = 0;
    NtUserBuildNameList(winsta, 0, nullptr, &needed);
    if (!needed) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    BYTE *list = HeapAlloc(GetProcessHeap(), 0, needed);
    if (!list)
        return FALSE;
    NTSTATUS status = NtUserBuildNameList(winsta, needed, list, &needed);
    BOOL result = NT_SUCCESS(status);
    if (!result)
        SetLastError(RtlNtStatusToDosError(status));
    for (PWSTR name = (PWSTR)(list + 8); result && *name; name += wcslen(name) + 1) {
        if (ansi) {
            char narrow[128];
            WideCharToMultiByte(CP_ACP, 0, name, -1, narrow, sizeof(narrow), nullptr, nullptr);
            result = ((NAMEENUMPROCA)proc)(narrow, param);
        } else {
            result = ((WINSTAENUMPROCW)proc)(name, param);
        }
        if (!result)
            break;
    }
    HeapFree(GetProcessHeap(), 0, list);
    return result;
}

WINUSERAPI BOOL WINAPI EnumWindowStationsW(WINSTAENUMPROCW proc, LPARAM param)
{
    return enum_names(nullptr, (void *)proc, param, FALSE);
}

WINUSERAPI BOOL WINAPI EnumWindowStationsA(NAMEENUMPROCA proc, LPARAM param)
{
    return enum_names(nullptr, (void *)proc, param, TRUE);
}

WINUSERAPI BOOL WINAPI EnumDesktopsW(HWINSTA winsta, DESKTOPENUMPROCW proc, LPARAM param)
{
    return enum_names(winsta ? winsta : GetProcessWindowStation(), (void *)proc, param, FALSE);
}

WINUSERAPI BOOL WINAPI EnumDesktopsA(HWINSTA winsta, NAMEENUMPROCA proc, LPARAM param)
{
    return enum_names(winsta ? winsta : GetProcessWindowStation(), (void *)proc, param, TRUE);
}

/* ------------------------------------------------------------------ the session's input and resources */

WINUSERAPI BOOL WINAPI GetLastInputInfo(PLASTINPUTINFO info)
{
    if (!info || info->cbSize != sizeof(*info)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    info->dwTime = (DWORD)NtUserCallNoParam(MIME_NOPARAM_GETLASTINPUTTIME);
    return TRUE;
}

/* GR_GLOBAL counts the whole session's objects, which mime answers with the caller's. */
WINUSERAPI DWORD WINAPI GetGuiResources(HANDLE process, DWORD flags)
{
    return NtUserGetGuiResources(process, flags);
}
