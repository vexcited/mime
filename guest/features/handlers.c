/* What puts each feature on the drive and takes it off. */
#include "featurep.h"

#define WEBVIEW2_CLIENTS L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"
#define WEBVIEW2_CLIENT_STATE L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\ClientState\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"
#define INSTALL_MINUTES 30

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

/* A process that ends with an exit code, or a limit. Returns the Win32 error of starting or waiting. */
static DWORD run_and_wait(RUN *run, PWSTR command_line, DWORD *exit_code, PCWSTR step)
{
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(nullptr, command_line, nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &startup,
                        &process))
        return GetLastError();
    CloseHandle(process.hThread);
    ULONGLONG start = GetTickCount64();
    DWORD wait;
    while ((wait = WaitForSingleObject(process.hProcess, 500)) == WAIT_TIMEOUT) {
        WCHAR text[128];
        ULONGLONG seconds = (GetTickCount64() - start) / 1000;
        _snwprintf(text, RTL_NUMBER_OF(text), L"%ls %llu s", step, seconds);
        text[RTL_NUMBER_OF(text) - 1] = 0;
        RunReport(run, 0, 0, text);
        if (seconds > INSTALL_MINUTES * 60) {
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
        DWORD code;
        DWORD error = run_and_wait(run, line, &code, L"Installing");
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

static uint32_t netfx_install(RUN *run, PCWSTR package)
{
    run->Cancellable = TRUE;
    return NetfxInstallFromPackage(package, run->Root, netfx_progress, run);
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
    return netfx_present(run) ? ERROR_SHARING_VIOLATION : ERROR_SUCCESS;
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
    case FEATURE_HANDLER_FONTS:
        return FontsRemove(run, feature);
    default:
        return ERROR_NOT_SUPPORTED;
    }
}
