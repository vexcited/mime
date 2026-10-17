/*
 * printer handles: opening and closing, and every function that works on an
 * open printer, a print job or a form.
 */
#include "winspool_private.h"

static SRWLOCK g_lock = SRWLOCK_INIT;
static SpoolHandle *g_handles;

SpoolHandle *SpoolLookup(HANDLE printer)
{
    SpoolHandle *found = nullptr;
    AcquireSRWLockShared(&g_lock);
    for (SpoolHandle *h = g_handles; h && printer; h = h->next) {
        if (h == printer) {
            found = h;
            break;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    if (!found)
        SetLastError(ERROR_INVALID_HANDLE);
    return found;
}

/* ------------------------------------------------------------------ opening */

/* true for the names of the local print server: none, empty, or \\ followed by this machine. */
static BOOL SpoolIsLocalServer(LPCWSTR name)
{
    if (!name || !*name)
        return TRUE;
    if (name[0] != '\\' || name[1] != '\\')
        return FALSE;
    WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(computer, &size) && !_wcsicmp(name + 2, computer))
        return TRUE;
    return !_wcsicmp(name + 2, L"localhost");
}

DECLSPEC_EXPORT BOOL WINAPI OpenPrinter2W(LPCWSTR name, LPHANDLE printer, PPRINTER_DEFAULTSW defaults,
                                                PPRINTER_OPTIONSW options)
{
    if (!printer)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    *printer = nullptr;
    if (!SpoolIsLocalServer(name))
        return SpoolFail(ERROR_INVALID_PRINTER_NAME);
    SpoolHandle *h = SpoolAlloc(sizeof(*h));
    if (!h)
        return SpoolFail(ERROR_NOT_ENOUGH_MEMORY);
    h->access = defaults ? defaults->DesiredAccess : SERVER_ACCESS_ENUMERATE;
    AcquireSRWLockExclusive(&g_lock);
    h->next = g_handles;
    g_handles = h;
    ReleaseSRWLockExclusive(&g_lock);
    *printer = h;
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI OpenPrinterW(LPWSTR name, LPHANDLE printer, LPPRINTER_DEFAULTSW defaults)
{
    return OpenPrinter2W(name, printer, defaults, nullptr);
}

DECLSPEC_EXPORT BOOL WINAPI OpenPrinterA(LPSTR name, LPHANDLE printer, LPPRINTER_DEFAULTSA defaults)
{
    PWSTR wide = SpoolToWide(name);
    if (name && !wide)
        return SpoolFail(ERROR_NOT_ENOUGH_MEMORY);
    PRINTER_DEFAULTSW w = {};
    if (defaults)
        w.DesiredAccess = defaults->DesiredAccess;
    BOOL ok = OpenPrinter2W(wide, printer, defaults ? &w : nullptr, nullptr);
    SpoolFree(wide);
    return ok;
}

/* the ansi form is not implemented by windows either. */
DECLSPEC_EXPORT BOOL WINAPI OpenPrinter2A(LPCSTR name, LPHANDLE printer, PPRINTER_DEFAULTSA defaults,
                                                PPRINTER_OPTIONSA options)
{
    return SpoolFail(ERROR_NOT_SUPPORTED);
}

DECLSPEC_EXPORT BOOL WINAPI ClosePrinter(HANDLE printer)
{
    BOOL found = FALSE;
    AcquireSRWLockExclusive(&g_lock);
    for (SpoolHandle **link = &g_handles; *link && printer; link = &(*link)->next) {
        if (*link == printer) {
            *link = (*link)->next;
            found = TRUE;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!found)
        return SpoolFail(ERROR_INVALID_HANDLE);
    SpoolFree(printer);
    return TRUE;
}

/* ------------------------------------------------------------------ printer state */

/* the server handle is not a printer: functions that need one fail on it as on any bad handle. */
static BOOL SpoolNoPrinter(HANDLE printer)
{
    return SpoolFail(ERROR_INVALID_HANDLE);
}

DECLSPEC_EXPORT BOOL WINAPI GetPrinterW(HANDLE printer, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI GetPrinterA(HANDLE printer, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI SetPrinterW(HANDLE printer, DWORD level, LPBYTE buffer, DWORD command)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI SetPrinterA(HANDLE printer, DWORD level, LPBYTE buffer, DWORD command)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI ResetPrinterW(HANDLE printer, LPPRINTER_DEFAULTSW defaults)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI ResetPrinterA(HANDLE printer, LPPRINTER_DEFAULTSA defaults)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI DeletePrinter(HANDLE printer)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI GetPrinterDriverW(HANDLE printer, LPWSTR environment, DWORD level, LPBYTE buffer,
                                                    DWORD size, LPDWORD needed)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI GetPrinterDriverA(HANDLE printer, LPSTR environment, DWORD level, LPBYTE buffer,
                                                    DWORD size, LPDWORD needed)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI GetPrinterDriver2W(HWND window, HANDLE printer, LPWSTR environment, DWORD level,
                                                     LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI GetPrinterDriver2A(HWND window, HANDLE printer, LPSTR environment, DWORD level,
                                                     LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return SpoolFail(ERROR_NOT_SUPPORTED);
}

DECLSPEC_EXPORT BOOL WINAPI PrinterProperties(HWND window, HANDLE printer)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT DWORD WINAPI WaitForPrinterChange(HANDLE printer, DWORD flags)
{
    SpoolNoPrinter(printer);
    return 0;
}

DECLSPEC_EXPORT HANDLE WINAPI FindFirstPrinterChangeNotification(HANDLE printer, DWORD flags, DWORD options,
                                                                        LPVOID notify_options)
{
    SpoolNoPrinter(printer);
    return INVALID_HANDLE_VALUE;
}

/* no change notification can be created, so no notification handle is valid. */
DECLSPEC_EXPORT BOOL WINAPI FindNextPrinterChangeNotification(HANDLE change, PDWORD changes, LPVOID options,
                                                                     LPVOID *info)
{
    return SpoolFail(ERROR_INVALID_HANDLE);
}

DECLSPEC_EXPORT BOOL WINAPI FindClosePrinterChangeNotification(HANDLE change)
{
    return SpoolFail(ERROR_INVALID_HANDLE);
}

DECLSPEC_EXPORT BOOL WINAPI FreePrinterNotifyInfo(PPRINTER_NOTIFY_INFO info)
{
    return SpoolFail(ERROR_INVALID_PARAMETER);
}

/* ------------------------------------------------------------------ documents and jobs */

DECLSPEC_EXPORT DWORD WINAPI StartDocPrinterW(HANDLE printer, DWORD level, LPBYTE info)
{
    SpoolNoPrinter(printer);
    return 0;
}

DECLSPEC_EXPORT DWORD WINAPI StartDocPrinterA(HANDLE printer, DWORD level, LPBYTE info)
{
    SpoolNoPrinter(printer);
    return 0;
}

DECLSPEC_EXPORT BOOL WINAPI StartPagePrinter(HANDLE printer)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI EndPagePrinter(HANDLE printer)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI EndDocPrinter(HANDLE printer)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI AbortPrinter(HANDLE printer)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI WritePrinter(HANDLE printer, LPVOID buffer, DWORD size, LPDWORD written)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI FlushPrinter(HANDLE printer, LPVOID buffer, DWORD size, LPDWORD written,
                                               DWORD sleep)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI ReadPrinter(HANDLE printer, LPVOID buffer, DWORD size, LPDWORD read)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI SeekPrinter(HANDLE printer, LARGE_INTEGER distance, PLARGE_INTEGER position,
                                               DWORD method, BOOL write)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI AddJobW(HANDLE printer, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI AddJobA(HANDLE printer, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI ScheduleJob(HANDLE printer, DWORD job)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI GetJobW(HANDLE printer, DWORD job, DWORD level, LPBYTE buffer, DWORD size,
                                          LPDWORD needed)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI GetJobA(HANDLE printer, DWORD job, DWORD level, LPBYTE buffer, DWORD size,
                                          LPDWORD needed)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI SetJobW(HANDLE printer, DWORD job, DWORD level, LPBYTE buffer, DWORD command)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI SetJobA(HANDLE printer, DWORD job, DWORD level, LPBYTE buffer, DWORD command)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI EnumJobsW(HANDLE printer, DWORD first, DWORD count, DWORD level, LPBYTE buffer,
                                            DWORD size, LPDWORD needed, LPDWORD returned)
{
    return SpoolNoPrinter(printer);
}

DECLSPEC_EXPORT BOOL WINAPI EnumJobsA(HANDLE printer, DWORD first, DWORD count, DWORD level, LPBYTE buffer,
                                            DWORD size, LPDWORD needed, LPDWORD returned)
{
    return SpoolNoPrinter(printer);
}

/* spool file handles belong to jobs started with StartDocPrinter. */
DECLSPEC_EXPORT HANDLE WINAPI GetSpoolFileHandle(HANDLE printer)
{
    SpoolNoPrinter(printer);
    return INVALID_HANDLE_VALUE;
}

DECLSPEC_EXPORT HANDLE WINAPI CommitSpoolData(HANDLE printer, HANDLE file, DWORD size)
{
    SpoolNoPrinter(printer);
    return INVALID_HANDLE_VALUE;
}

DECLSPEC_EXPORT BOOL WINAPI CloseSpoolFileHandle(HANDLE printer, HANDLE file)
{
    return SpoolNoPrinter(printer);
}

/* ------------------------------------------------------------------ information contexts */

DECLSPEC_EXPORT HANDLE WINAPI CreatePrinterIC(HANDLE printer, LPDEVMODEW devmode)
{
    SpoolNoPrinter(printer);
    return nullptr;
}

DECLSPEC_EXPORT BOOL WINAPI PlayGdiScriptOnPrinterIC(HANDLE ic, LPBYTE in, DWORD in_size, LPBYTE out,
                                                           DWORD out_size, DWORD reserved)
{
    return SpoolFail(ERROR_INVALID_HANDLE);
}

DECLSPEC_EXPORT BOOL WINAPI DeletePrinterIC(HANDLE ic)
{
    return SpoolFail(ERROR_INVALID_HANDLE);
}

/* ------------------------------------------------------------------ forms */

static BOOL SpoolFormLevel(DWORD level)
{
    return level == 1 || level == 2;
}

static BOOL SpoolEnumForms(HANDLE printer, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                           LPDWORD returned)
{
    if (!needed || !returned)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (!SpoolFormLevel(level))
        return SpoolFail(ERROR_INVALID_LEVEL);
    if (!SpoolLookup(printer))
        return FALSE;
    if (buffer)
        memset(buffer, 0, size);
    *needed = 0;
    *returned = 0;
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI EnumFormsW(HANDLE printer, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                                             LPDWORD returned)
{
    return SpoolEnumForms(printer, level, buffer, size, needed, returned);
}

DECLSPEC_EXPORT BOOL WINAPI EnumFormsA(HANDLE printer, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                                             LPDWORD returned)
{
    return SpoolEnumForms(printer, level, buffer, size, needed, returned);
}

static BOOL SpoolGetForm(HANDLE printer, const void *name, DWORD level, LPDWORD needed)
{
    if (!SpoolLookup(printer))
        return FALSE;
    if (!needed)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (!SpoolFormLevel(level))
        return SpoolFail(ERROR_INVALID_LEVEL);
    return SpoolFail(ERROR_INVALID_FORM_NAME);
}

DECLSPEC_EXPORT BOOL WINAPI GetFormW(HANDLE printer, LPWSTR name, DWORD level, LPBYTE buffer, DWORD size,
                                           LPDWORD needed)
{
    return SpoolGetForm(printer, name, level, needed);
}

DECLSPEC_EXPORT BOOL WINAPI GetFormA(HANDLE printer, LPSTR name, DWORD level, LPBYTE buffer, DWORD size,
                                           LPDWORD needed)
{
    return SpoolGetForm(printer, name, level, needed);
}

/* changing forms needs SERVER_ACCESS_ADMINISTER, and the form list is empty. */
static BOOL SpoolChangeForm(HANDLE printer, BOOL add)
{
    SpoolHandle *h = SpoolLookup(printer);
    if (!h)
        return FALSE;
    if (add || !(h->access & SERVER_ACCESS_ADMINISTER))
        return SpoolFail(ERROR_ACCESS_DENIED);
    return SpoolFail(ERROR_INVALID_FORM_NAME);
}

DECLSPEC_EXPORT BOOL WINAPI AddFormW(HANDLE printer, DWORD level, LPBYTE form)
{
    return SpoolChangeForm(printer, TRUE);
}

DECLSPEC_EXPORT BOOL WINAPI AddFormA(HANDLE printer, DWORD level, LPBYTE form)
{
    return SpoolChangeForm(printer, TRUE);
}

DECLSPEC_EXPORT BOOL WINAPI DeleteFormW(HANDLE printer, LPWSTR name)
{
    return SpoolChangeForm(printer, FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI DeleteFormA(HANDLE printer, LPSTR name)
{
    return SpoolChangeForm(printer, FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI SetFormW(HANDLE printer, LPWSTR name, DWORD level, LPBYTE form)
{
    return SpoolChangeForm(printer, FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI SetFormA(HANDLE printer, LPSTR name, DWORD level, LPBYTE form)
{
    return SpoolChangeForm(printer, FALSE);
}

/* ------------------------------------------------------------------ printer data */

/* the print server values a program may ask for. */
static DWORD SpoolServerData(LPCWSTR name, LPDWORD type, LPBYTE data, DWORD size, LPDWORD needed, BOOL ansi)
{
    if (!name || !needed)
        return ERROR_INVALID_PARAMETER;
    WCHAR text[MAX_PATH + 32];
    DWORD number = 0;
    BOOL is_text = TRUE;
    if (!_wcsicmp(name, L"DefaultSpoolDirectory")) {
        UINT n = GetSystemDirectoryW(text, MAX_PATH);
        if (!n || n >= MAX_PATH)
            return ERROR_FILE_NOT_FOUND;
        wcscpy(text + n, L"\\spool\\PRINTERS");
    } else if (!_wcsicmp(name, L"Architecture")) {
        wcscpy(text, SpoolNativeEnvironment());
    } else if (!_wcsicmp(name, L"MajorVersion")) {
        is_text = FALSE;
        number = 3;
    } else if (!_wcsicmp(name, L"MinorVersion")) {
        is_text = FALSE;
        number = 0;
    } else {
        return ERROR_FILE_NOT_FOUND;
    }

    if (type)
        *type = is_text ? REG_SZ : REG_DWORD;
    if (!is_text) {
        *needed = sizeof(DWORD);
        if (size < sizeof(DWORD) || !data)
            return ERROR_MORE_DATA;
        memcpy(data, &number, sizeof(DWORD));
        return ERROR_SUCCESS;
    }
    DWORD bytes = (DWORD)(wcslen(text) + 1) * sizeof(WCHAR);
    if (ansi)
        bytes = (DWORD)WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
    *needed = bytes;
    if (size < bytes || !data)
        return ERROR_MORE_DATA;
    if (ansi)
        WideCharToMultiByte(CP_ACP, 0, text, -1, (LPSTR)data, size, nullptr, nullptr);
    else
        memcpy(data, text, bytes);
    return ERROR_SUCCESS;
}

DECLSPEC_EXPORT DWORD WINAPI GetPrinterDataW(HANDLE printer, LPWSTR name, LPDWORD type, LPBYTE data,
                                                   DWORD size, LPDWORD needed)
{
    if (!SpoolLookup(printer))
        return ERROR_INVALID_HANDLE;
    return SpoolServerData(name, type, data, size, needed, FALSE);
}

DECLSPEC_EXPORT DWORD WINAPI GetPrinterDataA(HANDLE printer, LPSTR name, LPDWORD type, LPBYTE data,
                                                   DWORD size, LPDWORD needed)
{
    if (!SpoolLookup(printer))
        return ERROR_INVALID_HANDLE;
    PWSTR wide = SpoolToWide(name);
    if (name && !wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD error = SpoolServerData(wide, type, data, size, needed, TRUE);
    SpoolFree(wide);
    return error;
}

/* server data lives under the empty key; GetPrinterDataEx reads the same values there. */
DECLSPEC_EXPORT DWORD WINAPI GetPrinterDataExW(HANDLE printer, LPCWSTR key, LPCWSTR name, LPDWORD type,
                                                     LPBYTE data, DWORD size, LPDWORD needed)
{
    if (!SpoolLookup(printer))
        return ERROR_INVALID_HANDLE;
    if (key && *key)
        return ERROR_FILE_NOT_FOUND;
    return SpoolServerData(name, type, data, size, needed, FALSE);
}

DECLSPEC_EXPORT DWORD WINAPI GetPrinterDataExA(HANDLE printer, LPCSTR key, LPCSTR name, LPDWORD type,
                                                     LPBYTE data, DWORD size, LPDWORD needed)
{
    if (!SpoolLookup(printer))
        return ERROR_INVALID_HANDLE;
    if (key && *key)
        return ERROR_FILE_NOT_FOUND;
    PWSTR wide = SpoolToWide(name);
    if (name && !wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD error = SpoolServerData(wide, type, data, size, needed, TRUE);
    SpoolFree(wide);
    return error;
}

/* writing or enumerating data needs a printer, or administrator access to the server. */
static DWORD SpoolChangeData(HANDLE printer)
{
    SpoolHandle *h = SpoolLookup(printer);
    if (!h)
        return ERROR_INVALID_HANDLE;
    return h->access & SERVER_ACCESS_ADMINISTER ? ERROR_NOT_SUPPORTED : ERROR_ACCESS_DENIED;
}

DECLSPEC_EXPORT DWORD WINAPI SetPrinterDataW(HANDLE printer, LPWSTR name, DWORD type, LPBYTE data, DWORD size)
{
    return SpoolChangeData(printer);
}

DECLSPEC_EXPORT DWORD WINAPI SetPrinterDataA(HANDLE printer, LPSTR name, DWORD type, LPBYTE data, DWORD size)
{
    return SpoolChangeData(printer);
}

DECLSPEC_EXPORT DWORD WINAPI SetPrinterDataExW(HANDLE printer, LPCWSTR key, LPCWSTR name, DWORD type,
                                                     LPBYTE data, DWORD size)
{
    return SpoolChangeData(printer);
}

DECLSPEC_EXPORT DWORD WINAPI SetPrinterDataExA(HANDLE printer, LPCSTR key, LPCSTR name, DWORD type,
                                                     LPBYTE data, DWORD size)
{
    return SpoolChangeData(printer);
}

DECLSPEC_EXPORT DWORD WINAPI DeletePrinterDataW(HANDLE printer, LPWSTR name)
{
    return SpoolChangeData(printer);
}

DECLSPEC_EXPORT DWORD WINAPI DeletePrinterDataA(HANDLE printer, LPSTR name)
{
    return SpoolChangeData(printer);
}

DECLSPEC_EXPORT DWORD WINAPI DeletePrinterDataExW(HANDLE printer, LPCWSTR key, LPCWSTR name)
{
    return SpoolChangeData(printer);
}

DECLSPEC_EXPORT DWORD WINAPI DeletePrinterDataExA(HANDLE printer, LPCSTR key, LPCSTR name)
{
    return SpoolChangeData(printer);
}

DECLSPEC_EXPORT DWORD WINAPI DeletePrinterKeyW(HANDLE printer, LPCWSTR key)
{
    return SpoolChangeData(printer);
}

DECLSPEC_EXPORT DWORD WINAPI DeletePrinterKeyA(HANDLE printer, LPCSTR key)
{
    return SpoolChangeData(printer);
}

DECLSPEC_EXPORT DWORD WINAPI EnumPrinterDataW(HANDLE printer, DWORD index, LPWSTR name, DWORD name_size,
                                                    LPDWORD name_needed, LPDWORD type, LPBYTE data, DWORD data_size,
                                                    LPDWORD data_needed)
{
    return SpoolLookup(printer) ? ERROR_NO_MORE_ITEMS : ERROR_INVALID_HANDLE;
}

DECLSPEC_EXPORT DWORD WINAPI EnumPrinterDataA(HANDLE printer, DWORD index, LPSTR name, DWORD name_size,
                                                    LPDWORD name_needed, LPDWORD type, LPBYTE data, DWORD data_size,
                                                    LPDWORD data_needed)
{
    return SpoolLookup(printer) ? ERROR_NO_MORE_ITEMS : ERROR_INVALID_HANDLE;
}

DECLSPEC_EXPORT DWORD WINAPI EnumPrinterDataExW(HANDLE printer, LPCWSTR key, LPBYTE buffer, DWORD size,
                                                      LPDWORD needed, LPDWORD returned)
{
    return SpoolLookup(printer) ? ERROR_FILE_NOT_FOUND : ERROR_INVALID_HANDLE;
}

DECLSPEC_EXPORT DWORD WINAPI EnumPrinterDataExA(HANDLE printer, LPCSTR key, LPBYTE buffer, DWORD size,
                                                      LPDWORD needed, LPDWORD returned)
{
    return SpoolLookup(printer) ? ERROR_FILE_NOT_FOUND : ERROR_INVALID_HANDLE;
}

DECLSPEC_EXPORT DWORD WINAPI EnumPrinterKeyW(HANDLE printer, LPCWSTR key, LPWSTR subkeys, DWORD size,
                                                   LPDWORD needed)
{
    return SpoolLookup(printer) ? ERROR_FILE_NOT_FOUND : ERROR_INVALID_HANDLE;
}

DECLSPEC_EXPORT DWORD WINAPI EnumPrinterKeyA(HANDLE printer, LPCSTR key, LPSTR subkeys, DWORD size,
                                                   LPDWORD needed)
{
    return SpoolLookup(printer) ? ERROR_FILE_NOT_FOUND : ERROR_INVALID_HANDLE;
}

DECLSPEC_EXPORT BOOL WINAPI XcvDataW(HANDLE xcv, PCWSTR name, PBYTE in, DWORD in_size, PBYTE out,
                                           DWORD out_size, PDWORD needed, PDWORD status)
{
    return SpoolFail(ERROR_INVALID_HANDLE);
}
