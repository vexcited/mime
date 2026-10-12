/*
 * spooler functions added since windows vista: driver packages, core drivers,
 * named job properties and print workflow helpers.
 */
#include "winspool_private.h"

/* the environment check of the package functions, as an hresult. */
static HRESULT SpoolEnvironmentResultW(LPCWSTR environment)
{
    return SpoolEnvironmentDirectory(environment) ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_ENVIRONMENT);
}

static HRESULT SpoolEnvironmentResultA(LPCSTR environment)
{
    if (!environment)
        return S_OK;
    PWSTR wide = SpoolToWide(environment);
    if (!wide)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);
    HRESULT hr = SpoolEnvironmentResultW(wide);
    SpoolFree(wide);
    return hr;
}

/* ------------------------------------------------------------------ core drivers */

__declspec(dllexport) HRESULT WINAPI CorePrinterDriverInstalledW(LPCWSTR server, LPCWSTR environment, GUID driver,
                                                                 FILETIME date, DWORDLONG version, BOOL *installed)
{
    if (!installed)
        return E_INVALIDARG;
    HRESULT hr = SpoolEnvironmentResultW(environment);
    if (SUCCEEDED(hr))
        *installed = FALSE;
    return hr;
}

__declspec(dllexport) HRESULT WINAPI CorePrinterDriverInstalledA(LPCSTR server, LPCSTR environment, GUID driver,
                                                                 FILETIME date, DWORDLONG version, BOOL *installed)
{
    if (!installed)
        return E_INVALIDARG;
    HRESULT hr = SpoolEnvironmentResultA(environment);
    if (SUCCEEDED(hr))
        *installed = FALSE;
    return hr;
}

static HRESULT SpoolGetCoreDrivers(const void *keys, DWORD count, PVOID drivers)
{
    if (!count)
        return S_OK;
    if (!keys || !drivers)
        return E_INVALIDARG;
    return HRESULT_FROM_WIN32(ERROR_UNKNOWN_PRINTER_DRIVER);
}

__declspec(dllexport) HRESULT WINAPI GetCorePrinterDriversW(LPCWSTR server, LPCWSTR environment, LPCWSTR keys,
                                                            DWORD count, PVOID drivers)
{
    HRESULT hr = SpoolEnvironmentResultW(environment);
    return SUCCEEDED(hr) ? SpoolGetCoreDrivers(keys, count, drivers) : hr;
}

__declspec(dllexport) HRESULT WINAPI GetCorePrinterDriversA(LPCSTR server, LPCSTR environment, LPCSTR keys,
                                                            DWORD count, PVOID drivers)
{
    HRESULT hr = SpoolEnvironmentResultA(environment);
    return SUCCEEDED(hr) ? SpoolGetCoreDrivers(keys, count, drivers) : hr;
}

/* ------------------------------------------------------------------ driver packages */

/* the arguments are checked before the environment. */
static BOOL SpoolPackagePathArgs(const void *package, const void *cab, DWORD cab_size, LPDWORD required)
{
    return package && required && (!cab_size || cab);
}

__declspec(dllexport) HRESULT WINAPI GetPrinterDriverPackagePathW(LPCWSTR server, LPCWSTR environment,
                                                                  LPCWSTR language, LPCWSTR package, LPWSTR cab,
                                                                  DWORD cab_size, LPDWORD required)
{
    if (!SpoolPackagePathArgs(package, cab, cab_size, required))
        return E_INVALIDARG;
    HRESULT hr = SpoolEnvironmentResultW(environment);
    return SUCCEEDED(hr) ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : hr;
}

__declspec(dllexport) HRESULT WINAPI GetPrinterDriverPackagePathA(LPCSTR server, LPCSTR environment,
                                                                  LPCSTR language, LPCSTR package, LPSTR cab,
                                                                  DWORD cab_size, LPDWORD required)
{
    if (!SpoolPackagePathArgs(package, cab, cab_size, required))
        return E_INVALIDARG;
    HRESULT hr = SpoolEnvironmentResultA(environment);
    return SUCCEEDED(hr) ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : hr;
}

/* every package operation names an inf file; none can be found in the empty driver store. */
__declspec(dllexport) HRESULT WINAPI UploadPrinterDriverPackageW(LPCWSTR server, LPCWSTR inf, LPCWSTR environment,
                                                                 DWORD flags, HWND window, LPWSTR destination,
                                                                 PULONG destination_size)
{
    return inf ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : E_INVALIDARG;
}

__declspec(dllexport) HRESULT WINAPI UploadPrinterDriverPackageA(LPCSTR server, LPCSTR inf, LPCSTR environment,
                                                                 DWORD flags, HWND window, LPSTR destination,
                                                                 PULONG destination_size)
{
    return inf ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : E_INVALIDARG;
}

__declspec(dllexport) HRESULT WINAPI InstallPrinterDriverFromPackageW(LPCWSTR server, LPCWSTR inf, LPCWSTR driver,
                                                                      LPCWSTR environment, DWORD flags)
{
    return driver ? HRESULT_FROM_WIN32(ERROR_UNKNOWN_PRINTER_DRIVER) : E_INVALIDARG;
}

__declspec(dllexport) HRESULT WINAPI InstallPrinterDriverFromPackageA(LPCSTR server, LPCSTR inf, LPCSTR driver,
                                                                      LPCSTR environment, DWORD flags)
{
    return driver ? HRESULT_FROM_WIN32(ERROR_UNKNOWN_PRINTER_DRIVER) : E_INVALIDARG;
}

__declspec(dllexport) HRESULT WINAPI DeletePrinterDriverPackageW(LPCWSTR server, LPCWSTR inf, LPCWSTR environment)
{
    return inf ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : E_INVALIDARG;
}

__declspec(dllexport) HRESULT WINAPI DeletePrinterDriverPackageA(LPCSTR server, LPCSTR inf, LPCSTR environment)
{
    return inf ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : E_INVALIDARG;
}

/* ------------------------------------------------------------------ named job properties */

/* jobs only exist on printers, so these all fail on the handle. */
__declspec(dllexport) DWORD WINAPI GetJobNamedPropertyValue(HANDLE printer, DWORD job, PCWSTR name, PVOID value)
{
    return ERROR_INVALID_HANDLE;
}

__declspec(dllexport) DWORD WINAPI SetJobNamedProperty(HANDLE printer, DWORD job, const void *property)
{
    return ERROR_INVALID_HANDLE;
}

__declspec(dllexport) DWORD WINAPI DeleteJobNamedProperty(HANDLE printer, DWORD job, PCWSTR name)
{
    return ERROR_INVALID_HANDLE;
}

__declspec(dllexport) DWORD WINAPI EnumJobNamedProperties(HANDLE printer, DWORD job, DWORD *count, PVOID *properties)
{
    return ERROR_INVALID_HANDLE;
}

/* EnumJobNamedProperties never returns an array, so there is nothing to free. */
__declspec(dllexport) void WINAPI FreePrintNamedPropertyArray(DWORD count, PVOID *properties)
{
}

/* a PrintPropertyValue: the type, then a union whose string or buffer the spooler allocated. */
typedef struct SpoolPropertyValue {
    DWORD type;
    union {
        PWSTR string;
        struct {
            DWORD size;
            PVOID buffer;
        } blob;
    };
} SpoolPropertyValue;

#define SPOOL_PROPERTY_STRING 1
#define SPOOL_PROPERTY_BUFFER 10

__declspec(dllexport) void WINAPI FreePrintPropertyValue(SpoolPropertyValue *value)
{
    if (!value)
        return;
    if (value->type == SPOOL_PROPERTY_STRING) {
        LocalFree(value->string);
        value->string = NULL;
    } else if (value->type == SPOOL_PROPERTY_BUFFER) {
        LocalFree(value->blob.buffer);
        value->blob.buffer = NULL;
    }
}

/* ------------------------------------------------------------------ print workflow */

__declspec(dllexport) HRESULT WINAPI GetPrintOutputInfo(HWND window, PCWSTR printer, HANDLE *file, PWSTR *port)
{
    if (!file || !port)
        return E_INVALIDARG;
    return HRESULT_FROM_WIN32(ERROR_INVALID_PRINTER_NAME);
}

__declspec(dllexport) HRESULT WINAPI ReportJobProcessingProgress(HANDLE printer, ULONG job, int operation,
                                                                 int progress)
{
    return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
}

__declspec(dllexport) DWORD WINAPI PrinterMessageBoxW(HANDLE printer, DWORD error, HWND window, LPWSTR text,
                                                      LPWSTR caption, DWORD type)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return 0;
}

__declspec(dllexport) DWORD WINAPI PrinterMessageBoxA(HANDLE printer, DWORD error, HWND window, LPSTR text,
                                                      LPSTR caption, DWORD type)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return 0;
}

__declspec(dllexport) LPWSTR WINAPI StartDocDlgW(HANDLE printer, DOC_INFO_1W *info)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return NULL;
}

__declspec(dllexport) LPSTR WINAPI StartDocDlgA(HANDLE printer, DOC_INFO_1A *info)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return NULL;
}
