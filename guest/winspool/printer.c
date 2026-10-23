/* printer handles, settings, jobs and forms. */
#include "winspool_private.h"
#include "info.h"
#include <stddef.h>

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

/* operations requiring a printer reject the server handle. */
SpoolHandle *SpoolLookupPrinter(HANDLE printer)
{
    SpoolHandle *h = SpoolLookup(printer);
    if (h && h->printer < 0) {
        SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    return h;
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
    int index = -1;
    if (!SpoolIsLocalServer(name)) {
        index = SpoolFindPrinter(name);
        if (index < 0)
            return SpoolFail(ERROR_INVALID_PRINTER_NAME);
    }
    SpoolHandle *h = SpoolAlloc(sizeof(*h));
    if (!h)
        return SpoolFail(ERROR_NOT_ENOUGH_MEMORY);
    h->printer = index;
    if (index >= 0) {
        h->access = defaults && defaults->DesiredAccess ? defaults->DesiredAccess : PRINTER_ACCESS_USE;
        if (defaults && defaults->pDatatype)
            _snwprintf(h->datatype, ARRAYSIZE(h->datatype), L"%ls", defaults->pDatatype);
        /* copies valid default device modes unchanged. */
        if (defaults && defaults->pDevMode && defaults->pDevMode->dmSize >= offsetof(DEVMODEW, dmFields) + sizeof(DWORD)) {
            SIZE_T bytes = defaults->pDevMode->dmSize + defaults->pDevMode->dmDriverExtra;
            h->devmode = SpoolAlloc(bytes < sizeof(DEVMODEW) ? sizeof(DEVMODEW) : bytes);
            if (h->devmode)
                memcpy(h->devmode, defaults->pDevMode, bytes);
        }
    } else {
        h->access = defaults ? defaults->DesiredAccess : SERVER_ACCESS_ENUMERATE;
    }
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
    PWSTR datatype = nullptr;
    DEVMODEW *devmode = nullptr;
    if (defaults) {
        w.DesiredAccess = defaults->DesiredAccess;
        datatype = SpoolToWide(defaults->pDatatype);
        w.pDatatype = datatype;
        if (defaults->pDevMode) {
            devmode = SpoolDevmodeFromAnsi(defaults->pDevMode);
            w.pDevMode = devmode;
        }
    }
    BOOL ok = OpenPrinter2W(wide, printer, defaults ? &w : nullptr, nullptr);
    DWORD error = GetLastError();
    SpoolFree(wide);
    SpoolFree(datatype);
    SpoolFree(devmode);
    SetLastError(error);
    return ok;
}

/* the ANSI form is not implemented by windows either. */
DECLSPEC_EXPORT BOOL WINAPI OpenPrinter2A(LPCSTR name, LPHANDLE printer, PPRINTER_DEFAULTSA defaults,
                                                PPRINTER_OPTIONSA options)
{
    return SpoolFail(ERROR_NOT_SUPPORTED);
}

DECLSPEC_EXPORT BOOL WINAPI ClosePrinter(HANDLE printer)
{
    SpoolHandle *found = nullptr;
    AcquireSRWLockExclusive(&g_lock);
    for (SpoolHandle **link = &g_handles; *link && printer; link = &(*link)->next) {
        if (*link == printer) {
            found = *link;
            *link = (*link)->next;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!found)
        return SpoolFail(ERROR_INVALID_HANDLE);
    /* abandons any unfinished document. */
    SpoolAbortJob(found);
    SpoolFree(found->devmode);
    SpoolFree(found);
    return TRUE;
}

/* ------------------------------------------------------------------ printer state */

/* validates the handle, size pointer and level, in that order. */
static BOOL SpoolGetPrinter(HANDLE printer, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed, BOOL ansi)
{
    SpoolHandle *h = SpoolLookupPrinter(printer);
    if (!h)
        return FALSE;
    if (!needed)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (level > 9)
        return SpoolFail(ERROR_INVALID_LEVEL);
    return InfoGetPrinter(h->printer, level, buffer, size, needed, ansi);
}

DECLSPEC_EXPORT BOOL WINAPI GetPrinterW(HANDLE printer, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return SpoolGetPrinter(printer, level, buffer, size, needed, FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI GetPrinterA(HANDLE printer, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return SpoolGetPrinter(printer, level, buffer, size, needed, TRUE);
}

/*
 * level 9 saves per-user settings and level 0 accepts commands. installing or reconfiguring printers requires rights
 * unavailable here.
 */
static BOOL SpoolSetPrinter(HANDLE printer, DWORD level, LPBYTE buffer, DWORD command, BOOL ansi)
{
    SpoolHandle *h = SpoolLookupPrinter(printer);
    if (!h)
        return FALSE;
    if (level == 0 && !buffer)
        return command <= PRINTER_CONTROL_SET_STATUS ? TRUE : SpoolFail(ERROR_INVALID_PRINTER_COMMAND);
    if (level == 9 && buffer) {
        PRINTER_INFO_9W *info = (PRINTER_INFO_9W *)buffer;
        if (!info->pDevMode)
            return TRUE;
        DEVMODEW *wide = ansi ? SpoolDevmodeFromAnsi((DEVMODEA *)info->pDevMode) : info->pDevMode;
        if (wide) {
            DEVMODEW *dm = SpoolUserDevmode(h->printer);
            if (dm) {
                SpoolMergeDevmode(h->printer, dm, wide);
                SpoolStoreUserDevmode(h->printer, dm);
                SpoolFree(dm);
            }
            if (ansi)
                SpoolFree(wide);
        }
        return TRUE;
    }
    return SpoolFail(ERROR_ACCESS_DENIED);
}

DECLSPEC_EXPORT BOOL WINAPI SetPrinterW(HANDLE printer, DWORD level, LPBYTE buffer, DWORD command)
{
    return SpoolSetPrinter(printer, level, buffer, command, FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI SetPrinterA(HANDLE printer, DWORD level, LPBYTE buffer, DWORD command)
{
    return SpoolSetPrinter(printer, level, buffer, command, TRUE);
}

/* reopening with defaults keeps the same built-in printer. */
DECLSPEC_EXPORT BOOL WINAPI ResetPrinterW(HANDLE printer, LPPRINTER_DEFAULTSW defaults)
{
    SpoolHandle *h = SpoolLookupPrinter(printer);
    if (!h)
        return FALSE;
    if (defaults && defaults->pDatatype)
        _snwprintf(h->datatype, ARRAYSIZE(h->datatype), L"%ls", defaults->pDatatype);
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI ResetPrinterA(HANDLE printer, LPPRINTER_DEFAULTSA defaults)
{
    SpoolHandle *h = SpoolLookupPrinter(printer);
    if (!h)
        return FALSE;
    if (defaults && defaults->pDatatype) {
        PWSTR wide = SpoolToWide(defaults->pDatatype);
        if (wide)
            _snwprintf(h->datatype, ARRAYSIZE(h->datatype), L"%ls", wide);
        SpoolFree(wide);
    }
    return TRUE;
}

/* printers that are built in cannot be deleted. */
DECLSPEC_EXPORT BOOL WINAPI DeletePrinter(HANDLE printer)
{
    if (!SpoolLookupPrinter(printer))
        return FALSE;
    return SpoolFail(ERROR_ACCESS_DENIED);
}

/* the driver of the printer, in the environments this build has. */
static BOOL SpoolGetDriver(HANDLE printer, LPCWSTR environment, DWORD level, LPBYTE buffer, DWORD size,
                           LPDWORD needed, BOOL ansi)
{
    SpoolHandle *h = SpoolLookupPrinter(printer);
    if (!h)
        return FALSE;
    if (!needed)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (!InfoDriverLevel(level))
        return SpoolFail(ERROR_INVALID_LEVEL);
    if (environment && *environment && !SpoolEnvironmentDirectory(environment))
        return SpoolFail(ERROR_INVALID_ENVIRONMENT);
    return InfoGetDriver(h->printer, level, buffer, size, needed, ansi);
}

DECLSPEC_EXPORT BOOL WINAPI GetPrinterDriverW(HANDLE printer, LPWSTR environment, DWORD level, LPBYTE buffer,
                                                    DWORD size, LPDWORD needed)
{
    return SpoolGetDriver(printer, environment, level, buffer, size, needed, FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI GetPrinterDriverA(HANDLE printer, LPSTR environment, DWORD level, LPBYTE buffer,
                                                    DWORD size, LPDWORD needed)
{
    PWSTR wide = SpoolToWide(environment);
    BOOL ok = SpoolGetDriver(printer, wide, level, buffer, size, needed, TRUE);
    DWORD error = GetLastError();
    SpoolFree(wide);
    SetLastError(error);
    return ok;
}

DECLSPEC_EXPORT BOOL WINAPI GetPrinterDriver2W(HWND window, HANDLE printer, LPWSTR environment, DWORD level,
                                                     LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return SpoolGetDriver(printer, environment, level, buffer, size, needed, FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI GetPrinterDriver2A(HWND window, HANDLE printer, LPSTR environment, DWORD level,
                                                     LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return SpoolFail(ERROR_NOT_SUPPORTED);
}

DECLSPEC_EXPORT BOOL WINAPI PrinterProperties(HWND window, HANDLE printer)
{
    SpoolHandle *h = SpoolLookupPrinter(printer);
    if (!h)
        return FALSE;
    DEVMODEW *dm = SpoolUserDevmode(h->printer);
    if (!dm)
        return SpoolFail(ERROR_NOT_ENOUGH_MEMORY);
    BOOL accepted = SpoolPreferences(window, h->printer, dm) == IDOK;
    if (accepted)
        SpoolStoreUserDevmode(h->printer, dm);
    SpoolFree(dm);
    return accepted;
}

/* nothing changes on a printer that is built in, and the server has no printer to watch. */
DECLSPEC_EXPORT DWORD WINAPI WaitForPrinterChange(HANDLE printer, DWORD flags)
{
    SpoolLookupPrinter(printer);
    return 0;
}

DECLSPEC_EXPORT HANDLE WINAPI FindFirstPrinterChangeNotification(HANDLE printer, DWORD flags, DWORD options,
                                                                        LPVOID notify_options)
{
    if (!SpoolLookup(printer))
        return INVALID_HANDLE_VALUE;
    SetLastError(ERROR_NOT_SUPPORTED);
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

/* ------------------------------------------------------------------ jobs */

/* lists or cancels this process's documents. */
static BOOL SpoolEnumJobs(HANDLE printer, DWORD first, DWORD count, DWORD level, LPBYTE buffer, DWORD size,
                          LPDWORD needed, LPDWORD returned, BOOL ansi)
{
    SpoolHandle *h = SpoolLookupPrinter(printer);
    if (!h)
        return FALSE;
    if (!needed || !returned)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (level != 1 && level != 2)
        return SpoolFail(ERROR_INVALID_LEVEL);
    return SpoolJobsEnum(h->printer, level, first, count, buffer, size, needed, returned, ansi);
}

DECLSPEC_EXPORT BOOL WINAPI EnumJobsW(HANDLE printer, DWORD first, DWORD count, DWORD level, LPBYTE buffer,
                                            DWORD size, LPDWORD needed, LPDWORD returned)
{
    return SpoolEnumJobs(printer, first, count, level, buffer, size, needed, returned, FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI EnumJobsA(HANDLE printer, DWORD first, DWORD count, DWORD level, LPBYTE buffer,
                                            DWORD size, LPDWORD needed, LPDWORD returned)
{
    return SpoolEnumJobs(printer, first, count, level, buffer, size, needed, returned, TRUE);
}

static BOOL SpoolGetJob(HANDLE printer, DWORD job, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed, BOOL ansi)
{
    SpoolHandle *h = SpoolLookupPrinter(printer);
    if (!h)
        return FALSE;
    if (!needed)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (level != 1 && level != 2)
        return SpoolFail(ERROR_INVALID_LEVEL);
    return SpoolJobGet(h->printer, job, level, buffer, size, needed, ansi);
}

DECLSPEC_EXPORT BOOL WINAPI GetJobW(HANDLE printer, DWORD job, DWORD level, LPBYTE buffer, DWORD size,
                                          LPDWORD needed)
{
    return SpoolGetJob(printer, job, level, buffer, size, needed, FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI GetJobA(HANDLE printer, DWORD job, DWORD level, LPBYTE buffer, DWORD size,
                                          LPDWORD needed)
{
    return SpoolGetJob(printer, job, level, buffer, size, needed, TRUE);
}

DECLSPEC_EXPORT BOOL WINAPI SetJobW(HANDLE printer, DWORD job, DWORD level, LPBYTE buffer, DWORD command)
{
    SpoolHandle *h = SpoolLookupPrinter(printer);
    if (!h)
        return FALSE;
    return SpoolJobSet(h->printer, job, command);
}

DECLSPEC_EXPORT BOOL WINAPI SetJobA(HANDLE printer, DWORD job, DWORD level, LPBYTE buffer, DWORD command)
{
    return SetJobW(printer, job, level, buffer, command);
}

/* AddJob creates spool files, but no service here processes them. */
DECLSPEC_EXPORT BOOL WINAPI AddJobW(HANDLE printer, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed)
{
    if (!SpoolLookupPrinter(printer))
        return FALSE;
    return SpoolFail(ERROR_NOT_SUPPORTED);
}

DECLSPEC_EXPORT BOOL WINAPI AddJobA(HANDLE printer, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed)
{
    return AddJobW(printer, level, buffer, size, needed);
}

DECLSPEC_EXPORT BOOL WINAPI ScheduleJob(HANDLE printer, DWORD job)
{
    if (!SpoolLookupPrinter(printer))
        return FALSE;
    return SpoolFail(ERROR_INVALID_PARAMETER);
}

DECLSPEC_EXPORT BOOL WINAPI ReadPrinter(HANDLE printer, LPVOID buffer, DWORD size, LPDWORD read)
{
    if (!SpoolLookupPrinter(printer))
        return FALSE;
    return SpoolFail(ERROR_NOT_SUPPORTED);
}

DECLSPEC_EXPORT BOOL WINAPI SeekPrinter(HANDLE printer, LARGE_INTEGER distance, PLARGE_INTEGER position,
                                               DWORD method, BOOL write)
{
    if (!SpoolLookupPrinter(printer))
        return FALSE;
    return SpoolFail(ERROR_NOT_SUPPORTED);
}

/* spool file handles belong to jobs started with StartDocPrinter. */
DECLSPEC_EXPORT HANDLE WINAPI GetSpoolFileHandle(HANDLE printer)
{
    if (!SpoolLookupPrinter(printer))
        return INVALID_HANDLE_VALUE;
    SetLastError(ERROR_NOT_SUPPORTED);
    return INVALID_HANDLE_VALUE;
}

DECLSPEC_EXPORT HANDLE WINAPI CommitSpoolData(HANDLE printer, HANDLE file, DWORD size)
{
    if (!SpoolLookupPrinter(printer))
        return INVALID_HANDLE_VALUE;
    SetLastError(ERROR_NOT_SUPPORTED);
    return INVALID_HANDLE_VALUE;
}

DECLSPEC_EXPORT BOOL WINAPI CloseSpoolFileHandle(HANDLE printer, HANDLE file)
{
    if (!SpoolLookupPrinter(printer))
        return FALSE;
    return SpoolFail(ERROR_NOT_SUPPORTED);
}

/* ------------------------------------------------------------------ information contexts */

DECLSPEC_EXPORT HANDLE WINAPI CreatePrinterIC(HANDLE printer, LPDEVMODEW devmode)
{
    SpoolLookupPrinter(printer);
    if (printer && GetLastError() != ERROR_INVALID_HANDLE)
        SetLastError(ERROR_NOT_SUPPORTED);
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
                           LPDWORD returned, BOOL ansi)
{
    if (!needed || !returned)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (!SpoolFormLevel(level))
        return SpoolFail(ERROR_INVALID_LEVEL);
    if (!SpoolLookup(printer))
        return FALSE;
    if (buffer)
        memset(buffer, 0, size);
    return InfoEnumForms(level, buffer, size, needed, returned, ansi);
}

DECLSPEC_EXPORT BOOL WINAPI EnumFormsW(HANDLE printer, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                                             LPDWORD returned)
{
    return SpoolEnumForms(printer, level, buffer, size, needed, returned, FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI EnumFormsA(HANDLE printer, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                                             LPDWORD returned)
{
    return SpoolEnumForms(printer, level, buffer, size, needed, returned, TRUE);
}

static BOOL SpoolGetForm(HANDLE printer, LPCWSTR name, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                         BOOL ansi)
{
    if (!SpoolLookup(printer))
        return FALSE;
    if (!needed)
        return SpoolFail(ERROR_INVALID_PARAMETER);
    if (!SpoolFormLevel(level))
        return SpoolFail(ERROR_INVALID_LEVEL);
    const SpoolPaper *paper = name ? SpoolFindPaperByName(name) : nullptr;
    if (!paper)
        return SpoolFail(ERROR_INVALID_FORM_NAME);
    return InfoGetForm(paper, level, buffer, size, needed, ansi);
}

DECLSPEC_EXPORT BOOL WINAPI GetFormW(HANDLE printer, LPWSTR name, DWORD level, LPBYTE buffer, DWORD size,
                                           LPDWORD needed)
{
    return SpoolGetForm(printer, name, level, buffer, size, needed, FALSE);
}

DECLSPEC_EXPORT BOOL WINAPI GetFormA(HANDLE printer, LPSTR name, DWORD level, LPBYTE buffer, DWORD size,
                                           LPDWORD needed)
{
    PWSTR wide = SpoolToWide(name);
    BOOL ok = SpoolGetForm(printer, wide, level, buffer, size, needed, TRUE);
    DWORD error = GetLastError();
    SpoolFree(wide);
    SetLastError(error);
    return ok;
}

/* changing forms needs SERVER_ACCESS_ADMINISTER, and the forms are those built in. */
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
