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
        return nullptr;
    int count = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    PWSTR w = SpoolAlloc((count ? count : 1) * sizeof(WCHAR));
    if (w && count)
        MultiByteToWideChar(CP_ACP, 0, s, -1, w, count);
    return w;
}

BOOL SpoolToAnsi(PCWSTR s, LPSTR buffer, DWORD size)
{
    int count = WideCharToMultiByte(CP_ACP, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (!buffer || !count || (DWORD)count > size)
        return FALSE;
    WideCharToMultiByte(CP_ACP, 0, s, -1, buffer, count, nullptr, nullptr);
    return TRUE;
}

PCWSTR SpoolNativeEnvironment(void)
{
#ifdef __x86_64__
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
    return nullptr;
}

/* converts an optional ansi environment and checks that it names a known one. */
static BOOL SpoolCheckEnvironmentA(LPCSTR environment)
{
    if (!environment || !_stricmp(environment, "all"))
        return TRUE;
    PWSTR wide = SpoolToWide(environment);
    if (!wide)
        return SpoolFail(ERROR_NOT_ENOUGH_MEMORY);
    BOOL known = SpoolEnvironmentDirectory(wide) != nullptr;
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

#include "info.h"

static BOOL SpoolPrinterLevel(DWORD level)
{
    return level <= 2 || level == 4 || level == 5;
}

/* true for the name of the local server, or of its print provider, or none. */
static BOOL SpoolIsLocalProvider(LPCWSTR name)
{
    if (!name || !*name)
        return TRUE;
    if (!_wcsicmp(name, InfoProvider()))
        return TRUE;
    if (name[0] != L'\\' || name[1] != L'\\')
        return FALSE;
    WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(computer, &size) && !_wcsicmp(name + 2, computer))
        return TRUE;
    return !_wcsicmp(name + 2, L"localhost");
}

static BOOL SpoolEnumPrinters(DWORD flags, LPCWSTR name, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                              LPDWORD returned, BOOL ansi)
{
    if (!needed || !returned)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (!SpoolPrinterLevel(level))
        return SpoolFail(ERROR_INVALID_LEVEL);
    if (buffer)
        memset(buffer, 0, size);
    *needed = 0;
    *returned = 0;
    int indexes[16];
    int count = 0;
    BOOL local = SpoolIsLocalProvider(name);
    if (name && *name && !local) {
        /* the name of one printer lists that printer. */
        int one = (flags & PRINTER_ENUM_NAME) ? SpoolFindPrinter(name) : -1;
        if (one < 0)
            return SpoolFail(ERROR_INVALID_NAME);
        indexes[count++] = one;
    } else if (flags & PRINTER_ENUM_NAME) {
        /* without a name the print providers are what is listed, one level up from the printers. */
        if ((!name || !*name) && level == 1 && !(flags & PRINTER_ENUM_LOCAL))
            return InfoEnumContainers(buffer, size, needed, returned, ansi);
        for (int i = 0; i < SpoolPrinterCount() && count < 16; i++)
            indexes[count++] = i;
    } else if (flags & PRINTER_ENUM_LOCAL) {
        for (int i = 0; i < SpoolPrinterCount() && count < 16; i++)
            indexes[count++] = i;
    } else if (flags & PRINTER_ENUM_DEFAULT) {
        int d = SpoolDefaultPrinter();
        if (d >= 0)
            indexes[count++] = d;
    }
    return InfoEnumPrinters(indexes, count, level, buffer, size, needed, returned, ansi);
}

DECLSPEC_EXPORT BOOL WINAPI EnumPrintersW(DWORD flags, LPWSTR name, DWORD level, LPBYTE buffer, DWORD size,
                                                LPDWORD needed, LPDWORD returned)
{
    return SpoolEnumPrinters(flags, name, level, buffer, size, needed, returned, FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI EnumPrintersA(DWORD flags, LPSTR name, DWORD level, LPBYTE buffer, DWORD size,
                                                LPDWORD needed, LPDWORD returned)
{
    PWSTR wide = SpoolToWide(name);
    if (name && !wide)
        return SpoolFail(ERROR_NOT_ENOUGH_MEMORY);
    BOOL ok = SpoolEnumPrinters(flags, wide, level, buffer, size, needed, returned, TRUE);
    DWORD error = GetLastError();
    SpoolFree(wide);
    SetLastError(error);
    return ok;
}

static BOOL SpoolDriverLevel(DWORD level)
{
    return (level >= 1 && level <= 6) || level == 8;
}

/* a list of names of one kind in the form of the level asked for; the drivers are listed for levels 1 to 3. */
static BOOL SpoolEnumNames(DWORD kind, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed, LPDWORD returned,
                           BOOL ansi)
{
    if (buffer)
        memset(buffer, 0, size);
    *needed = 0;
    *returned = 0;
    if (kind == INFO_DRIVER && !InfoDriverLevel(level))
        return TRUE;
    return InfoEnumNames(kind, level, buffer, size, needed, returned, ansi);
}

DECLSPEC_EXPORT BOOL WINAPI EnumPrinterDriversW(LPWSTR name, LPWSTR environment, DWORD level, LPBYTE buffer,
                                                      DWORD size, LPDWORD needed, LPDWORD returned)
{
    if (!needed || !returned)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (!SpoolDriverLevel(level))
        return SpoolFail(ERROR_INVALID_LEVEL);
    if (!SpoolCheckEnvironmentW(environment))
        return FALSE;
    return SpoolEnumNames(INFO_DRIVER, level, buffer, size, needed, returned, FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI EnumPrinterDriversA(LPSTR name, LPSTR environment, DWORD level, LPBYTE buffer,
                                                      DWORD size, LPDWORD needed, LPDWORD returned)
{
    if (!needed || !returned)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (!SpoolDriverLevel(level))
        return SpoolFail(ERROR_INVALID_LEVEL);
    if (!SpoolCheckEnvironmentA(environment))
        return FALSE;
    return SpoolEnumNames(INFO_DRIVER, level, buffer, size, needed, returned, TRUE);
}

DECLSPEC_EXPORT BOOL WINAPI EnumPortsW(LPWSTR name, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                                             LPDWORD returned)
{
    if (!needed || !returned)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (level != 1 && level != 2)
        return SpoolFail(ERROR_INVALID_LEVEL);
    return SpoolEnumNames(INFO_PORT, level, buffer, size, needed, returned, FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI EnumPortsA(LPSTR name, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                                             LPDWORD returned)
{
    if (!needed || !returned)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (level != 1 && level != 2)
        return SpoolFail(ERROR_INVALID_LEVEL);
    return SpoolEnumNames(INFO_PORT, level, buffer, size, needed, returned, TRUE);
}

DECLSPEC_EXPORT BOOL WINAPI EnumMonitorsW(LPWSTR name, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                                                LPDWORD returned)
{
    if (!needed || !returned)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (level != 1 && level != 2)
        return SpoolFail(ERROR_INVALID_LEVEL);
    return SpoolEnumNames(INFO_MONITOR, level, buffer, size, needed, returned, FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI EnumMonitorsA(LPSTR name, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                                                LPDWORD returned)
{
    if (!needed || !returned)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (level != 1 && level != 2)
        return SpoolFail(ERROR_INVALID_LEVEL);
    return SpoolEnumNames(INFO_MONITOR, level, buffer, size, needed, returned, TRUE);
}

DECLSPEC_EXPORT BOOL WINAPI EnumPrintProcessorsW(LPWSTR name, LPWSTR environment, DWORD level, LPBYTE buffer,
                                                       DWORD size, LPDWORD needed, LPDWORD returned)
{
    if (!needed || !returned)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (level != 1)
        return SpoolFail(ERROR_INVALID_LEVEL);
    if (!SpoolCheckEnvironmentW(environment))
        return FALSE;
    return SpoolEnumNames(INFO_PROCESSOR, level, buffer, size, needed, returned, FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI EnumPrintProcessorsA(LPSTR name, LPSTR environment, DWORD level, LPBYTE buffer,
                                                       DWORD size, LPDWORD needed, LPDWORD returned)
{
    if (!needed || !returned)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (level != 1)
        return SpoolFail(ERROR_INVALID_LEVEL);
    if (!SpoolCheckEnvironmentA(environment))
        return FALSE;
    return SpoolEnumNames(INFO_PROCESSOR, level, buffer, size, needed, returned, TRUE);
}

/* the data types of the print processor winprint, and a failure for any other processor. */
static BOOL SpoolEnumDatatypes(LPCWSTR processor, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                               LPDWORD returned, BOOL ansi)
{
    if (!needed || !returned)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (level != 1)
        return SpoolFail(ERROR_INVALID_LEVEL);
    if (!processor || _wcsicmp(processor, L"winprint"))
        return SpoolFail(ERROR_UNKNOWN_PRINTPROCESSOR);
    return SpoolEnumNames(INFO_DATATYPE, level, buffer, size, needed, returned, ansi);
}

DECLSPEC_EXPORT BOOL WINAPI EnumPrintProcessorDatatypesW(LPWSTR name, LPWSTR processor, DWORD level,
                                                               LPBYTE buffer, DWORD size, LPDWORD needed,
                                                               LPDWORD returned)
{
    return SpoolEnumDatatypes(processor, level, buffer, size, needed, returned, FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI EnumPrintProcessorDatatypesA(LPSTR name, LPSTR processor, DWORD level,
                                                               LPBYTE buffer, DWORD size, LPDWORD needed,
                                                               LPDWORD returned)
{
    PWSTR wide = SpoolToWide(processor);
    if (processor && !wide)
        return SpoolFail(ERROR_NOT_ENOUGH_MEMORY);
    BOOL ok = SpoolEnumDatatypes(wide, level, buffer, size, needed, returned, TRUE);
    DWORD error = GetLastError();
    SpoolFree(wide);
    SetLastError(error);
    return ok;
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

DECLSPEC_EXPORT BOOL WINAPI GetPrinterDriverDirectoryW(LPWSTR name, LPWSTR environment, DWORD level,
                                                             LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return SpoolDirectoryW(environment, level, buffer, size, needed, L"DRIVERS");
}

DECLSPEC_EXPORT BOOL WINAPI GetPrinterDriverDirectoryA(LPSTR name, LPSTR environment, DWORD level,
                                                             LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return SpoolDirectoryA(environment, level, buffer, size, needed, L"DRIVERS");
}

DECLSPEC_EXPORT BOOL WINAPI GetPrintProcessorDirectoryW(LPWSTR name, LPWSTR environment, DWORD level,
                                                              LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return SpoolDirectoryW(environment, level, buffer, size, needed, L"PRTPROCS");
}

DECLSPEC_EXPORT BOOL WINAPI GetPrintProcessorDirectoryA(LPSTR name, LPSTR environment, DWORD level,
                                                              LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return SpoolDirectoryA(environment, level, buffer, size, needed, L"PRTPROCS");
}

/* ------------------------------------------------------------------ default printer */

/* the name of the default printer: the chosen one if there is one, else the first of the printers. */
DECLSPEC_EXPORT BOOL WINAPI GetDefaultPrinterW(LPWSTR buffer, LPDWORD size)
{
    if (!size)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    int index = SpoolDefaultPrinter();
    if (index < 0)
        return SpoolFail(ERROR_FILE_NOT_FOUND);
    LPCWSTR name = SpoolPrinterAt(index)->Name;
    DWORD chars = (DWORD)wcslen(name) + 1;
    if (!buffer || *size < chars) {
        *size = chars;
        return SpoolFail(ERROR_INSUFFICIENT_BUFFER);
    }
    memcpy(buffer, name, chars * sizeof(WCHAR));
    *size = chars;
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI GetDefaultPrinterA(LPSTR buffer, LPDWORD size)
{
    if (!size)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    int index = SpoolDefaultPrinter();
    if (index < 0)
        return SpoolFail(ERROR_FILE_NOT_FOUND);
    LPCWSTR name = SpoolPrinterAt(index)->Name;
    int chars = WideCharToMultiByte(CP_ACP, 0, name, -1, nullptr, 0, nullptr, nullptr);
    if (!buffer || *size < (DWORD)chars) {
        *size = (DWORD)chars;
        return SpoolFail(ERROR_INSUFFICIENT_BUFFER);
    }
    WideCharToMultiByte(CP_ACP, 0, name, -1, buffer, chars, nullptr, nullptr);
    *size = (DWORD)chars;
    return TRUE;
}

/* a null or empty name asks for a default to be picked, which the first printer is. */
DECLSPEC_EXPORT BOOL WINAPI SetDefaultPrinterW(LPCWSTR printer)
{
    int index = printer && *printer ? SpoolFindPrinter(printer) : (SpoolPrinterCount() ? 0 : -1);
    if (index < 0)
        return SpoolFail(ERROR_INVALID_PRINTER_NAME);
    return SpoolSetDefaultPrinter(index) ? TRUE : SpoolFail(ERROR_ACCESS_DENIED);
}

DECLSPEC_EXPORT BOOL WINAPI SetDefaultPrinterA(LPCSTR printer)
{
    PWSTR wide = SpoolToWide(printer);
    if (printer && !wide)
        return SpoolFail(ERROR_NOT_ENOUGH_MEMORY);
    BOOL ok = SetDefaultPrinterW(wide);
    DWORD error = GetLastError();
    SpoolFree(wide);
    SetLastError(error);
    return ok;
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
    return nullptr;
}

DECLSPEC_EXPORT HANDLE WINAPI AddPrinterW(LPWSTR name, DWORD level, LPBYTE printer)
{
    return SpoolAddPrinter(level, printer);
}

DECLSPEC_EXPORT HANDLE WINAPI AddPrinterA(LPSTR name, DWORD level, LPBYTE printer)
{
    return SpoolAddPrinter(level, printer);
}

DECLSPEC_EXPORT BOOL WINAPI AddPrinterConnectionW(LPWSTR name)
{
    return SpoolFail(ERROR_INVALID_PRINTER_NAME);
}

DECLSPEC_EXPORT BOOL WINAPI AddPrinterConnectionA(LPSTR name)
{
    return SpoolFail(ERROR_INVALID_PRINTER_NAME);
}

DECLSPEC_EXPORT BOOL WINAPI AddPrinterConnection2W(HWND window, LPCWSTR name, DWORD level, PVOID info)
{
    return SpoolFail(ERROR_INVALID_PRINTER_NAME);
}

/* the ansi form is not implemented by windows either. */
DECLSPEC_EXPORT BOOL WINAPI AddPrinterConnection2A(HWND window, LPCSTR name, DWORD level, PVOID info)
{
    return SpoolFail(ERROR_NOT_SUPPORTED);
}

DECLSPEC_EXPORT BOOL WINAPI DeletePrinterConnectionW(LPWSTR name)
{
    return SpoolFail(ERROR_INVALID_PRINTER_NAME);
}

DECLSPEC_EXPORT BOOL WINAPI DeletePrinterConnectionA(LPSTR name)
{
    return SpoolFail(ERROR_INVALID_PRINTER_NAME);
}

DECLSPEC_EXPORT HANDLE WINAPI ConnectToPrinterDlg(HWND window, DWORD flags)
{
    return nullptr;
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

DECLSPEC_EXPORT BOOL WINAPI AddPrinterDriverW(LPWSTR name, DWORD level, LPBYTE info)
{
    return SpoolAddDriver(level, info);
}

DECLSPEC_EXPORT BOOL WINAPI AddPrinterDriverA(LPSTR name, DWORD level, LPBYTE info)
{
    return SpoolAddDriver(level, info);
}

DECLSPEC_EXPORT BOOL WINAPI AddPrinterDriverExW(LPWSTR name, DWORD level, LPBYTE info, DWORD flags)
{
    return SpoolAddDriver(level, info);
}

DECLSPEC_EXPORT BOOL WINAPI AddPrinterDriverExA(LPSTR name, DWORD level, LPBYTE info, DWORD flags)
{
    return SpoolAddDriver(level, info);
}

DECLSPEC_EXPORT BOOL WINAPI DeletePrinterDriverW(LPWSTR name, LPWSTR environment, LPWSTR driver)
{
    return SpoolCheckEnvironmentW(environment) && SpoolFail(ERROR_UNKNOWN_PRINTER_DRIVER);
}

DECLSPEC_EXPORT BOOL WINAPI DeletePrinterDriverA(LPSTR name, LPSTR environment, LPSTR driver)
{
    return SpoolCheckEnvironmentA(environment) && SpoolFail(ERROR_UNKNOWN_PRINTER_DRIVER);
}

DECLSPEC_EXPORT BOOL WINAPI DeletePrinterDriverExW(LPWSTR name, LPWSTR environment, LPWSTR driver,
                                                         DWORD flags, DWORD version)
{
    return SpoolCheckEnvironmentW(environment) && SpoolFail(ERROR_UNKNOWN_PRINTER_DRIVER);
}

DECLSPEC_EXPORT BOOL WINAPI DeletePrinterDriverExA(LPSTR name, LPSTR environment, LPSTR driver, DWORD flags,
                                                         DWORD version)
{
    return SpoolCheckEnvironmentA(environment) && SpoolFail(ERROR_UNKNOWN_PRINTER_DRIVER);
}

/* ------------------------------------------------------------------ ports, monitors, processors, providers */

DECLSPEC_EXPORT BOOL WINAPI AddPortW(LPWSTR name, HWND window, LPWSTR monitor)
{
    return SpoolFail(ERROR_UNKNOWN_PRINT_MONITOR);
}

DECLSPEC_EXPORT BOOL WINAPI AddPortA(LPSTR name, HWND window, LPSTR monitor)
{
    return SpoolFail(ERROR_UNKNOWN_PRINT_MONITOR);
}

DECLSPEC_EXPORT BOOL WINAPI AddPortExW(LPWSTR name, DWORD level, LPBYTE buffer, LPWSTR monitor)
{
    return SpoolFail(ERROR_UNKNOWN_PRINT_MONITOR);
}

DECLSPEC_EXPORT BOOL WINAPI AddPortExA(LPSTR name, DWORD level, LPBYTE buffer, LPSTR monitor)
{
    return SpoolFail(ERROR_UNKNOWN_PRINT_MONITOR);
}

DECLSPEC_EXPORT BOOL WINAPI ConfigurePortW(LPWSTR name, HWND window, LPWSTR port)
{
    return SpoolFail(ERROR_UNKNOWN_PORT);
}

DECLSPEC_EXPORT BOOL WINAPI ConfigurePortA(LPSTR name, HWND window, LPSTR port)
{
    return SpoolFail(ERROR_UNKNOWN_PORT);
}

DECLSPEC_EXPORT BOOL WINAPI DeletePortW(LPWSTR name, HWND window, LPWSTR port)
{
    return SpoolFail(ERROR_UNKNOWN_PORT);
}

DECLSPEC_EXPORT BOOL WINAPI DeletePortA(LPSTR name, HWND window, LPSTR port)
{
    return SpoolFail(ERROR_UNKNOWN_PORT);
}

DECLSPEC_EXPORT BOOL WINAPI SetPortW(LPWSTR name, LPWSTR port, DWORD level, LPBYTE info)
{
    return SpoolFail(ERROR_UNKNOWN_PORT);
}

DECLSPEC_EXPORT BOOL WINAPI SetPortA(LPSTR name, LPSTR port, DWORD level, LPBYTE info)
{
    return SpoolFail(ERROR_UNKNOWN_PORT);
}

/* monitors, processors and providers name a dll that would have to be in a spool directory. */
DECLSPEC_EXPORT BOOL WINAPI AddMonitorW(LPWSTR name, DWORD level, LPBYTE info)
{
    return SpoolFail(level == 2 ? ERROR_FILE_NOT_FOUND : ERROR_INVALID_LEVEL);
}

DECLSPEC_EXPORT BOOL WINAPI AddMonitorA(LPSTR name, DWORD level, LPBYTE info)
{
    return SpoolFail(level == 2 ? ERROR_FILE_NOT_FOUND : ERROR_INVALID_LEVEL);
}

DECLSPEC_EXPORT BOOL WINAPI DeleteMonitorW(LPWSTR name, LPWSTR environment, LPWSTR monitor)
{
    return SpoolFail(ERROR_UNKNOWN_PRINT_MONITOR);
}

DECLSPEC_EXPORT BOOL WINAPI DeleteMonitorA(LPSTR name, LPSTR environment, LPSTR monitor)
{
    return SpoolFail(ERROR_UNKNOWN_PRINT_MONITOR);
}

DECLSPEC_EXPORT BOOL WINAPI AddPrintProcessorW(LPWSTR name, LPWSTR environment, LPWSTR path,
                                                     LPWSTR processor)
{
    return SpoolCheckEnvironmentW(environment) && SpoolFail(ERROR_FILE_NOT_FOUND);
}

DECLSPEC_EXPORT BOOL WINAPI AddPrintProcessorA(LPSTR name, LPSTR environment, LPSTR path, LPSTR processor)
{
    return SpoolCheckEnvironmentA(environment) && SpoolFail(ERROR_FILE_NOT_FOUND);
}

DECLSPEC_EXPORT BOOL WINAPI DeletePrintProcessorW(LPWSTR name, LPWSTR environment, LPWSTR processor)
{
    return SpoolCheckEnvironmentW(environment) && SpoolFail(ERROR_UNKNOWN_PRINTPROCESSOR);
}

DECLSPEC_EXPORT BOOL WINAPI DeletePrintProcessorA(LPSTR name, LPSTR environment, LPSTR processor)
{
    return SpoolCheckEnvironmentA(environment) && SpoolFail(ERROR_UNKNOWN_PRINTPROCESSOR);
}

DECLSPEC_EXPORT BOOL WINAPI AddPrintProvidorW(LPWSTR name, DWORD level, LPBYTE info)
{
    return SpoolFail(ERROR_FILE_NOT_FOUND);
}

DECLSPEC_EXPORT BOOL WINAPI AddPrintProvidorA(LPSTR name, DWORD level, LPBYTE info)
{
    return SpoolFail(ERROR_FILE_NOT_FOUND);
}

DECLSPEC_EXPORT BOOL WINAPI DeletePrintProvidorW(LPWSTR name, LPWSTR environment, LPWSTR provider)
{
    return SpoolFail(ERROR_FILE_NOT_FOUND);
}

DECLSPEC_EXPORT BOOL WINAPI DeletePrintProvidorA(LPSTR name, LPSTR environment, LPSTR provider)
{
    return SpoolFail(ERROR_FILE_NOT_FOUND);
}

/* ------------------------------------------------------------------ execution context */

DECLSPEC_EXPORT BOOL WINAPI GetPrintExecutionData(PRINT_EXECUTION_DATA *data)
{
    if (!data)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    data->context = PRINT_EXECUTION_CONTEXT_APPLICATION;
    data->clientAppPID = GetCurrentProcessId();
    return TRUE;
}
