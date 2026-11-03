/* What puts each feature on the drive and takes it off. */
#include "featurep.h"

#define WEBVIEW2_CLIENTS L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"
#define WEBVIEW2_CLIENT_STATE L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\ClientState\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"
#define INSTALL_MINUTES 30

/*
 * The folders of the feature that Windows has whether or not it is on, as host/install.c makes them on the drive. The
 * removal takes them with the files, and a program that looks at them must still find them.
 */
static void keep_folders(RUN *run, const WCHAR *const *folders, SIZE_T count)
{
    SIZE_T root = wcslen(run->Root);
    for (SIZE_T i = 0; i < count; i++) {
        WCHAR path[MAX_PATH];
        _snwprintf(path, RTL_NUMBER_OF(path), L"%ls%ls", run->Root, folders[i]);
        path[RTL_NUMBER_OF(path) - 1] = 0;
        for (WCHAR *p = path + root; *p; p++) {
            if (*p != L'\\')
                continue;
            *p = 0;
            CreateDirectoryW(path, nullptr);
            *p = L'\\';
        }
        CreateDirectoryW(path, nullptr);
    }
}

/* ------------------------------------------------------------------ the WebView2 runtime */

static void webview2_application(RUN *run, WCHAR *path, SIZE_T chars)
{
    _snwprintf(path, chars, L"%lsProgram Files (x86)\\Microsoft\\EdgeWebView\\Application", run->Root);
    path[chars - 1] = 0;
}

/* The runtime lives in a folder named after its version, which holds msedgewebview2.exe. */
static BOOL webview2_find(RUN *run, WCHAR *version, SIZE_T chars)
{
    WCHAR application[MAX_PATH], pattern[MAX_PATH];
    webview2_application(run, application, RTL_NUMBER_OF(application));
    _snwprintf(pattern, RTL_NUMBER_OF(pattern), L"%ls\\*", application);
    pattern[RTL_NUMBER_OF(pattern) - 1] = 0;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW(pattern, FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE)
        return FALSE;
    BOOL found = FALSE;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || data.cFileName[0] == L'.')
            continue;
        WCHAR program[MAX_PATH];
        _snwprintf(program, RTL_NUMBER_OF(program), L"%ls\\%ls\\msedgewebview2.exe", application, data.cFileName);
        program[RTL_NUMBER_OF(program) - 1] = 0;
        if (GetFileAttributesW(program) != INVALID_FILE_ATTRIBUTES) {
            found = TRUE;
            if (version)
                lstrcpynW(version, data.cFileName, (int)chars);
            break;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return found;
}

static BOOL webview2_present(RUN *run)
{
    return webview2_find(run, nullptr, 0);
}

static void webview2_version(RUN *run, WCHAR *version, SIZE_T chars)
{
    webview2_find(run, version, chars);
}

/* The bytes of the files below a folder. */
static ULONGLONG folder_bytes(PCWSTR path)
{
    WCHAR pattern[MAX_PATH];
    _snwprintf(pattern, RTL_NUMBER_OF(pattern), L"%ls\\*", path);
    pattern[RTL_NUMBER_OF(pattern) - 1] = 0;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW(pattern, FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE)
        return 0;
    ULONGLONG bytes = 0;
    do {
        if (data.cFileName[0] == L'.' && (!data.cFileName[1] || (data.cFileName[1] == L'.' && !data.cFileName[2])))
            continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            WCHAR child[MAX_PATH];
            _snwprintf(child, RTL_NUMBER_OF(child), L"%ls\\%ls", path, data.cFileName);
            child[RTL_NUMBER_OF(child) - 1] = 0;
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                bytes += folder_bytes(child);
        } else {
            bytes += (ULONGLONG)data.nFileSizeHigh << 32 | data.nFileSizeLow;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return bytes;
}

/*
 * A process that ends with an exit code, or a limit. Returns the Win32 error of starting or waiting. An installer
 * tells nothing of how far it is, what it has written to its folder against what the feature takes is the progress.
 */
static DWORD run_and_wait(RUN *run, PWSTR command_line, DWORD *exit_code, PCWSTR folder)
{
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(nullptr, command_line, nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &startup,
                        &process))
        return GetLastError();
    CloseHandle(process.hThread);
    ULONGLONG start = GetTickCount64(), expected = (ULONGLONG)run->Feature->InstallMB << 20;
    DWORD wait;
    while ((wait = WaitForSingleObject(process.hProcess, 500)) == WAIT_TIMEOUT) {
        if (expected && folder) {
            ULONGLONG written = folder_bytes(folder);
            /* A version larger than the catalog says stays short of the end until the installer is done. */
            if (written > expected * 99 / 100)
                written = expected * 99 / 100;
            RunReport(run, written, expected, L"Installing the files");
        }
        if ((GetTickCount64() - start) / 1000 > INSTALL_MINUTES * 60) {
            TerminateProcess(process.hProcess, ERROR_TIMEOUT);
            break;
        }
    }
    DWORD result = wait == WAIT_OBJECT_0 ? ERROR_SUCCESS : ERROR_TIMEOUT;
    *exit_code = 0;
    if (result == ERROR_SUCCESS)
        GetExitCodeProcess(process.hProcess, exit_code);
    CloseHandle(process.hProcess);
    return result;
}

static void set_detail(RUN *run, PCWSTR format, DWORD code)
{
    _snwprintf(run->Detail, RTL_NUMBER_OF(run->Detail), format, code);
    run->Detail[RTL_NUMBER_OF(run->Detail) - 1] = 0;
}

/* The standalone installer of the Evergreen runtime, which installs for all users and ends with 0 when it is done. */
static uint32_t webview2_install(RUN *run, PCWSTR package)
{
    for (int attempt = 0; attempt < 2; attempt++) {
        WCHAR line[MAX_PATH + 40];
        _snwprintf(line, RTL_NUMBER_OF(line), L"\"%ls\" /silent /install", package);
        line[RTL_NUMBER_OF(line) - 1] = 0;
        WCHAR application[MAX_PATH];
        webview2_application(run, application, RTL_NUMBER_OF(application));
        DWORD code;
        DWORD error = run_and_wait(run, line, &code, application);
        if (error)
            return error;
        if (code == 0 && webview2_present(run))
            return ERROR_SUCCESS;
        set_detail(run, L"The installer ended with code 0x%08X.", code);
        /*
         * The registry may declare a runtime that has no files, and the updater inside the installer then refuses
         * a version that is not newer. Without the declaration the installer starts over.
         */
        if (attempt == 0 && !webview2_present(run)) {
            RegDeleteTreeW(HKEY_LOCAL_MACHINE, WEBVIEW2_CLIENTS);
            RegDeleteTreeW(HKEY_LOCAL_MACHINE, WEBVIEW2_CLIENT_STATE);
            continue;
        }
        return code && code < 0x10000 ? code : FEATURE_ERROR_INSTALL_FAILURE;
    }
    return FEATURE_ERROR_INSTALL_FAILURE;
}

static uint32_t webview2_remove(RUN *run)
{
    WCHAR folder[MAX_PATH];
    _snwprintf(folder, RTL_NUMBER_OF(folder), L"%lsProgram Files (x86)\\Microsoft\\EdgeWebView", run->Root);
    folder[RTL_NUMBER_OF(folder) - 1] = 0;
    DeleteTree(folder);
    static const WCHAR *const kept[] = {L"Program Files (x86)\\Microsoft\\EdgeWebView\\Application"};
    keep_folders(run, kept, RTL_NUMBER_OF(kept));
    if (webview2_present(run))
        return ERROR_SHARING_VIOLATION;
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ the .NET Framework */

static void netfx_path(RUN *run, PCWSTR below, WCHAR *path, SIZE_T chars)
{
    _snwprintf(path, chars, L"%ls%ls", run->Root, below);
    path[chars - 1] = 0;
}

static BOOL netfx_present(RUN *run)
{
    WCHAR path[MAX_PATH];
    netfx_path(run, L"Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\clr.dll", path, RTL_NUMBER_OF(path));
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static BOOL CALLBACK netfx_progress(void *context, ULONGLONG done, ULONGLONG total, PCWSTR step)
{
    RUN *run = context;
    RunReport(run, done, total, step);
    return !run->Cancel;
}

/*
 * The Framework ships the native image of mscorlib only. Windows compiles the others when it installs the Framework, and a
 * program that loads them without images spends its first seconds in the JIT compiler. Their dependencies are compiled
 * with them, what is compiled already is skipped.
 */
static const PCWSTR netfx_image_roots[] = {
    L"System, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089",
    L"System.Core, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089",
    L"System.Xml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089",
    L"System.Configuration, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a",
    L"System.Data, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089",
    L"System.Xml.Linq, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089",
    L"System.Runtime.Serialization, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089",
    L"System.Windows.Forms, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089",
    L"WindowsBase, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35",
    L"PresentationCore, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35",
    L"PresentationFramework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35",
};

/* Runs ngen.exe for an assembly of a Framework folder and waits, a program that finds no image compiles the assembly itself. */
static void netfx_ngen(RUN *run, PCWSTR framework, PCWSTR assembly)
{
    WCHAR line[MAX_PATH + 160];
    _snwprintf(line, RTL_NUMBER_OF(line), L"\"%lsWindows\\Microsoft.NET\\%ls\\v4.0.30319\\ngen.exe\" install \"%ls\" /nologo", run->Root,
               framework, assembly);
    line[RTL_NUMBER_OF(line) - 1] = 0;
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(nullptr, line, nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        return;
    CloseHandle(process.hThread);
    if (WaitForSingleObject(process.hProcess, INSTALL_MINUTES * 60 * 1000) == WAIT_TIMEOUT)
        TerminateProcess(process.hProcess, ERROR_TIMEOUT);
    CloseHandle(process.hProcess);
}

/* One compiler at a time, started together for both processors the 32-bit one ended at once with an error. */
static void netfx_native_images(RUN *run)
{
    /* the 32-bit programs of the media use the Windows Forms stack and not the 64-bit presentation one. */
    SIZE_T x86 = 8, total = RTL_NUMBER_OF(netfx_image_roots) + x86;
    for (SIZE_T i = 0; i < total && !run->Cancel; i++) {
        RunReport(run, i, total, L"Compiling the native images");
        if (i < RTL_NUMBER_OF(netfx_image_roots))
            netfx_ngen(run, L"Framework64", netfx_image_roots[i]);
        else
            netfx_ngen(run, L"Framework", netfx_image_roots[i - RTL_NUMBER_OF(netfx_image_roots)]);
    }
}

static uint32_t netfx_install(RUN *run, PCWSTR package)
{
    run->Cancellable = TRUE;
    uint32_t error = NetfxInstallFromPackage(package, run->Root, netfx_progress, run);
    if (error == ERROR_SUCCESS)
        netfx_native_images(run);
    return error;
}

/* The folders and the runtime libraries that the Framework puts on the drive. */
static uint32_t netfx_remove(RUN *run)
{
    static const WCHAR *const libraries[] = {
        L"clrhost.dll", L"mscorier.dll", L"mscories.dll", L"netfxperf.dll", L"msvcp120_clr0400.dll",
        L"msvcp140_clr0400.dll", L"msvcr100_clr0400.dll", L"msvcr120_clr0400.dll", L"ucrtbase_clr0400.dll",
        L"vcruntime140_clr0400.dll", L"vcruntime140_1_clr0400.dll",
    };
    WCHAR path[MAX_PATH];
    netfx_path(run, L"Windows\\Microsoft.NET", path, RTL_NUMBER_OF(path));
    DeleteTree(path);
    netfx_path(run, L"Windows\\assembly", path, RTL_NUMBER_OF(path));
    DeleteTree(path);
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(libraries); i++) {
        _snwprintf(path, RTL_NUMBER_OF(path), L"%lsWindows\\System32\\%ls", run->Root, libraries[i]);
        path[RTL_NUMBER_OF(path) - 1] = 0;
        DeleteFileW(path);
        _snwprintf(path, RTL_NUMBER_OF(path), L"%lsWindows\\SysWOW64\\%ls", run->Root, libraries[i]);
        path[RTL_NUMBER_OF(path) - 1] = 0;
        DeleteFileW(path);
    }
    static const WCHAR *const kept[] = {
        L"Windows\\Microsoft.NET\\Framework\\v4.0.30319", L"Windows\\Microsoft.NET\\Framework64\\v4.0.30319",
        L"Windows\\Microsoft.NET\\Assembly\\GAC_32", L"Windows\\Microsoft.NET\\Assembly\\GAC_64",
        L"Windows\\Microsoft.NET\\Assembly\\GAC_MSIL", L"Windows\\assembly\\GAC_32", L"Windows\\assembly\\GAC_64",
        L"Windows\\assembly\\GAC_MSIL", L"Windows\\assembly\\NativeImages_v4.0.30319_32",
        L"Windows\\assembly\\NativeImages_v4.0.30319_64",
    };
    keep_folders(run, kept, RTL_NUMBER_OF(kept));
    return netfx_present(run) ? ERROR_SHARING_VIOLATION : ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ Windows PowerShell */

static BOOL powershell_present(RUN *run)
{
    WCHAR path[MAX_PATH];
    netfx_path(run, L"Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe", path, RTL_NUMBER_OF(path));
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        return FALSE;
    netfx_path(run, L"Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Management.Automation", path,
               RTL_NUMBER_OF(path));
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

/* A feature the shell needs is asked for first when it is not there, by the program itself like a program would. */
static DWORD powershell_needs(RUN *run, PCWSTR name, PCWSTR title, BOOL (*present)(RUN *run))
{
    if (present(run))
        return ERROR_SUCCESS;
    WCHAR program[MAX_PATH], line[MAX_PATH + 100];
    DWORD n = GetModuleFileNameW(nullptr, program, RTL_NUMBER_OF(program));
    if (!n || n >= RTL_NUMBER_OF(program))
        return ERROR_PATH_NOT_FOUND;
    if (run->EnableFlags & FEATURE_ENABLE_ASK)
        _snwprintf(line, RTL_NUMBER_OF(line), L"\"%ls\" /enable-feature:%ls /caller-name:PowerShell", program, name);
    else
        _snwprintf(line, RTL_NUMBER_OF(line), L"\"%ls\" enable %ls", program, name);
    line[RTL_NUMBER_OF(line) - 1] = 0;
    RunReport(run, 0, 0, title);
    DWORD code;
    DWORD error = run_and_wait(run, line, &code, nullptr);
    if (error)
        return error;
    if (code)
        return code;
    return present(run) ? ERROR_SUCCESS : FEATURE_ERROR_INSTALL_FAILURE;
}

static BOOL CALLBACK powershell_progress(void *context, ULONGLONG done, ULONGLONG total, PCWSTR step)
{
    RUN *run = context;
    RunReport(run, done, total, step);
    return !run->Cancel;
}

static uint32_t powershell_install(RUN *run, PCWSTR package)
{
    DWORD error = powershell_needs(run, L"NetFx4", L"Waiting for the .NET Framework", netfx_present);
    if (!error)
        error = powershell_needs(run, L"ManagementInfrastructure", L"Waiting for the Management Infrastructure",
                                 ManagementPresent);
    if (error)
        return error;
    run->Cancellable = TRUE;
    error = PowerShellInstallFromPackage(package, run->Root, powershell_progress, run);
    if (error == ERROR_SUCCESS && !powershell_present(run))
        return FEATURE_ERROR_INSTALL_FAILURE;
    return error;
}

/* The folders of the shell, and the entries of the cache of assemblies that the package puts there. */
static uint32_t powershell_remove(RUN *run)
{
    static const WCHAR *const folders[] = {
        L"Windows\\System32\\WindowsPowerShell", L"Windows\\SysWOW64\\WindowsPowerShell",
        L"Program Files\\WindowsPowerShell", L"Program Files (x86)\\WindowsPowerShell",
    };
    WCHAR path[MAX_PATH];
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(folders); i++) {
        netfx_path(run, folders[i], path, RTL_NUMBER_OF(path));
        DeleteTree(path);
    }
    WCHAR cache[MAX_PATH], pattern[MAX_PATH];
    netfx_path(run, L"Windows\\Microsoft.NET\\assembly\\GAC_MSIL", cache, RTL_NUMBER_OF(cache));
    _snwprintf(pattern, RTL_NUMBER_OF(pattern), L"%ls\\*", cache);
    pattern[RTL_NUMBER_OF(pattern) - 1] = 0;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW(pattern, FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            static const WCHAR *const prefixes[] = {L"Microsoft.PowerShell.", L"Microsoft.PackageManagement",
                                                    L"System.Management.Automation", L"Policy.1.0.Microsoft.PowerShell.",
                                                    L"Policy.1.0.System.Management.Automation", L"Microsoft.WSMan."};
            for (SIZE_T i = 0; i < RTL_NUMBER_OF(prefixes); i++) {
                if (!_wcsnicmp(data.cFileName, prefixes[i], wcslen(prefixes[i]))) {
                    _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\%ls", cache, data.cFileName);
                    path[RTL_NUMBER_OF(path) - 1] = 0;
                    DeleteTree(path);
                    break;
                }
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    static const WCHAR *const kept[] = {
        L"Windows\\System32\\WindowsPowerShell\\v1.0\\Modules", L"Windows\\System32\\WindowsPowerShell\\v1.0\\Examples",
        L"Windows\\System32\\WindowsPowerShell\\v1.0\\Schemas", L"Windows\\System32\\WindowsPowerShell\\v1.0\\en-US",
        L"Windows\\SysWOW64\\WindowsPowerShell\\v1.0\\Modules", L"Windows\\SysWOW64\\WindowsPowerShell\\v1.0\\Examples",
        L"Windows\\SysWOW64\\WindowsPowerShell\\v1.0\\Schemas", L"Windows\\SysWOW64\\WindowsPowerShell\\v1.0\\en-US",
    };
    keep_folders(run, kept, RTL_NUMBER_OF(kept));
    return powershell_present(run) ? ERROR_SHARING_VIOLATION : ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ by handler */

uint32_t HandlerAvailable(const FEATURE_DEF *feature)
{
    switch (feature->Handler) {
    case FEATURE_HANDLER_WEBVIEW2:
        return ERROR_SUCCESS;
    case FEATURE_HANDLER_NETFX:
        /* The stub says it is not supported, the real one refuses a null package for another reason. */
        return NetfxInstallFromPackage(nullptr, nullptr, nullptr, nullptr) == ERROR_NOT_SUPPORTED ? ERROR_NOT_SUPPORTED
                                                                                                     : ERROR_SUCCESS;
    case FEATURE_HANDLER_POWERSHELL:
        /* The package has the files of the x64 processor only. */
        return MACHINE_ARCH == FEATURE_ARCH_X64 ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
    case FEATURE_HANDLER_MANAGEMENT:
    case FEATURE_HANDLER_FONTS:
        return ERROR_SUCCESS;
    default:
        return ERROR_NOT_SUPPORTED;
    }
}

BOOL HandlerPresent(RUN *run, const FEATURE_DEF *feature)
{
    switch (feature->Handler) {
    case FEATURE_HANDLER_WEBVIEW2:
        return webview2_present(run);
    case FEATURE_HANDLER_NETFX:
        return netfx_present(run);
    case FEATURE_HANDLER_POWERSHELL:
        return powershell_present(run);
    case FEATURE_HANDLER_MANAGEMENT:
        return ManagementPresent(run);
    case FEATURE_HANDLER_FONTS:
        return FontsPresent(run, feature);
    default:
        return FALSE;
    }
}

void HandlerVersion(RUN *run, const FEATURE_DEF *feature, WCHAR *version, SIZE_T chars)
{
    if (feature->Handler == FEATURE_HANDLER_WEBVIEW2)
        webview2_version(run, version, chars);
}

uint32_t HandlerInstall(RUN *run, const FEATURE_DEF *feature, PCWSTR package)
{
    /* Whatever the installer starts must not ask for the feature it is putting there. */
    SetEnvironmentVariableW(MIME_FEATURE_BUSY_VARIABLE, L"1");
    switch (feature->Handler) {
    case FEATURE_HANDLER_WEBVIEW2:
        return webview2_install(run, package);
    case FEATURE_HANDLER_NETFX:
        return netfx_install(run, package);
    case FEATURE_HANDLER_POWERSHELL:
        return powershell_install(run, package);
    case FEATURE_HANDLER_MANAGEMENT:
        return ManagementInstall(run, feature, package);
    case FEATURE_HANDLER_FONTS:
        return FontsInstall(run, feature, package);
    default:
        return ERROR_NOT_SUPPORTED;
    }
}

uint32_t HandlerRemove(RUN *run, const FEATURE_DEF *feature)
{
    switch (feature->Handler) {
    case FEATURE_HANDLER_WEBVIEW2:
        return webview2_remove(run);
    case FEATURE_HANDLER_NETFX:
        return netfx_remove(run);
    case FEATURE_HANDLER_POWERSHELL:
        return powershell_remove(run);
    case FEATURE_HANDLER_MANAGEMENT:
        return ManagementRemove(run);
    case FEATURE_HANDLER_FONTS:
        return FontsRemove(run, feature);
    default:
        return ERROR_NOT_SUPPORTED;
    }
}
