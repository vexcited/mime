#include "msi_private.h"
#include <mime/win32/shlobj.h>

/*
 * the properties the installer sets about the machine, the user and the package before any action runs: system
 * version, shell folders, package platform. a package's Property table cannot override these names.
 */

static UINT set_folder(MIME_MSI_PACKAGE *package, PCWSTR name, PCWSTR path)
{
    WCHAR text[MAX_PATH + 2];
    SIZE_T length = wcslen(path);
    if (length >= MAX_PATH)
        return ERROR_SUCCESS;
    wcscpy(text, path);
    if (length && text[length - 1] != L'\\') {
        text[length++] = L'\\';
        text[length] = 0;
    }
    return MimeMsiSetProperty(package, name, text);
}

static UINT set_csidl(MIME_MSI_PACKAGE *package, PCWSTR name, int csidl)
{
    WCHAR path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, csidl | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, path)))
        return ERROR_SUCCESS;
    return set_folder(package, name, path);
}

/* a native folder: a 32-bit process reads it from the variable windows keeps for it, as it sees the 32-bit one. */
static UINT set_native(MIME_MSI_PACKAGE *package, PCWSTR name, PCWSTR variable, int csidl)
{
    WCHAR path[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(variable, path, RTL_NUMBER_OF(path));
    if (!length || length >= RTL_NUMBER_OF(path))
        return set_csidl(package, name, csidl);
    return set_folder(package, name, path);
}

BOOL MimeMsiIs64BitPackage(MIME_MSI_PACKAGE *package)
{
    MIME_MSI_SUMMARY_VALUE value;
    BOOL is64 = FALSE;
    if (!MimeMsiSummaryProperty(package->Database, 7, &value) && value.Type == 30 && value.Text) {
        is64 = !_wcsnicmp(value.Text, L"x64", 3) || !_wcsnicmp(value.Text, L"Intel64", 7) || !_wcsnicmp(value.Text, L"AMD64", 5);
        MimeMsiFree(value.Text);
    }
    return is64;
}

/* folders that depend on the installation scope, known only once the package's properties are in. */
UINT MimeMsiSetScopeFolders(MIME_MSI_PACKAGE *package)
{
    PCWSTR all_users = MimeMsiGetProperty(package, L"ALLUSERS");
    BOOL machine = !wcscmp(all_users, L"1") || !wcscmp(all_users, L"2");
    UINT error = ERROR_SUCCESS;
    error = error ? error : set_csidl(package, L"ProgramMenuFolder", machine ? CSIDL_COMMON_PROGRAMS : CSIDL_PROGRAMS);
    error = error ? error : set_csidl(package, L"StartMenuFolder", machine ? CSIDL_COMMON_STARTMENU : CSIDL_STARTMENU);
    error = error ? error : set_csidl(package, L"StartupFolder", machine ? CSIDL_COMMON_STARTUP : CSIDL_STARTUP);
    error = error ? error : set_csidl(package, L"DesktopFolder",
                                      machine ? CSIDL_COMMON_DESKTOPDIRECTORY : CSIDL_DESKTOPDIRECTORY);
    error = error ? error : set_csidl(package, L"FavoritesFolder", machine ? CSIDL_COMMON_FAVORITES : CSIDL_FAVORITES);
    error = error ? error : set_csidl(package, L"TemplateFolder", machine ? CSIDL_COMMON_TEMPLATES : CSIDL_TEMPLATES);
    error = error ? error : set_csidl(package, L"AdminToolsFolder",
                                      machine ? CSIDL_COMMON_ADMINTOOLS : CSIDL_ADMINTOOLS);
    return error;
}

UINT MimeMsiInitializeSystem(MIME_MSI_PACKAGE *package)
{
    RTL_OSVERSIONINFOEXW version = {.dwOSVersionInfoSize = sizeof(version)};
    RtlGetVersion(&version);
    WCHAR text[64];
    UINT error = ERROR_SUCCESS;
    BOOL is64 = MimeMsiIs64BitPackage(package);
    package->Is64Bit = is64;
    _snwprintf(text, RTL_NUMBER_OF(text), L"%u", version.dwMajorVersion * 100 + version.dwMinorVersion);
    error = MimeMsiSetProperty(package, L"VersionNT", text);
#ifdef _WIN64
    if (!error)
        error = MimeMsiSetProperty(package, L"VersionNT64", text);
#else
    BOOL wow64 = FALSE;
    typedef BOOL (WINAPI *IS_WOW64)(HANDLE, PBOOL);
    IS_WOW64 is_wow64 = (IS_WOW64)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process");
    if (is_wow64)
        is_wow64(GetCurrentProcess(), &wow64);
    if (!error && wow64)
        error = MimeMsiSetProperty(package, L"VersionNT64", text);
#endif
    _snwprintf(text, RTL_NUMBER_OF(text), L"%u", version.dwBuildNumber);
    error = error ? error : MimeMsiSetProperty(package, L"WindowsBuild", text);
    _snwprintf(text, RTL_NUMBER_OF(text), L"%u", version.wServicePackMajor);
    error = error ? error : MimeMsiSetProperty(package, L"ServicePackLevel", text);
    _snwprintf(text, RTL_NUMBER_OF(text), L"%u", version.wServicePackMinor);
    error = error ? error : MimeMsiSetProperty(package, L"ServicePackLevelMinor", text);
    _snwprintf(text, RTL_NUMBER_OF(text), L"%u", version.wProductType);
    error = error ? error : MimeMsiSetProperty(package, L"MsiNTProductType", text);
    error = error ? error : MimeMsiSetProperty(package, L"VersionMsi", L"5.00");
    error = error ? error : MimeMsiSetProperty(package, L"MsiNetAssemblySupport", L"4.8.9037.0");
    error = error ? error : MimeMsiSetProperty(package, L"Intel", L"6");
    error = error ? error : MimeMsiSetProperty(package, L"Msix64", L"6");
    error = error ? error : MimeMsiSetProperty(package, L"MsiAMD64", L"6");
    error = error ? error : MimeMsiSetProperty(package, L"TTCSupport", L"1");
    error = error ? error : MimeMsiSetProperty(package, L"ColorBits", L"32");

    _snwprintf(text, RTL_NUMBER_OF(text), L"%d", GetSystemMetrics(SM_CXSCREEN));
    error = error ? error : MimeMsiSetProperty(package, L"ScreenX", text);
    _snwprintf(text, RTL_NUMBER_OF(text), L"%d", GetSystemMetrics(SM_CYSCREEN));
    error = error ? error : MimeMsiSetProperty(package, L"ScreenY", text);
    _snwprintf(text, RTL_NUMBER_OF(text), L"%u", GetSystemDefaultLangID());
    error = error ? error : MimeMsiSetProperty(package, L"SystemLanguageID", text);
    _snwprintf(text, RTL_NUMBER_OF(text), L"%u", GetUserDefaultLangID());
    error = error ? error : MimeMsiSetProperty(package, L"UserLanguageID", text);

    SYSTEMTIME time;
    GetLocalTime(&time);
    _snwprintf(text, RTL_NUMBER_OF(text), L"%u/%u/%u", time.wMonth, time.wDay, time.wYear);
    error = error ? error : MimeMsiSetProperty(package, L"Date", text);
    _snwprintf(text, RTL_NUMBER_OF(text), L"%02u:%02u:%02u", time.wHour, time.wMinute, time.wSecond);
    error = error ? error : MimeMsiSetProperty(package, L"Time", text);

    WCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = RTL_NUMBER_OF(name);
    if (!error && GetComputerNameW(name, &size))
        error = MimeMsiSetProperty(package, L"ComputerName", name);
    WCHAR user[257];
    size = RTL_NUMBER_OF(user);
    if (!error && GetUserNameW(user, &size)) {
        error = MimeMsiSetProperty(package, L"LogonUser", user);
        if (!error)
            error = MimeMsiSetProperty(package, L"USERNAME", user);
    }
    WCHAR sid[256];
    if (!error && !MimeMsiCurrentSid(sid))
        error = MimeMsiSetProperty(package, L"UserSID", sid);
    error = error ? error : MimeMsiSetProperty(package, L"Privileged", L"1");
    error = error ? error : MimeMsiSetProperty(package, L"AdminUser", L"1");

    WCHAR path[MAX_PATH];
    UINT length = GetWindowsDirectoryW(path, RTL_NUMBER_OF(path));
    if (!error && length && length < RTL_NUMBER_OF(path)) {
        error = set_folder(package, L"WindowsFolder", path);
        WCHAR volume[4] = {path[0], L':', L'\\', 0};
        error = error ? error : MimeMsiSetProperty(package, L"WindowsVolume", volume);
        error = error ? error : MimeMsiSetProperty(package, L"ROOTDRIVE", volume);
    }
    length = GetSystemDirectoryW(path, RTL_NUMBER_OF(path));
#ifndef _WIN64
    /* a 32-bit installer sees the 32-bit system directory, the 64-bit one has its own name. */
    if (length && length < RTL_NUMBER_OF(path) && GetSystemWow64DirectoryW(path, RTL_NUMBER_OF(path))) {
        length = GetWindowsDirectoryW(path, RTL_NUMBER_OF(path));
        if (length && length + 9 < RTL_NUMBER_OF(path)) {
            wcscpy(path + length, L"\\System32");
            length += 9;
        }
    }
#endif
    /* on 64-bit windows SystemFolder and ProgramFilesFolder are the 32-bit folders for every package, the 64 names
       are the native ones. */
    WCHAR wow[MAX_PATH];
    BOOL native64 = GetSystemWow64DirectoryW(wow, RTL_NUMBER_OF(wow)) != 0;
    if (!error && length && length < RTL_NUMBER_OF(path)) {
        error = set_folder(package, L"SystemFolder", native64 ? wow : path);
        if (native64)
            error = error ? error : set_folder(package, L"System64Folder", path);
    }
    if (!error) {
        error = set_csidl(package, L"ProgramFilesFolder", native64 ? CSIDL_PROGRAM_FILESX86 : CSIDL_PROGRAM_FILES);
        error = error ? error : set_csidl(package, L"CommonFilesFolder",
                                          native64 ? CSIDL_PROGRAM_FILES_COMMONX86 : CSIDL_PROGRAM_FILES_COMMON);
        if (native64 && !error) {
            error = set_native(package, L"ProgramFiles64Folder", L"ProgramW6432", CSIDL_PROGRAM_FILES);
            error = error ? error : set_native(package, L"CommonFiles64Folder", L"CommonProgramW6432",
                                               CSIDL_PROGRAM_FILES_COMMON);
        }
        error = error ? error : set_csidl(package, L"AppDataFolder", CSIDL_APPDATA);
        error = error ? error : set_csidl(package, L"LocalAppDataFolder", CSIDL_LOCAL_APPDATA);
        error = error ? error : set_csidl(package, L"CommonAppDataFolder", CSIDL_COMMON_APPDATA);
        error = error ? error : set_csidl(package, L"PersonalFolder", CSIDL_PERSONAL);
        error = error ? error : set_csidl(package, L"MyPicturesFolder", CSIDL_MYPICTURES);
        error = error ? error : set_csidl(package, L"SendToFolder", CSIDL_SENDTO);
        error = error ? error : set_csidl(package, L"FontsFolder", CSIDL_FONTS);
    }
    length = GetTempPathW(RTL_NUMBER_OF(path), path);
    if (!error && length && length < RTL_NUMBER_OF(path))
        error = set_folder(package, L"TempFolder", path);
    error = error ? error : MimeMsiSetScopeFolders(package);

    /* the package code is the summary's revision number. */
    MIME_MSI_SUMMARY_VALUE revision;
    if (!error && !MimeMsiSummaryProperty(package->Database, 9, &revision)) {
        if (revision.Type == 30 && revision.Text)
            error = MimeMsiSetProperty(package, L"PackageCode", revision.Text);
        MimeMsiFree(revision.Text);
    }
    return error;
}
