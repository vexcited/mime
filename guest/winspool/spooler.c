/*
 * the print server: enumerating printers, drivers, ports, monitors and print
 * processors, the default printer, the spool directories and the functions
 * that install or remove spooler components. nothing is installed.
 */
#include "winspool_private.h"

/* ------------------------------------------------------------------ helpers */

PWSTR SpoolToWide(LPCSTR s)
{
    if (!s)
        return NULL;
    int count = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    PWSTR w = SpoolAlloc((count ? count : 1) * sizeof(WCHAR));
    if (w && count)
        MultiByteToWideChar(CP_ACP, 0, s, -1, w, count);
    return w;
}

BOOL SpoolToAnsi(PCWSTR s, LPSTR buffer, DWORD size)
{
    int count = WideCharToMultiByte(CP_ACP, 0, s, -1, NULL, 0, NULL, NULL);
    if (!buffer || !count || (DWORD)count > size)
        return FALSE;
    WideCharToMultiByte(CP_ACP, 0, s, -1, buffer, count, NULL, NULL);
    return TRUE;
}

PCWSTR SpoolNativeEnvironment(void)
{
#if defined(__x86_64__)
    return L"Windows x64";
#else
    return L"Windows ARM64";
#endif
}

static const struct {
    PCWSTR environment;
    PCWSTR directory;
} g_environments[] = {
    {L"Windows NT x86", L"W32X86"},
    {L"Windows x64", L"x64"},
    {L"Windows ARM64", L"ARM64"},
    {L"Windows IA64", L"IA64"},
    {L"Windows 4.0", L"WIN40"},
};

PCWSTR SpoolEnvironmentDirectory(LPCWSTR environment)
{
    if (!environment || !*environment)
        environment = SpoolNativeEnvironment();
    for (SIZE_T i = 0; i < ARRAYSIZE(g_environments); i++) {
        if (!_wcsicmp(environment, g_environments[i].environment))
            return g_environments[i].directory;
    }
    return NULL;
}

/* checks the arguments every enumeration shares and reports an empty list. */
static BOOL SpoolEmpty(BOOL valid_level, LPBYTE buffer, DWORD size, LPDWORD needed, LPDWORD returned)
{
    if (!needed || !returned)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (!valid_level)
        return SpoolFail(ERROR_INVALID_LEVEL);
    if (buffer)
        memset(buffer, 0, size);
    *needed = 0;
    *returned = 0;
    return TRUE;
}

/* converts an optional ansi environment and checks that it names a known one. */
static BOOL SpoolCheckEnvironmentA(LPCSTR environment)
{
    if (!environment || !_stricmp(environment, "all"))
        return TRUE;
    PWSTR wide = SpoolToWide(environment);
    if (!wide)
        return SpoolFail(ERROR_NOT_ENOUGH_MEMORY);
    BOOL known = SpoolEnvironmentDirectory(wide) != NULL;
    SpoolFree(wide);
    return known ? TRUE : SpoolFail(ERROR_INVALID_ENVIRONMENT);
}

static BOOL SpoolCheckEnvironmentW(LPCWSTR environment)
{
    if (environment && _wcsicmp(environment, L"all") && !SpoolEnvironmentDirectory(environment))
        return SpoolFail(ERROR_INVALID_ENVIRONMENT);
    return TRUE;
}

/* ------------------------------------------------------------------ enumeration */

static BOOL SpoolPrinterLevel(DWORD level)
{
    return level <= 2 || level == 4 || level == 5;
}

__declspec(dllexport) BOOL WINAPI EnumPrintersW(DWORD flags, LPWSTR name, DWORD level, LPBYTE buffer, DWORD size,
                                                LPDWORD needed, LPDWORD returned)
{
    return SpoolEmpty(SpoolPrinterLevel(level), buffer, size, needed, returned);
}

__declspec(dllexport) BOOL WINAPI EnumPrintersA(DWORD flags, LPSTR name, DWORD level, LPBYTE buffer, DWORD size,
                                                LPDWORD needed, LPDWORD returned)
{
    return SpoolEmpty(SpoolPrinterLevel(level), buffer, size, needed, returned);
}

static BOOL SpoolDriverLevel(DWORD level)
{
    return (level >= 1 && level <= 6) || level == 8;
}

__declspec(dllexport) BOOL WINAPI EnumPrinterDriversW(LPWSTR name, LPWSTR environment, DWORD level, LPBYTE buffer,
                                                      DWORD size, LPDWORD needed, LPDWORD returned)
{
    if (needed && returned && SpoolDriverLevel(level) && !SpoolCheckEnvironmentW(environment))
        return FALSE;
    return SpoolEmpty(SpoolDriverLevel(level), NULL, 0, needed, returned);
}

__declspec(dllexport) BOOL WINAPI EnumPrinterDriversA(LPSTR name, LPSTR environment, DWORD level, LPBYTE buffer,
                                                      DWORD size, LPDWORD needed, LPDWORD returned)
{
    if (needed && returned && SpoolDriverLevel(level) && !SpoolCheckEnvironmentA(environment))
        return FALSE;
    return SpoolEmpty(SpoolDriverLevel(level), NULL, 0, needed, returned);
}

__declspec(dllexport) BOOL WINAPI EnumPortsW(LPWSTR name, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                                             LPDWORD returned)
{
    return SpoolEmpty(level == 1 || level == 2, buffer, size, needed, returned);
}

__declspec(dllexport) BOOL WINAPI EnumPortsA(LPSTR name, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                                             LPDWORD returned)
{
    return SpoolEmpty(level == 1 || level == 2, buffer, size, needed, returned);
}

__declspec(dllexport) BOOL WINAPI EnumMonitorsW(LPWSTR name, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                                                LPDWORD returned)
{
    return SpoolEmpty(level == 1 || level == 2, buffer, size, needed, returned);
}

__declspec(dllexport) BOOL WINAPI EnumMonitorsA(LPSTR name, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                                                LPDWORD returned)
{
    return SpoolEmpty(level == 1 || level == 2, buffer, size, needed, returned);
}

__declspec(dllexport) BOOL WINAPI EnumPrintProcessorsW(LPWSTR name, LPWSTR environment, DWORD level, LPBYTE buffer,
                                                       DWORD size, LPDWORD needed, LPDWORD returned)
{
    if (needed && returned && level == 1 && !SpoolCheckEnvironmentW(environment))
        return FALSE;
    return SpoolEmpty(level == 1, buffer, size, needed, returned);
}

__declspec(dllexport) BOOL WINAPI EnumPrintProcessorsA(LPSTR name, LPSTR environment, DWORD level, LPBYTE buffer,
                                                       DWORD size, LPDWORD needed, LPDWORD returned)
{
    if (needed && returned && level == 1 && !SpoolCheckEnvironmentA(environment))
        return FALSE;
    return SpoolEmpty(level == 1, buffer, size, needed, returned);
}

/* with no print processor installed every processor name is unknown. */
static BOOL SpoolEnumDatatypes(DWORD level, LPDWORD needed, LPDWORD returned)
{
    if (!needed || !returned)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (level != 1)
        return SpoolFail(ERROR_INVALID_LEVEL);
    return SpoolFail(ERROR_UNKNOWN_PRINTPROCESSOR);
}

__declspec(dllexport) BOOL WINAPI EnumPrintProcessorDatatypesW(LPWSTR name, LPWSTR processor, DWORD level,
                                                               LPBYTE buffer, DWORD size, LPDWORD needed,
                                                               LPDWORD returned)
{
    return SpoolEnumDatatypes(level, needed, returned);
}

__declspec(dllexport) BOOL WINAPI EnumPrintProcessorDatatypesA(LPSTR name, LPSTR processor, DWORD level,
                                                               LPBYTE buffer, DWORD size, LPDWORD needed,
                                                               LPDWORD returned)
{
    return SpoolEnumDatatypes(level, needed, returned);
}

/* ------------------------------------------------------------------ spool directories */

/* builds %SystemRoot%\system32\spool\<kind>\<environment directory>. */
static BOOL SpoolDirectory(LPCWSTR environment, DWORD level, LPDWORD needed, PCWSTR kind, PWSTR path)
{
    if (!needed)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (level != 1)
        return SpoolFail(ERROR_INVALID_LEVEL);
    PCWSTR directory = SpoolEnvironmentDirectory(environment);
    if (!directory)
        return SpoolFail(ERROR_INVALID_ENVIRONMENT);
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return SpoolFail(ERROR_INSUFFICIENT_BUFFER);
    _snwprintf(path + n, MAX_PATH, L"\\spool\\%ls\\%ls", kind, directory);
    return TRUE;
}

static BOOL SpoolDirectoryW(LPWSTR environment, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                            PCWSTR kind)
{
    WCHAR path[2 * MAX_PATH];
    if (!SpoolDirectory(environment, level, needed, kind, path))
        return FALSE;
    DWORD bytes = (DWORD)(wcslen(path) + 1) * sizeof(WCHAR);
    *needed = bytes;
    if (!buffer || size < bytes)
        return SpoolFail(ERROR_INSUFFICIENT_BUFFER);
    memcpy(buffer, path, bytes);
    return TRUE;
}

/* the ansi forms need room for the unicode result and report its size, like windows. */
static BOOL SpoolDirectoryA(LPSTR environment, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed, PCWSTR kind)
{
    PWSTR wide = SpoolToWide(environment);
    if (environment && !wide)
        return SpoolFail(ERROR_NOT_ENOUGH_MEMORY);
    WCHAR path[2 * MAX_PATH];
    BOOL ok = SpoolDirectory(wide, level, needed, kind, path);
    SpoolFree(wide);
    if (!ok)
        return FALSE;
    DWORD bytes = (DWORD)(wcslen(path) + 1) * sizeof(WCHAR);
    *needed = bytes;
    if (!buffer || size < bytes)
        return SpoolFail(ERROR_INSUFFICIENT_BUFFER);
    return SpoolToAnsi(path, (LPSTR)buffer, size) ? TRUE : SpoolFail(ERROR_INSUFFICIENT_BUFFER);
}

__declspec(dllexport) BOOL WINAPI GetPrinterDriverDirectoryW(LPWSTR name, LPWSTR environment, DWORD level,
                                                             LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return SpoolDirectoryW(environment, level, buffer, size, needed, L"DRIVERS");
}

__declspec(dllexport) BOOL WINAPI GetPrinterDriverDirectoryA(LPSTR name, LPSTR environment, DWORD level,
                                                             LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return SpoolDirectoryA(environment, level, buffer, size, needed, L"DRIVERS");
}

__declspec(dllexport) BOOL WINAPI GetPrintProcessorDirectoryW(LPWSTR name, LPWSTR environment, DWORD level,
                                                              LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return SpoolDirectoryW(environment, level, buffer, size, needed, L"PRTPROCS");
}

__declspec(dllexport) BOOL WINAPI GetPrintProcessorDirectoryA(LPSTR name, LPSTR environment, DWORD level,
                                                              LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return SpoolDirectoryA(environment, level, buffer, size, needed, L"PRTPROCS");
}

/* ------------------------------------------------------------------ default printer */

/* there is no default printer: the size is left alone. */
__declspec(dllexport) BOOL WINAPI GetDefaultPrinterW(LPWSTR buffer, LPDWORD size)
{
    if (!size)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    return SpoolFail(ERROR_FILE_NOT_FOUND);
}

__declspec(dllexport) BOOL WINAPI GetDefaultPrinterA(LPSTR buffer, LPDWORD size)
{
    return GetDefaultPrinterW(NULL, size);
}

__declspec(dllexport) BOOL WINAPI SetDefaultPrinterW(LPCWSTR printer)
{
    return SpoolFail(ERROR_INVALID_PRINTER_NAME);
}

__declspec(dllexport) BOOL WINAPI SetDefaultPrinterA(LPCSTR printer)
{
    return SpoolFail(ERROR_INVALID_PRINTER_NAME);
}

/* ------------------------------------------------------------------ printers and connections */

static HANDLE SpoolAddPrinter(DWORD level, LPBYTE printer)
{
    if (level != 2)
        SetLastError(ERROR_INVALID_LEVEL);
    else if (!printer)
        SetLastError(ERROR_INVALID_PARAMETER);
    else
        SetLastError(ERROR_UNKNOWN_PRINTER_DRIVER);
    return NULL;
}

__declspec(dllexport) HANDLE WINAPI AddPrinterW(LPWSTR name, DWORD level, LPBYTE printer)
{
    return SpoolAddPrinter(level, printer);
}

__declspec(dllexport) HANDLE WINAPI AddPrinterA(LPSTR name, DWORD level, LPBYTE printer)
{
    return SpoolAddPrinter(level, printer);
}

__declspec(dllexport) BOOL WINAPI AddPrinterConnectionW(LPWSTR name)
{
    return SpoolFail(ERROR_INVALID_PRINTER_NAME);
}

__declspec(dllexport) BOOL WINAPI AddPrinterConnectionA(LPSTR name)
{
    return SpoolFail(ERROR_INVALID_PRINTER_NAME);
}

__declspec(dllexport) BOOL WINAPI AddPrinterConnection2W(HWND window, LPCWSTR name, DWORD level, PVOID info)
{
    return SpoolFail(ERROR_INVALID_PRINTER_NAME);
}

/* the ansi form is not implemented by windows either. */
__declspec(dllexport) BOOL WINAPI AddPrinterConnection2A(HWND window, LPCSTR name, DWORD level, PVOID info)
{
    return SpoolFail(ERROR_NOT_SUPPORTED);
}

__declspec(dllexport) BOOL WINAPI DeletePrinterConnectionW(LPWSTR name)
{
    return SpoolFail(ERROR_INVALID_PRINTER_NAME);
}

__declspec(dllexport) BOOL WINAPI DeletePrinterConnectionA(LPSTR name)
{
    return SpoolFail(ERROR_INVALID_PRINTER_NAME);
}

__declspec(dllexport) HANDLE WINAPI ConnectToPrinterDlg(HWND window, DWORD flags)
{
    return NULL;
}

/* ------------------------------------------------------------------ drivers */

/* the files a driver names are looked up in the empty driver directory. */
static BOOL SpoolAddDriver(DWORD level, LPBYTE info)
{
    if (level < 2 || level > 8 || level == 5 || level == 7)
        return SpoolFail(ERROR_INVALID_LEVEL);
    if (!info)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    return SpoolFail(ERROR_FILE_NOT_FOUND);
}

__declspec(dllexport) BOOL WINAPI AddPrinterDriverW(LPWSTR name, DWORD level, LPBYTE info)
{
    return SpoolAddDriver(level, info);
}

__declspec(dllexport) BOOL WINAPI AddPrinterDriverA(LPSTR name, DWORD level, LPBYTE info)
{
    return SpoolAddDriver(level, info);
}

__declspec(dllexport) BOOL WINAPI AddPrinterDriverExW(LPWSTR name, DWORD level, LPBYTE info, DWORD flags)
{
    return SpoolAddDriver(level, info);
}

__declspec(dllexport) BOOL WINAPI AddPrinterDriverExA(LPSTR name, DWORD level, LPBYTE info, DWORD flags)
{
    return SpoolAddDriver(level, info);
}

__declspec(dllexport) BOOL WINAPI DeletePrinterDriverW(LPWSTR name, LPWSTR environment, LPWSTR driver)
{
    return SpoolCheckEnvironmentW(environment) && SpoolFail(ERROR_UNKNOWN_PRINTER_DRIVER);
}

__declspec(dllexport) BOOL WINAPI DeletePrinterDriverA(LPSTR name, LPSTR environment, LPSTR driver)
{
    return SpoolCheckEnvironmentA(environment) && SpoolFail(ERROR_UNKNOWN_PRINTER_DRIVER);
}

__declspec(dllexport) BOOL WINAPI DeletePrinterDriverExW(LPWSTR name, LPWSTR environment, LPWSTR driver,
                                                         DWORD flags, DWORD version)
{
    return SpoolCheckEnvironmentW(environment) && SpoolFail(ERROR_UNKNOWN_PRINTER_DRIVER);
}

__declspec(dllexport) BOOL WINAPI DeletePrinterDriverExA(LPSTR name, LPSTR environment, LPSTR driver, DWORD flags,
                                                         DWORD version)
{
    return SpoolCheckEnvironmentA(environment) && SpoolFail(ERROR_UNKNOWN_PRINTER_DRIVER);
}

/* ------------------------------------------------------------------ ports, monitors, processors, providers */

__declspec(dllexport) BOOL WINAPI AddPortW(LPWSTR name, HWND window, LPWSTR monitor)
{
    return SpoolFail(ERROR_UNKNOWN_PRINT_MONITOR);
}

__declspec(dllexport) BOOL WINAPI AddPortA(LPSTR name, HWND window, LPSTR monitor)
{
    return SpoolFail(ERROR_UNKNOWN_PRINT_MONITOR);
}

__declspec(dllexport) BOOL WINAPI AddPortExW(LPWSTR name, DWORD level, LPBYTE buffer, LPWSTR monitor)
{
    return SpoolFail(ERROR_UNKNOWN_PRINT_MONITOR);
}

__declspec(dllexport) BOOL WINAPI AddPortExA(LPSTR name, DWORD level, LPBYTE buffer, LPSTR monitor)
{
    return SpoolFail(ERROR_UNKNOWN_PRINT_MONITOR);
}

__declspec(dllexport) BOOL WINAPI ConfigurePortW(LPWSTR name, HWND window, LPWSTR port)
{
    return SpoolFail(ERROR_UNKNOWN_PORT);
}

__declspec(dllexport) BOOL WINAPI ConfigurePortA(LPSTR name, HWND window, LPSTR port)
{
    return SpoolFail(ERROR_UNKNOWN_PORT);
}

__declspec(dllexport) BOOL WINAPI DeletePortW(LPWSTR name, HWND window, LPWSTR port)
{
    return SpoolFail(ERROR_UNKNOWN_PORT);
}

__declspec(dllexport) BOOL WINAPI DeletePortA(LPSTR name, HWND window, LPSTR port)
{
    return SpoolFail(ERROR_UNKNOWN_PORT);
}

__declspec(dllexport) BOOL WINAPI SetPortW(LPWSTR name, LPWSTR port, DWORD level, LPBYTE info)
{
    return SpoolFail(ERROR_UNKNOWN_PORT);
}

__declspec(dllexport) BOOL WINAPI SetPortA(LPSTR name, LPSTR port, DWORD level, LPBYTE info)
{
    return SpoolFail(ERROR_UNKNOWN_PORT);
}

/* monitors, processors and providers name a dll that would have to be in a spool directory. */
__declspec(dllexport) BOOL WINAPI AddMonitorW(LPWSTR name, DWORD level, LPBYTE info)
{
    return SpoolFail(level == 2 ? ERROR_FILE_NOT_FOUND : ERROR_INVALID_LEVEL);
}

__declspec(dllexport) BOOL WINAPI AddMonitorA(LPSTR name, DWORD level, LPBYTE info)
{
    return SpoolFail(level == 2 ? ERROR_FILE_NOT_FOUND : ERROR_INVALID_LEVEL);
}

__declspec(dllexport) BOOL WINAPI DeleteMonitorW(LPWSTR name, LPWSTR environment, LPWSTR monitor)
{
    return SpoolFail(ERROR_UNKNOWN_PRINT_MONITOR);
}

__declspec(dllexport) BOOL WINAPI DeleteMonitorA(LPSTR name, LPSTR environment, LPSTR monitor)
{
    return SpoolFail(ERROR_UNKNOWN_PRINT_MONITOR);
}

__declspec(dllexport) BOOL WINAPI AddPrintProcessorW(LPWSTR name, LPWSTR environment, LPWSTR path,
                                                     LPWSTR processor)
{
    return SpoolCheckEnvironmentW(environment) && SpoolFail(ERROR_FILE_NOT_FOUND);
}

__declspec(dllexport) BOOL WINAPI AddPrintProcessorA(LPSTR name, LPSTR environment, LPSTR path, LPSTR processor)
{
    return SpoolCheckEnvironmentA(environment) && SpoolFail(ERROR_FILE_NOT_FOUND);
}

__declspec(dllexport) BOOL WINAPI DeletePrintProcessorW(LPWSTR name, LPWSTR environment, LPWSTR processor)
{
    return SpoolCheckEnvironmentW(environment) && SpoolFail(ERROR_UNKNOWN_PRINTPROCESSOR);
}

__declspec(dllexport) BOOL WINAPI DeletePrintProcessorA(LPSTR name, LPSTR environment, LPSTR processor)
{
    return SpoolCheckEnvironmentA(environment) && SpoolFail(ERROR_UNKNOWN_PRINTPROCESSOR);
}

__declspec(dllexport) BOOL WINAPI AddPrintProvidorW(LPWSTR name, DWORD level, LPBYTE info)
{
    return SpoolFail(ERROR_FILE_NOT_FOUND);
}

__declspec(dllexport) BOOL WINAPI AddPrintProvidorA(LPSTR name, DWORD level, LPBYTE info)
{
    return SpoolFail(ERROR_FILE_NOT_FOUND);
}

__declspec(dllexport) BOOL WINAPI DeletePrintProvidorW(LPWSTR name, LPWSTR environment, LPWSTR provider)
{
    return SpoolFail(ERROR_FILE_NOT_FOUND);
}

__declspec(dllexport) BOOL WINAPI DeletePrintProvidorA(LPSTR name, LPSTR environment, LPSTR provider)
{
    return SpoolFail(ERROR_FILE_NOT_FOUND);
}

/* ------------------------------------------------------------------ execution context */

__declspec(dllexport) BOOL WINAPI GetPrintExecutionData(PRINT_EXECUTION_DATA *data)
{
    if (!data)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    data->context = PRINT_EXECUTION_CONTEXT_APPLICATION;
    data->clientAppPID = GetCurrentProcessId();
    return TRUE;
}
