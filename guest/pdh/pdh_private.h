/* pdh internals: the performance data of the machine as objects, instances and counters, names, and counter paths. */
#ifndef MIME_PDH_PRIVATE_H
#define MIME_PDH_PRIVATE_H

#define PDH_FUNCTION LONG WINAPI
#include <mime/win32/pdh.h>
#include <mime/ntapi.h>

#define wsprintfW(buffer, ...) _snwprintf((buffer), 4000, __VA_ARGS__)

#define PDH_WARNING(x) ((PDH_STATUS)(0x80000000L | (x)))
#define PDH_ERROR(x) ((PDH_STATUS)(0xC0000000L | (x)))

#define PDH_RETRY PDH_WARNING(0x7D4)
#define PDH_CANNOT_READ_NAME_STRINGS PDH_ERROR(0xBC8)
#define PDH_LOG_FILE_OPEN_ERROR PDH_ERROR(0xBCA)
#define PDH_UNABLE_READ_LOG_HEADER PDH_ERROR(0xBD0)
#define PDH_INVALID_DATASOURCE PDH_ERROR(0xBDD)
#define PDH_NO_COUNTERS PDH_ERROR(0xBDF)
#define PDH_QUERY_PERF_DATA_TIMEOUT PDH_ERROR(0xBFE)

#define PDH_DETAIL_MIN 100
#define PDH_DETAIL_MAX 400
#define PDH_INDEX_NONE ((DWORD)-1)

/* ------------------------------------------------------------------ the layout of winperf.h */

#pragma pack(push, 8)
typedef struct PERF_BLOCK_ {
    WCHAR Signature[4];
    DWORD LittleEndian, Version, Revision, TotalByteLength, HeaderLength, NumObjectTypes;
    LONG DefaultObject;
    SYSTEMTIME SystemTime;
    LARGE_INTEGER PerfTime, PerfFreq, PerfTime100nSec;
    DWORD SystemNameLength, SystemNameOffset;
} PERF_BLOCK_;

typedef struct PERF_OBJECT_ {
    DWORD TotalByteLength, DefinitionLength, HeaderLength, ObjectNameTitleIndex, ObjectNameTitle, ObjectHelpTitleIndex,
        ObjectHelpTitle, DetailLevel, NumCounters;
    LONG DefaultCounter, NumInstances;
    DWORD CodePage;
    LARGE_INTEGER PerfTime, PerfFreq;
} PERF_OBJECT_;

typedef struct PERF_COUNTER_ {
    DWORD ByteLength, CounterNameTitleIndex, CounterNameTitle, CounterHelpTitleIndex, CounterHelpTitle;
    LONG DefaultScale;
    DWORD DetailLevel, CounterType, CounterSize, CounterOffset;
} PERF_COUNTER_;

typedef struct PERF_INSTANCE_ {
    DWORD ByteLength, ParentObjectTitleIndex, ParentObjectInstance;
    LONG UniqueID;
    DWORD NameOffset, NameLength;
} PERF_INSTANCE_;
#pragma pack(pop)

#define PERF_NO_INSTANCES_ (-1)
#define PERF_TYPE_MASK_ 0x00000C00
#define PERF_SUBTYPE_MASK_ 0x000F0000
#define PERF_COUNTER_BASE_ 0x00030000
#define PERF_FRACTION_ 0x00020000
#define PERF_TIME_BASE_MASK_ 0x00300000
#define PERF_TIMER_100NS_ 0x00100000
#define PERF_OBJECT_TIMER_ 0x00200000
#define PERF_DELTA_COUNTER_ 0x00400000

/* the structures of the public header under the names the sources use. */
typedef PDH_COUNTER_PATH_ELEMENTS_W PDH_PATH_ELEMENTS_W;
typedef PDH_COUNTER_PATH_ELEMENTS_A PDH_PATH_ELEMENTS_A;
typedef PDH_STATISTICS PDH_STATISTICS_;
typedef PDH_TIME_INFO PDH_TIME_INFO_;

/* ------------------------------------------------------------------ the performance data */

typedef struct PDH_SNAPSHOT {
    BYTE *Data;
    DWORD Size;
} PDH_SNAPSHOT;

typedef struct PDH_NAME {
    DWORD Index;
    WCHAR *Text;
} PDH_NAME;

PDH_STATUS PdhiSnapshot(PDH_SNAPSHOT *snapshot);
void PdhiFreeSnapshot(PDH_SNAPSHOT *snapshot);
const PERF_BLOCK_ *PdhiBlock(const PDH_SNAPSHOT *snapshot);
const PERF_OBJECT_ *PdhiObject(const PDH_SNAPSHOT *snapshot, DWORD position);
const PERF_OBJECT_ *PdhiFindObject(const PDH_SNAPSHOT *snapshot, DWORD index);
const PERF_COUNTER_ *PdhiCounterAt(const PERF_OBJECT_ *object, DWORD position);
const PERF_COUNTER_ *PdhiFindCounter(const PERF_OBJECT_ *object, DWORD index);
const PERF_INSTANCE_ *PdhiInstanceAt(const PERF_OBJECT_ *object, LONG position, const BYTE **block);
const WCHAR *PdhiInstanceName(const PERF_INSTANCE_ *instance);

/* the name or help text of an index: NULL when it has none. */
const WCHAR *PdhiNameOf(DWORD index);
const WCHAR *PdhiHelpOf(DWORD index);
DWORD PdhiIndexOfName(const WCHAR *name);
DWORD PdhiIndexOfCounterName(const PERF_OBJECT_ *object, const WCHAR *name);

/* the name of the instance at a position, with a number after the duplicates of an earlier one. WCHAR[128]. */
void PdhiInstanceDisplayName(const PERF_OBJECT_ *object, LONG position, WCHAR *name, DWORD *index);

/* a machine name: local, or the name of this machine. */
PDH_STATUS PdhiCheckMachine(LPCWSTR machine);
BOOL PdhiIsLocalMachine(LPCWSTR machine);

/* ------------------------------------------------------------------ counter paths */

typedef struct PDH_PATH {
    WCHAR *Machine;
    WCHAR *Object;
    WCHAR *Instance;
    WCHAR *Parent;
    DWORD Index;
    WCHAR *Counter;
    WCHAR Storage[1];
} PDH_PATH;

/* parses a path into a block that the caller frees with LocalFree. */
PDH_STATUS PdhiParsePath(LPCWSTR path, BOOL allow_wildcards, PDH_PATH **parsed);
PDH_STATUS PdhiMakePath(const PDH_PATH_ELEMENTS_W *elements, WCHAR *buffer, DWORD *size);
BOOL PdhiHasWildcard(LPCWSTR text);
BOOL PdhiWildcardMatch(LPCWSTR pattern, LPCWSTR text);

/* writes text to a caller buffer in characters as the api does: the needed size, PDH_MORE_DATA when it does not fit. */
PDH_STATUS PdhiReturnText(LPCWSTR text, LPWSTR buffer, DWORD *size);

PDH_STATUS PdhiWideOf(LPCSTR text, WCHAR *out, DWORD chars);

#endif
