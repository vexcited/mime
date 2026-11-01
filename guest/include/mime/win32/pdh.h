/* performance data helper: pdh.h and pdhmsg.h. */
#ifndef MIME_WIN32_PDH_H
#define MIME_WIN32_PDH_H

#include <mime/win32/winbase.h>

#ifndef PDH_FUNCTION
#define PDH_FUNCTION DECLSPEC_IMPORT LONG WINAPI
#endif

typedef LONG PDH_STATUS;
typedef HANDLE PDH_HQUERY, PDH_HCOUNTER, PDH_HLOG;

#define PDH_CSTATUS_VALID_DATA ((PDH_STATUS)0x00000000L)
#define PDH_CSTATUS_NEW_DATA ((PDH_STATUS)0x00000001L)
#define PDH_CSTATUS_NO_MACHINE ((PDH_STATUS)0x800007D0L)
#define PDH_CSTATUS_NO_INSTANCE ((PDH_STATUS)0x800007D1L)
#define PDH_MORE_DATA ((PDH_STATUS)0x800007D2L)
#define PDH_CSTATUS_ITEM_NOT_VALIDATED ((PDH_STATUS)0x800007D3L)
#define PDH_NO_DATA ((PDH_STATUS)0x800007D5L)
#define PDH_CALC_NEGATIVE_DENOMINATOR ((PDH_STATUS)0x800007D6L)
#define PDH_CALC_NEGATIVE_TIMEBASE ((PDH_STATUS)0x800007D7L)
#define PDH_CALC_NEGATIVE_VALUE ((PDH_STATUS)0x800007D8L)
#define PDH_CSTATUS_NO_OBJECT ((PDH_STATUS)0xC0000BB8L)
#define PDH_CSTATUS_NO_COUNTER ((PDH_STATUS)0xC0000BB9L)
#define PDH_CSTATUS_INVALID_DATA ((PDH_STATUS)0xC0000BBAL)
#define PDH_MEMORY_ALLOCATION_FAILURE ((PDH_STATUS)0xC0000BBBL)
#define PDH_INVALID_HANDLE ((PDH_STATUS)0xC0000BBCL)
#define PDH_INVALID_ARGUMENT ((PDH_STATUS)0xC0000BBDL)
#define PDH_CSTATUS_BAD_COUNTERNAME ((PDH_STATUS)0xC0000BC0L)
#define PDH_INSUFFICIENT_BUFFER ((PDH_STATUS)0xC0000BC2L)
#define PDH_CANNOT_CONNECT_MACHINE ((PDH_STATUS)0xC0000BC3L)
#define PDH_INVALID_PATH ((PDH_STATUS)0xC0000BC4L)
#define PDH_INVALID_INSTANCE ((PDH_STATUS)0xC0000BC5L)
#define PDH_INVALID_DATA ((PDH_STATUS)0xC0000BC6L)
#define PDH_DATA_SOURCE_IS_REAL_TIME ((PDH_STATUS)0xC0000BCFL)
#define PDH_FILE_NOT_FOUND ((PDH_STATUS)0xC0000BD1L)
#define PDH_NOT_IMPLEMENTED ((PDH_STATUS)0xC0000BD3L)
#define PDH_STRING_NOT_FOUND ((PDH_STATUS)0xC0000BD4L)
#define PDH_UNKNOWN_LOG_FORMAT ((PDH_STATUS)0xC0000BD6L)
#define PDH_INVALID_SQLDB ((PDH_STATUS)0xC0000BDEL)

#define PDH_FMT_RAW 0x00000010
#define PDH_FMT_ANSI 0x00000020
#define PDH_FMT_UNICODE 0x00000040
#define PDH_FMT_LONG 0x00000100
#define PDH_FMT_DOUBLE 0x00000200
#define PDH_FMT_LARGE 0x00000400
#define PDH_FMT_NOSCALE 0x00001000
#define PDH_FMT_1000 0x00002000
#define PDH_FMT_NODATA 0x00004000
#define PDH_FMT_NOCAP100 0x00008000

#define PERF_DETAIL_NOVICE 100
#define PERF_DETAIL_ADVANCED 200
#define PERF_DETAIL_EXPERT 300
#define PERF_DETAIL_WIZARD 400
#define PDH_NOEXPANDCOUNTERS 1
#define PDH_NOEXPANDINSTANCES 2
#define PDH_REFRESHCOUNTERS 4
#define PDH_LOG_READ_ACCESS 0x00010000
#define PDH_LOG_WRITE_ACCESS 0x00020000
#define PDH_LOG_UPDATE_ACCESS 0x00040000
#define PDH_LOG_OPT_OPEN_EXISTING 4
#define PDH_MAX_COUNTER_PATH 2048

#define PERF_100NSEC_TIMER 0x20510500
#define PERF_100NSEC_TIMER_INV 0x21510500

typedef struct _PDH_FMT_COUNTERVALUE {
    DWORD CStatus;
    union {
        LONG longValue;
        double doubleValue;
        LONGLONG largeValue;
        LPCSTR AnsiStringValue;
        LPCWSTR WideStringValue;
    };
} PDH_FMT_COUNTERVALUE, *PPDH_FMT_COUNTERVALUE;

typedef struct _PDH_RAW_COUNTER {
    DWORD CStatus;
    FILETIME TimeStamp;
    LONGLONG FirstValue;
    LONGLONG SecondValue;
    DWORD MultiCount;
} PDH_RAW_COUNTER, *PPDH_RAW_COUNTER;

typedef struct _PDH_COUNTER_PATH_ELEMENTS_W {
    LPWSTR szMachineName;
    LPWSTR szObjectName;
    LPWSTR szInstanceName;
    LPWSTR szParentInstance;
    DWORD dwInstanceIndex;
    LPWSTR szCounterName;
} PDH_COUNTER_PATH_ELEMENTS_W, *PPDH_COUNTER_PATH_ELEMENTS_W;

typedef struct _PDH_COUNTER_PATH_ELEMENTS_A {
    LPSTR szMachineName;
    LPSTR szObjectName;
    LPSTR szInstanceName;
    LPSTR szParentInstance;
    DWORD dwInstanceIndex;
    LPSTR szCounterName;
} PDH_COUNTER_PATH_ELEMENTS_A, *PPDH_COUNTER_PATH_ELEMENTS_A;

typedef struct _PDH_COUNTER_INFO_W {
    DWORD dwLength;
    DWORD dwType;
    DWORD CVersion;
    DWORD CStatus;
    LONG lScale;
    LONG lDefaultScale;
    DWORD_PTR dwUserData;
    DWORD_PTR dwQueryUserData;
    LPWSTR szFullPath;
    PDH_COUNTER_PATH_ELEMENTS_W CounterPath;
    LPWSTR szExplainText;
    DWORD DataBuffer[1];
} PDH_COUNTER_INFO_W, *PPDH_COUNTER_INFO_W;

typedef struct _PDH_COUNTER_INFO_A {
    DWORD dwLength;
    DWORD dwType;
    DWORD CVersion;
    DWORD CStatus;
    LONG lScale;
    LONG lDefaultScale;
    DWORD_PTR dwUserData;
    DWORD_PTR dwQueryUserData;
    LPSTR szFullPath;
    PDH_COUNTER_PATH_ELEMENTS_A CounterPath;
    LPSTR szExplainText;
    DWORD DataBuffer[1];
} PDH_COUNTER_INFO_A, *PPDH_COUNTER_INFO_A;

typedef struct _PDH_RAW_COUNTER_ITEM_W {
    LPWSTR szName;
    PDH_RAW_COUNTER RawValue;
} PDH_RAW_COUNTER_ITEM_W, *PPDH_RAW_COUNTER_ITEM_W;

typedef struct _PDH_RAW_COUNTER_ITEM_A {
    LPSTR szName;
    PDH_RAW_COUNTER RawValue;
} PDH_RAW_COUNTER_ITEM_A, *PPDH_RAW_COUNTER_ITEM_A;

typedef struct _PDH_FMT_COUNTERVALUE_ITEM_W {
    LPWSTR szName;
    PDH_FMT_COUNTERVALUE FmtValue;
} PDH_FMT_COUNTERVALUE_ITEM_W, *PPDH_FMT_COUNTERVALUE_ITEM_W;

typedef struct _PDH_FMT_COUNTERVALUE_ITEM_A {
    LPSTR szName;
    PDH_FMT_COUNTERVALUE FmtValue;
} PDH_FMT_COUNTERVALUE_ITEM_A, *PPDH_FMT_COUNTERVALUE_ITEM_A;

typedef struct _PDH_STATISTICS {
    DWORD dwFormat;
    DWORD count;
    PDH_FMT_COUNTERVALUE min;
    PDH_FMT_COUNTERVALUE max;
    PDH_FMT_COUNTERVALUE mean;
} PDH_STATISTICS, *PPDH_STATISTICS;

typedef struct _PDH_TIME_INFO {
    LONGLONG StartTime;
    LONGLONG EndTime;
    DWORD SampleCount;
} PDH_TIME_INFO, *PPDH_TIME_INFO;

PDH_FUNCTION PdhOpenQueryW(LPCWSTR source, DWORD_PTR user, PDH_HQUERY *query);
PDH_FUNCTION PdhOpenQueryA(LPCSTR source, DWORD_PTR user, PDH_HQUERY *query);
PDH_FUNCTION PdhOpenQueryH(PDH_HLOG source, DWORD_PTR user, PDH_HQUERY *query);
PDH_FUNCTION PdhAddCounterW(PDH_HQUERY query, LPCWSTR path, DWORD_PTR user, PDH_HCOUNTER *counter);
PDH_FUNCTION PdhAddCounterA(PDH_HQUERY query, LPCSTR path, DWORD_PTR user, PDH_HCOUNTER *counter);
PDH_FUNCTION PdhAddEnglishCounterW(PDH_HQUERY query, LPCWSTR path, DWORD_PTR user, PDH_HCOUNTER *counter);
PDH_FUNCTION PdhAddEnglishCounterA(PDH_HQUERY query, LPCSTR path, DWORD_PTR user, PDH_HCOUNTER *counter);
PDH_FUNCTION PdhCollectQueryData(PDH_HQUERY query);
PDH_FUNCTION PdhCollectQueryDataWithTime(PDH_HQUERY query, LONGLONG *timestamp);
PDH_FUNCTION PdhCollectQueryDataEx(PDH_HQUERY query, DWORD interval, HANDLE event);
PDH_FUNCTION PdhSetQueryTimeRange(PDH_HQUERY query, PDH_TIME_INFO *info);
PDH_FUNCTION PdhGetFormattedCounterValue(PDH_HCOUNTER counter, DWORD format, LPDWORD type,
                                         PPDH_FMT_COUNTERVALUE value);
PDH_FUNCTION PdhGetRawCounterValue(PDH_HCOUNTER counter, LPDWORD type, PPDH_RAW_COUNTER value);
PDH_FUNCTION PdhFormatFromRawValue(DWORD type, DWORD format, LONGLONG *time_base, PPDH_RAW_COUNTER current,
                                   PPDH_RAW_COUNTER previous, PPDH_FMT_COUNTERVALUE value);
PDH_FUNCTION PdhRemoveCounter(PDH_HCOUNTER counter);
PDH_FUNCTION PdhCloseQuery(PDH_HQUERY query);
PDH_FUNCTION PdhValidatePathW(LPCWSTR path);
PDH_FUNCTION PdhValidatePathA(LPCSTR path);
PDH_FUNCTION PdhValidatePathExW(PVOID source, LPCWSTR path);
PDH_FUNCTION PdhValidatePathExA(PVOID source, LPCSTR path);
PDH_FUNCTION PdhGetDllVersion(LPDWORD version);
PDH_FUNCTION PdhSetCounterScaleFactor(PDH_HCOUNTER counter, LONG factor);
PDH_FUNCTION PdhGetCounterTimeBase(PDH_HCOUNTER counter, LONGLONG *frequency);
PDH_FUNCTION PdhGetCounterInfoW(PDH_HCOUNTER counter, BOOLEAN explain, LPDWORD size, PPDH_COUNTER_INFO_W buffer);
PDH_FUNCTION PdhGetCounterInfoA(PDH_HCOUNTER counter, BOOLEAN explain, LPDWORD size, PPDH_COUNTER_INFO_A buffer);
PDH_FUNCTION PdhGetFormattedCounterArrayW(PDH_HCOUNTER counter, DWORD format, LPDWORD size, LPDWORD count,
                                          PPDH_FMT_COUNTERVALUE_ITEM_W buffer);
PDH_FUNCTION PdhGetFormattedCounterArrayA(PDH_HCOUNTER counter, DWORD format, LPDWORD size, LPDWORD count,
                                          PPDH_FMT_COUNTERVALUE_ITEM_A buffer);
PDH_FUNCTION PdhGetRawCounterArrayW(PDH_HCOUNTER counter, LPDWORD size, LPDWORD count, PPDH_RAW_COUNTER_ITEM_W buffer);
PDH_FUNCTION PdhGetRawCounterArrayA(PDH_HCOUNTER counter, LPDWORD size, LPDWORD count, PPDH_RAW_COUNTER_ITEM_A buffer);
PDH_FUNCTION PdhCalculateCounterFromRawValue(PDH_HCOUNTER counter, DWORD format, PPDH_RAW_COUNTER current,
                                             PPDH_RAW_COUNTER previous, PPDH_FMT_COUNTERVALUE value);
PDH_FUNCTION PdhComputeCounterStatistics(PDH_HCOUNTER counter, DWORD format, DWORD first, DWORD count,
                                         PPDH_RAW_COUNTER samples, PPDH_STATISTICS statistics);
PDH_FUNCTION PdhEnumObjectsW(LPCWSTR source, LPCWSTR machine, LPWSTR list, LPDWORD size, DWORD detail, BOOL refresh);
PDH_FUNCTION PdhEnumObjectsA(LPCSTR source, LPCSTR machine, LPSTR list, LPDWORD size, DWORD detail, BOOL refresh);
PDH_FUNCTION PdhEnumObjectsHW(PDH_HLOG log, LPCWSTR machine, LPWSTR list, LPDWORD size, DWORD detail, BOOL refresh);
PDH_FUNCTION PdhEnumObjectsHA(PDH_HLOG log, LPCSTR machine, LPSTR list, LPDWORD size, DWORD detail, BOOL refresh);
PDH_FUNCTION PdhEnumObjectItemsW(LPCWSTR source, LPCWSTR machine, LPCWSTR object, LPWSTR counters, LPDWORD counter_size,
                                 LPWSTR instances, LPDWORD instance_size, DWORD detail, DWORD flags);
PDH_FUNCTION PdhEnumObjectItemsA(LPCSTR source, LPCSTR machine, LPCSTR object, LPSTR counters, LPDWORD counter_size,
                                 LPSTR instances, LPDWORD instance_size, DWORD detail, DWORD flags);
PDH_FUNCTION PdhEnumObjectItemsHW(PDH_HLOG log, LPCWSTR machine, LPCWSTR object, LPWSTR counters, LPDWORD counter_size,
                                  LPWSTR instances, LPDWORD instance_size, DWORD detail, DWORD flags);
PDH_FUNCTION PdhEnumObjectItemsHA(PDH_HLOG log, LPCSTR machine, LPCSTR object, LPSTR counters, LPDWORD counter_size,
                                  LPSTR instances, LPDWORD instance_size, DWORD detail, DWORD flags);
PDH_FUNCTION PdhEnumMachinesW(LPCWSTR source, LPWSTR list, LPDWORD size);
PDH_FUNCTION PdhEnumMachinesA(LPCSTR source, LPSTR list, LPDWORD size);
PDH_FUNCTION PdhEnumMachinesHW(PDH_HLOG log, LPWSTR list, LPDWORD size);
PDH_FUNCTION PdhEnumMachinesHA(PDH_HLOG log, LPSTR list, LPDWORD size);
PDH_FUNCTION PdhConnectMachineW(LPCWSTR machine);
PDH_FUNCTION PdhConnectMachineA(LPCSTR machine);
PDH_FUNCTION PdhGetDefaultPerfObjectW(LPCWSTR source, LPCWSTR machine, LPWSTR name, LPDWORD size);
PDH_FUNCTION PdhGetDefaultPerfObjectA(LPCSTR source, LPCSTR machine, LPSTR name, LPDWORD size);
PDH_FUNCTION PdhGetDefaultPerfObjectHW(PDH_HLOG log, LPCWSTR machine, LPWSTR name, LPDWORD size);
PDH_FUNCTION PdhGetDefaultPerfObjectHA(PDH_HLOG log, LPCSTR machine, LPSTR name, LPDWORD size);
PDH_FUNCTION PdhGetDefaultPerfCounterW(LPCWSTR source, LPCWSTR machine, LPCWSTR object, LPWSTR name, LPDWORD size);
PDH_FUNCTION PdhGetDefaultPerfCounterA(LPCSTR source, LPCSTR machine, LPCSTR object, LPSTR name, LPDWORD size);
PDH_FUNCTION PdhGetDefaultPerfCounterHW(PDH_HLOG log, LPCWSTR machine, LPCWSTR object, LPWSTR name, LPDWORD size);
PDH_FUNCTION PdhGetDefaultPerfCounterHA(PDH_HLOG log, LPCSTR machine, LPCSTR object, LPSTR name, LPDWORD size);
PDH_FUNCTION PdhExpandWildCardPathW(LPCWSTR source, LPCWSTR path, LPWSTR list, LPDWORD size, DWORD flags);
PDH_FUNCTION PdhExpandWildCardPathA(LPCSTR source, LPCSTR path, LPSTR list, LPDWORD size, DWORD flags);
PDH_FUNCTION PdhExpandWildCardPathHW(PDH_HLOG log, LPCWSTR path, LPWSTR list, LPDWORD size, DWORD flags);
PDH_FUNCTION PdhExpandWildCardPathHA(PDH_HLOG log, LPCSTR path, LPSTR list, LPDWORD size, DWORD flags);
PDH_FUNCTION PdhExpandCounterPathW(LPCWSTR path, LPWSTR list, LPDWORD size);
PDH_FUNCTION PdhExpandCounterPathA(LPCSTR path, LPSTR list, LPDWORD size);
PDH_FUNCTION PdhLookupPerfNameByIndexW(LPCWSTR machine, DWORD index, LPWSTR buffer, LPDWORD size);
PDH_FUNCTION PdhLookupPerfNameByIndexA(LPCSTR machine, DWORD index, LPSTR buffer, LPDWORD size);
PDH_FUNCTION PdhLookupPerfIndexByNameW(LPCWSTR machine, LPCWSTR name, LPDWORD index);
PDH_FUNCTION PdhLookupPerfIndexByNameA(LPCSTR machine, LPCSTR name, LPDWORD index);
PDH_FUNCTION PdhTranslate009CounterW(LPCWSTR path, LPWSTR buffer, LPDWORD size);
PDH_FUNCTION PdhTranslateLocaleCounterW(LPCWSTR path, LPWSTR buffer, LPDWORD size);
PDH_FUNCTION PdhParseCounterPathW(LPCWSTR path, PDH_COUNTER_PATH_ELEMENTS_W *elements, LPDWORD size, DWORD flags);
PDH_FUNCTION PdhParseCounterPathA(LPCSTR path, PDH_COUNTER_PATH_ELEMENTS_A *elements, LPDWORD size, DWORD flags);
PDH_FUNCTION PdhMakeCounterPathW(PDH_COUNTER_PATH_ELEMENTS_W *elements, LPWSTR buffer, LPDWORD size, DWORD flags);
PDH_FUNCTION PdhMakeCounterPathA(PDH_COUNTER_PATH_ELEMENTS_A *elements, LPSTR buffer, LPDWORD size, DWORD flags);
PDH_FUNCTION PdhParseInstanceNameW(LPCWSTR text, LPWSTR name, LPDWORD name_size, LPWSTR parent, LPDWORD parent_size,
                                   LPDWORD index);
PDH_FUNCTION PdhParseInstanceNameA(LPCSTR text, LPSTR name, LPDWORD name_size, LPSTR parent, LPDWORD parent_size,
                                   LPDWORD index);
PDH_FUNCTION PdhOpenLogW(LPCWSTR file, DWORD access, LPDWORD type, PDH_HQUERY query, DWORD size, LPCWSTR caption,
                         PDH_HLOG *log);
PDH_FUNCTION PdhOpenLogA(LPCSTR file, DWORD access, LPDWORD type, PDH_HQUERY query, DWORD size, LPCSTR caption,
                         PDH_HLOG *log);
PDH_FUNCTION PdhCloseLog(PDH_HLOG log, DWORD flags);
PDH_FUNCTION PdhUpdateLogW(PDH_HLOG log, LPCWSTR text);
PDH_FUNCTION PdhUpdateLogA(PDH_HLOG log, LPCSTR text);
PDH_FUNCTION PdhUpdateLogFileCatalog(PDH_HLOG log);
PDH_FUNCTION PdhGetLogFileSize(PDH_HLOG log, LONGLONG *size);
PDH_FUNCTION PdhGetLogFileTypeW(LPCWSTR file, LPDWORD type);
PDH_FUNCTION PdhGetLogSetGUID(PDH_HLOG log, GUID *guid, int *run);
PDH_FUNCTION PdhSetLogSetRunID(PDH_HLOG log, int run);
PDH_FUNCTION PdhEnumLogSetNamesW(LPCWSTR source, LPWSTR list, LPDWORD size);
PDH_FUNCTION PdhEnumLogSetNamesA(LPCSTR source, LPSTR list, LPDWORD size);
PDH_FUNCTION PdhSetDefaultRealTimeDataSource(DWORD source);
PDH_FUNCTION PdhBindInputDataSourceW(PDH_HLOG *handle, LPCWSTR names);
PDH_FUNCTION PdhBindInputDataSourceA(PDH_HLOG *handle, LPCSTR names);
PDH_FUNCTION PdhGetDataSourceTimeRangeW(LPCWSTR source, LPDWORD count, PDH_TIME_INFO *range, LPDWORD size);
PDH_FUNCTION PdhGetDataSourceTimeRangeA(LPCSTR source, LPDWORD count, PDH_TIME_INFO *range, LPDWORD size);
PDH_FUNCTION PdhGetDataSourceTimeRangeH(PDH_HLOG log, LPDWORD count, PDH_TIME_INFO *range, LPDWORD size);
PDH_FUNCTION PdhVerifySQLDBW(LPCWSTR source);
PDH_FUNCTION PdhVerifySQLDBA(LPCSTR source);
PDH_FUNCTION PdhCreateSQLTablesW(LPCWSTR source);
PDH_FUNCTION PdhCreateSQLTablesA(LPCSTR source);
PDH_FUNCTION PdhBrowseCountersW(PVOID config);
PDH_FUNCTION PdhBrowseCountersA(PVOID config);
PDH_FUNCTION PdhSelectDataSourceW(HWND owner, DWORD flags, LPWSTR source, LPDWORD size);
PDH_FUNCTION PdhSelectDataSourceA(HWND owner, DWORD flags, LPSTR source, LPDWORD size);
PDH_FUNCTION PdhVbOpenQuery(PDH_HQUERY *query);
PDH_FUNCTION PdhVbAddCounter(PDH_HQUERY query, LPCSTR path, PDH_HCOUNTER *counter);
PDH_FUNCTION PdhVbGetCounterPathElements(LPCSTR path, LPSTR machine, LPSTR object, LPSTR instance, LPSTR parent,
                                         LPSTR counter, DWORD size);

#endif
