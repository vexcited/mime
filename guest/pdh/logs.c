/*
 * pdh without logs, other data sources and dialogs: the functions that need them check their arguments as windows
 * does and fail the way a machine without the log reader, a database or the user interface of pdh fails. no log can be
 * open, so a handle of one is never valid. the dialogs of pdh are in pdhui.dll, which is not here.
 */
#include "pdh_private.h"

#define PDH_LOG_READ_ACCESS 0x00010000
#define PDH_LOG_WRITE_ACCESS 0x00020000
#define PDH_LOG_UPDATE_ACCESS 0x00040000
#define PDH_LOG_CREATE_NEW 1
#define PDH_LOG_CREATE_ALWAYS 2
#define PDH_LOG_OPEN_ALWAYS 3
#define PDH_LOG_OPEN_EXISTING 4

#define PDH_FILE_ALREADY_EXISTS_ PDH_ERROR(0xBD2)
#define PDH_LOG_FILE_CREATE_ERROR_ PDH_ERROR(0xBC9)

PDH_STATUS WINAPI PdhGetCounterTimeBase(PDH_HCOUNTER counter, LONGLONG *frequency);

/* ------------------------------------------------------------------ logs */

static BOOL is_sql_source(LPCWSTR name)
{
    return !_wcsnicmp(name, L"SQL:", 4);
}

PDH_STATUS WINAPI PdhOpenLogW(LPCWSTR file, DWORD access, LPDWORD type, PDH_HQUERY query, DWORD size, LPCWSTR caption,
                              PDH_HLOG *log)
{
    if (!log)
        return PDH_INVALID_ARGUMENT;
    *log = nullptr;
    if (!file || !type || !*file || wcslen(file) > 0x400 || (caption && (!*caption && FALSE)) ||
        (caption && wcslen(caption) > 0x800))
        return PDH_INVALID_ARGUMENT;
    if (is_sql_source(file))
        return PDH_INVALID_SQLDB;
    DWORD disposition = access & 7;
    if ((access & PDH_LOG_WRITE_ACCESS) && disposition != PDH_LOG_CREATE_NEW && disposition != PDH_LOG_CREATE_ALWAYS &&
        disposition != PDH_LOG_OPEN_ALWAYS && disposition != PDH_LOG_OPEN_EXISTING)
        return PDH_INVALID_ARGUMENT;
    (void)query;
    (void)size;
    DWORD attributes = GetFileAttributesW(file);
    if (access & PDH_LOG_WRITE_ACCESS) {
        /* a log is written in a binary format or as text, and neither writer is here. */
        if (attributes != INVALID_FILE_ATTRIBUTES && disposition == PDH_LOG_CREATE_NEW)
            return PDH_FILE_ALREADY_EXISTS_;
        return PDH_LOG_FILE_CREATE_ERROR_;
    }
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return PDH_FILE_NOT_FOUND;
    return PDH_UNKNOWN_LOG_FORMAT;
}

PDH_STATUS WINAPI PdhOpenLogA(LPCSTR file, DWORD access, LPDWORD type, PDH_HQUERY query, DWORD size, LPCSTR caption,
                              PDH_HLOG *log)
{
    WCHAR wide_file[1026], wide_caption[2050];
    if (!log)
        return PDH_INVALID_ARGUMENT;
    *log = nullptr;
    if (!file || PdhiWideOf(file, wide_file, 1026) || (caption && PdhiWideOf(caption, wide_caption, 2050)))
        return PDH_INVALID_ARGUMENT;
    return PdhOpenLogW(wide_file, access, type, query, size, caption ? wide_caption : nullptr, log);
}

PDH_STATUS WINAPI PdhCloseLog(PDH_HLOG log, DWORD flags)
{
    (void)log;
    (void)flags;
    return PDH_INVALID_HANDLE;
}

PDH_STATUS WINAPI PdhUpdateLogW(PDH_HLOG log, LPCWSTR text)
{
    (void)log;
    (void)text;
    return PDH_INVALID_HANDLE;
}

PDH_STATUS WINAPI PdhUpdateLogA(PDH_HLOG log, LPCSTR text)
{
    (void)log;
    (void)text;
    return PDH_INVALID_HANDLE;
}

PDH_STATUS WINAPI PdhUpdateLogFileCatalog(PDH_HLOG log)
{
    (void)log;
    return PDH_INVALID_HANDLE;
}

PDH_STATUS WINAPI PdhGetLogFileSize(PDH_HLOG log, LONGLONG *size)
{
    (void)log;
    if (!size)
        return PDH_INVALID_ARGUMENT;
    *size = 0;
    return PDH_INVALID_HANDLE;
}

PDH_STATUS WINAPI PdhGetLogFileTypeW(LPCWSTR file, LPDWORD type)
{
    if (!file || !type || !*file || wcslen(file) > 0x400)
        return PDH_INVALID_ARGUMENT;
    DWORD attributes = GetFileAttributesW(file);
    return attributes == INVALID_FILE_ATTRIBUTES ? PDH_FILE_NOT_FOUND : PDH_UNKNOWN_LOG_FORMAT;
}

PDH_STATUS WINAPI PdhGetLogSetGUID(PDH_HLOG log, GUID *guid, int *run)
{
    (void)log;
    if (guid)
        memset(guid, 0, sizeof(*guid));
    if (run)
        *run = 0;
    return guid || run ? PDH_INVALID_HANDLE : PDH_INVALID_ARGUMENT;
}

PDH_STATUS WINAPI PdhSetLogSetRunID(PDH_HLOG log, int run)
{
    (void)log;
    (void)run;
    return PDH_INVALID_HANDLE;
}

PDH_STATUS WINAPI PdhReadRawLogRecord(PDH_HLOG log, FILETIME time, PVOID record, LPDWORD size)
{
    (void)log;
    (void)time;
    (void)record;
    (void)size;
    return PDH_INVALID_HANDLE;
}

/* the logs of a data source: the live machine is not one, a file is not a set of the database kind. */
static PDH_STATUS enum_log_sets(LPCWSTR source, LPWSTR list, LPDWORD size)
{
    if (!size)
        return PDH_INVALID_ARGUMENT;
    if (*size) {
        if (!list)
            return PDH_INVALID_ARGUMENT;
        *list = 0;
        list[*size - 1] = 0;
    }
    if (!source || !*source || wcslen(source) > 0x400)
        return PDH_INVALID_ARGUMENT;
    return PDH_INVALID_SQLDB;
}

PDH_STATUS WINAPI PdhEnumLogSetNamesW(LPCWSTR source, LPWSTR list, LPDWORD size)
{
    return enum_log_sets(source, list, size);
}

PDH_STATUS WINAPI PdhEnumLogSetNamesA(LPCSTR source, LPSTR list, LPDWORD size)
{
    WCHAR wide[1026];
    if (!source || PdhiWideOf(source, wide, 1026))
        return PDH_INVALID_ARGUMENT;
    DWORD chars = 0;
    (void)list;
    PDH_STATUS status = enum_log_sets(wide, nullptr, size ? &chars : nullptr);
    return status;
}

/* the data source the live queries read; only the machine and its text log forms of windows 2000 exist. */
PDH_STATUS WINAPI PdhSetDefaultRealTimeDataSource(DWORD source)
{
    if (source != 1 && source != 4)
        return PDH_INVALID_ARGUMENT;
    return ERROR_SUCCESS;
}

PDH_STATUS WINAPI PdhBindInputDataSourceW(PDH_HLOG *handle, LPCWSTR names)
{
    if (!handle)
        return PDH_INVALID_ARGUMENT;
    *handle = nullptr;
    /* no name is the live machine, which has no handle. */
    if (!names)
        return ERROR_SUCCESS;
    if (!*names)
        return PDH_INVALID_ARGUMENT;
    if (is_sql_source(names))
        return PDH_INVALID_SQLDB;
    DWORD type = 0;
    PDH_STATUS status = PdhOpenLogW(names, PDH_LOG_READ_ACCESS | PDH_LOG_OPEN_EXISTING, &type, nullptr, 0, nullptr, handle);
    return status;
}

PDH_STATUS WINAPI PdhBindInputDataSourceA(PDH_HLOG *handle, LPCSTR names)
{
    WCHAR wide[1026];
    if (!handle)
        return PDH_INVALID_ARGUMENT;
    *handle = nullptr;
    if (!names)
        return ERROR_SUCCESS;
    if (PdhiWideOf(names, wide, 1026))
        return PDH_INVALID_ARGUMENT;
    return PdhBindInputDataSourceW(handle, wide);
}

PDH_STATUS WINAPI PdhGetDataSourceTimeRangeH(PDH_HLOG log, LPDWORD count, PDH_TIME_INFO_ *range, LPDWORD size)
{
    if (!count || !range || !size)
        return PDH_INVALID_ARGUMENT;
    return log ? PDH_INVALID_HANDLE : PDH_DATA_SOURCE_IS_REAL_TIME;
}

PDH_STATUS WINAPI PdhGetDataSourceTimeRangeW(LPCWSTR source, LPDWORD count, PDH_TIME_INFO_ *range, LPDWORD size)
{
    if (!count || !range || !size)
        return PDH_INVALID_ARGUMENT;
    if (!source)
        return PDH_DATA_SOURCE_IS_REAL_TIME;
    if (!*source || wcslen(source) > 0x400)
        return PDH_INVALID_ARGUMENT;
    DWORD type = 0;
    PDH_HLOG log;
    PDH_STATUS status = PdhOpenLogW(source, PDH_LOG_READ_ACCESS | PDH_LOG_OPEN_EXISTING, &type, nullptr, 0, nullptr, &log);
    return status;
}

PDH_STATUS WINAPI PdhGetDataSourceTimeRangeA(LPCSTR source, LPDWORD count, PDH_TIME_INFO_ *range, LPDWORD size)
{
    WCHAR wide[1026];
    if (!count || !range || !size)
        return PDH_INVALID_ARGUMENT;
    if (!source)
        return PDH_DATA_SOURCE_IS_REAL_TIME;
    if (PdhiWideOf(source, wide, 1026))
        return PDH_INVALID_ARGUMENT;
    return PdhGetDataSourceTimeRangeW(wide, count, range, size);
}

/* ------------------------------------------------------------------ relogging, which reads one log and writes another */

PDH_STATUS WINAPI PdhRelogW(PDH_HLOG log, PVOID relog_info)
{
    (void)log;
    (void)relog_info;
    return PDH_INVALID_HANDLE;
}

PDH_STATUS WINAPI PdhAddRelogCounter(PDH_HLOG log, LPCWSTR path, ULONG_PTR a3, ULONG_PTR a4, ULONG_PTR a5, ULONG_PTR a6,
                                     ULONG_PTR a7)
{
    (void)log;
    (void)path;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    (void)a7;
    return PDH_INVALID_HANDLE;
}

PDH_STATUS WINAPI PdhResetRelogCounterValues(PDH_HLOG log)
{
    (void)log;
    return PDH_INVALID_HANDLE;
}

PDH_STATUS WINAPI PdhWriteRelogSample(PDH_HLOG log, PVOID record, PVOID a3)
{
    (void)log;
    (void)record;
    (void)a3;
    return PDH_INVALID_HANDLE;
}

/* a value for a counter of a log being written; the counter must be one of a query that writes. */
PDH_STATUS WINAPI PdhSetCounterValue(PDH_HCOUNTER counter, PVOID value, LPCWSTR instance)
{
    (void)counter;
    (void)instance;
    return value ? PDH_INVALID_HANDLE : PDH_INVALID_ARGUMENT;
}

/* ------------------------------------------------------------------ databases */

static PDH_STATUS database(LPCWSTR source)
{
    if (!source || !*source || wcslen(source) > 0x400)
        return PDH_INVALID_ARGUMENT;
    return PDH_INVALID_SQLDB;
}

PDH_STATUS WINAPI PdhVerifySQLDBW(LPCWSTR source)
{
    return database(source);
}

PDH_STATUS WINAPI PdhVerifySQLDBA(LPCSTR source)
{
    WCHAR wide[1026];
    if (!source || PdhiWideOf(source, wide, 1026))
        return PDH_INVALID_ARGUMENT;
    return database(wide);
}

PDH_STATUS WINAPI PdhCreateSQLTablesW(LPCWSTR source)
{
    return database(source);
}

PDH_STATUS WINAPI PdhCreateSQLTablesA(LPCSTR source)
{
    WCHAR wide[1026];
    if (!source || PdhiWideOf(source, wide, 1026))
        return PDH_INVALID_ARGUMENT;
    return database(wide);
}

/* ------------------------------------------------------------------ dialogs, which are the part of pdhui.dll */

PDH_STATUS WINAPI PdhBrowseCountersW(PVOID config)
{
    (void)config;
    return PDH_NOT_IMPLEMENTED;
}

PDH_STATUS WINAPI PdhBrowseCountersA(PVOID config)
{
    (void)config;
    return PDH_NOT_IMPLEMENTED;
}

PDH_STATUS WINAPI PdhBrowseCountersHW(PVOID config)
{
    (void)config;
    return PDH_NOT_IMPLEMENTED;
}

PDH_STATUS WINAPI PdhBrowseCountersHA(PVOID config)
{
    (void)config;
    return PDH_NOT_IMPLEMENTED;
}

PDH_STATUS WINAPI PdhSelectDataSourceW(HWND owner, DWORD flags, LPWSTR source, LPDWORD size)
{
    (void)owner;
    (void)flags;
    (void)source;
    (void)size;
    return PDH_NOT_IMPLEMENTED;
}

PDH_STATUS WINAPI PdhSelectDataSourceA(HWND owner, DWORD flags, LPSTR source, LPDWORD size)
{
    (void)owner;
    (void)flags;
    (void)source;
    (void)size;
    return PDH_NOT_IMPLEMENTED;
}

/* ------------------------------------------------------------------ the visual basic entry points */

PDH_STATUS WINAPI PdhVbOpenQuery(PDH_HQUERY *query)
{
    if (!query)
        return PDH_INVALID_ARGUMENT;
    PDH_HQUERY opened = nullptr;
    PDH_STATUS status = PdhOpenQueryW(nullptr, 0, &opened);
    *query = status ? nullptr : opened;
    return status;
}

PDH_STATUS WINAPI PdhVbAddCounter(PDH_HQUERY query, LPCSTR path, PDH_HCOUNTER *counter)
{
    if (!counter)
        return PDH_INVALID_ARGUMENT;
    PDH_HCOUNTER added = nullptr;
    PDH_STATUS status = PDH_INVALID_ARGUMENT;
    if (path)
        status = PdhAddCounterA(query, path, 0, &added);
    *counter = status ? nullptr : added;
    return status;
}

BOOL WINAPI PdhVbIsGoodStatus(LONG status)
{
    DWORD severity = (DWORD)status & 0xC0000000;
    return status == 0 || severity == 0 || severity == 0x40000000;
}

/* the status of reading the value; visual basic gets the number itself through the floating point return it declares. */
DWORD WINAPI PdhVbGetDoubleCounterValue(PDH_HCOUNTER counter, DWORD *status)
{
    DWORD type = 0;
    PDH_FMT_COUNTERVALUE value = {};
    DWORD result = (DWORD)PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &type, &value);
    if (result == 0)
        result = value.CStatus;
    if (status)
        *status = result;
    return result;
}

PDH_STATUS WINAPI PdhVbGetLogFileSize(PDH_HLOG log, LONG *size)
{
    LONGLONG big = 0;
    if (!size)
        return PDH_INVALID_ARGUMENT;
    *size = 0;
    PDH_STATUS status = PdhGetLogFileSize(log, &big);
    if (status == 0) {
        if (big > 0x80000000LL)
            return PDH_INSUFFICIENT_BUFFER;
        *size = (LONG)(big & 0x7FFFFFFF);
    }
    return status;
}

PDH_STATUS WINAPI PdhVbOpenLog(LPCSTR file, DWORD access, LPDWORD type, PDH_HQUERY query, DWORD size, LPCSTR caption,
                               PDH_HLOG *log)
{
    return PdhOpenLogA(file, access, type, query, size, caption, log);
}

PDH_STATUS WINAPI PdhVbUpdateLog(PDH_HLOG log, LPCSTR text)
{
    return PdhUpdateLogA(log, text);
}

/* the five pieces of a path go into five buffers of the same size. */
PDH_STATUS WINAPI PdhVbGetCounterPathElements(LPCSTR path, LPSTR machine, LPSTR object, LPSTR instance, LPSTR parent,
                                              LPSTR counter, DWORD size)
{
    PDH_STATUS status = path ? ERROR_SUCCESS : PDH_INVALID_ARGUMENT;
    LPSTR buffers[5] = {machine, object, instance, parent, counter};
    for (int i = 0; i < 5; i++) {
        if (buffers[i] && size) {
            buffers[i][0] = 0;
            buffers[i][size - 1] = 0;
        } else if (!status) {
            status = PDH_INVALID_ARGUMENT;
        }
    }
    if (status)
        return status;
    BYTE storage[0x1000];
    DWORD room = sizeof(storage);
    PDH_PATH_ELEMENTS_A *elements = (PDH_PATH_ELEMENTS_A *)storage;
    status = PdhParseCounterPathA(path, elements, &room, 0);
    if (status)
        return status;
    LPCSTR texts[5] = {elements->szMachineName, elements->szObjectName, elements->szInstanceName,
                       elements->szParentInstance, elements->szCounterName};
    for (int i = 0; i < 5; i++) {
        if (!texts[i])
            continue;
        SIZE_T length = strlen(texts[i]);
        if (length + 1 > size)
            status = PDH_INSUFFICIENT_BUFFER;
        else
            memcpy(buffers[i], texts[i], length + 1);
    }
    return status;
}

/* the counter chosen in the browse dialog; there is none, so no choice is made. */
DWORD WINAPI PdhVbGetOneCounterPath(LPSTR path, DWORD size, DWORD detail, LPSTR caption)
{
    (void)detail;
    (void)caption;
    if (!size || !path)
        return 0;
    *path = 0;
    path[size - 1] = 0;
    return 0;
}

PDH_STATUS WINAPI PdhVbCreateCounterPathList(DWORD flags, LPSTR caption)
{
    (void)flags;
    (void)caption;
    return PDH_NOT_IMPLEMENTED;
}

/* the paths the dialog made, counted from one; there are none. */
DWORD WINAPI PdhVbGetCounterPathFromList(LONG index, LPSTR path, DWORD size)
{
    (void)index;
    if (path && size) {
        *path = 0;
        path[size - 1] = 0;
    }
    return 0;
}
