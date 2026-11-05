/*
 * win32 base api of kernel32.dll and kernelbase.dll. kernelbase implements everything and kernel32
 * forwards its exports there, like windows does.
 */
#ifndef MIME_WIN32_WINBASE_H
#define MIME_WIN32_WINBASE_H

#include <mime/win32/windef.h>
#include <mime/ntrtl.h>
#include <mime/win32/winerror.h>

#undef WINBASEAPI
#ifdef MIME_BUILD_KERNELBASE
#define WINBASEAPI DECLSPEC_EXPORT
#else
#define WINBASEAPI DECLSPEC_IMPORT
#endif

typedef CHAR *LPCH;
typedef WCHAR *LPWCH;
typedef const CHAR *LPCCH;
typedef const WCHAR *LPCWCH;
typedef ULONG_PTR *PDWORD_PTR;
typedef DWORD LCTYPE;
typedef DWORD CALTYPE;
typedef DWORD CALID;
typedef DWORD GEOID;
typedef LONG GEOTYPE;
typedef DWORD EXECUTION_STATE;
typedef DWORD REGSAM;
typedef LONG LSTATUS;
typedef void *PSID;
typedef void *PSECURITY_DESCRIPTOR;
typedef void *HPCON;
typedef HANDLE *LPHANDLE;
typedef unsigned int UINT32;
typedef signed int INT32;
typedef unsigned char UINT8;
typedef signed char INT8;
typedef unsigned short UINT16;
typedef signed short INT16;

#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFF)
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)

#define ZeroMemory(d, l) memset((d), 0, (l))
#define FillMemory(d, l, f) memset((d), (f), (l))
#define CopyMemory(d, s, l) memcpy((d), (s), (l))
#define MoveMemory(d, s, l) memmove((d), (s), (l))
#define SecureZeroMemory(d, l) memset((d), 0, (l))

/* ------------------------------------------------------------------ common structures */

typedef struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef struct _OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union {
        struct {
            DWORD Offset;
            DWORD OffsetHigh;
        };
        PVOID Pointer;
    };
    HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;

typedef struct _OVERLAPPED_ENTRY {
    ULONG_PTR lpCompletionKey;
    LPOVERLAPPED lpOverlapped;
    ULONG_PTR Internal;
    DWORD dwNumberOfBytesTransferred;
} OVERLAPPED_ENTRY, *LPOVERLAPPED_ENTRY;

typedef void(WINAPI *LPOVERLAPPED_COMPLETION_ROUTINE)(DWORD error, DWORD bytes, LPOVERLAPPED overlapped);

typedef struct _SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;

typedef struct _TIME_ZONE_INFORMATION {
    LONG Bias;
    WCHAR StandardName[32];
    SYSTEMTIME StandardDate;
    LONG StandardBias;
    WCHAR DaylightName[32];
    SYSTEMTIME DaylightDate;
    LONG DaylightBias;
} TIME_ZONE_INFORMATION, *PTIME_ZONE_INFORMATION, *LPTIME_ZONE_INFORMATION;

typedef struct _TIME_DYNAMIC_ZONE_INFORMATION {
    LONG Bias;
    WCHAR StandardName[32];
    SYSTEMTIME StandardDate;
    LONG StandardBias;
    WCHAR DaylightName[32];
    SYSTEMTIME DaylightDate;
    LONG DaylightBias;
    WCHAR TimeZoneKeyName[128];
    BOOLEAN DynamicDaylightTimeDisabled;
} DYNAMIC_TIME_ZONE_INFORMATION, *PDYNAMIC_TIME_ZONE_INFORMATION;

#define TIME_ZONE_ID_INVALID ((DWORD)0xFFFFFFFF)
#define TIME_ZONE_ID_UNKNOWN 0
#define TIME_ZONE_ID_STANDARD 1
#define TIME_ZONE_ID_DAYLIGHT 2

/* ------------------------------------------------------------------ errors */

#define SEM_FAILCRITICALERRORS 0x0001
#define SEM_NOGPFAULTERRORBOX 0x0002
#define SEM_NOALIGNMENTFAULTEXCEPT 0x0004
#define SEM_NOOPENFILEERRORBOX 0x8000

WINBASEAPI DWORD WINAPI GetLastError(void);
WINBASEAPI void WINAPI SetLastError(DWORD error);
WINBASEAPI void WINAPI RestoreLastError(DWORD error);
WINBASEAPI UINT WINAPI SetErrorMode(UINT mode);
WINBASEAPI UINT WINAPI GetErrorMode(void);
WINBASEAPI BOOL WINAPI SetThreadErrorMode(DWORD mode, LPDWORD old);
WINBASEAPI DWORD WINAPI GetThreadErrorMode(void);
WINBASEAPI ULONG WINAPI BaseSetLastNTError(NTSTATUS status);

#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200
#define FORMAT_MESSAGE_FROM_STRING 0x00000400
#define FORMAT_MESSAGE_FROM_HMODULE 0x00000800
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000
#define FORMAT_MESSAGE_ARGUMENT_ARRAY 0x00002000
#define FORMAT_MESSAGE_MAX_WIDTH_MASK 0x000000FF

WINBASEAPI DWORD WINAPI FormatMessageW(DWORD flags, LPCVOID source, DWORD id, DWORD lang, LPWSTR buffer, DWORD size,
                                       va_list *args);
WINBASEAPI DWORD WINAPI FormatMessageA(DWORD flags, LPCVOID source, DWORD id, DWORD lang, LPSTR buffer, DWORD size,
                                       va_list *args);

/* ------------------------------------------------------------------ handles */

#define HANDLE_FLAG_INHERIT 0x00000001
#define HANDLE_FLAG_PROTECT_FROM_CLOSE 0x00000002
#define DUPLICATE_CLOSE_SOURCE 0x00000001
#define DUPLICATE_SAME_ACCESS 0x00000002

WINBASEAPI BOOL WINAPI CloseHandle(HANDLE handle);
WINBASEAPI BOOL WINAPI DuplicateHandle(HANDLE source_process, HANDLE source, HANDLE target_process, LPHANDLE target,
                                       DWORD access, BOOL inherit, DWORD options);
WINBASEAPI BOOL WINAPI GetHandleInformation(HANDLE handle, LPDWORD flags);
WINBASEAPI BOOL WINAPI SetHandleInformation(HANDLE handle, DWORD mask, DWORD flags);
WINBASEAPI BOOL WINAPI CompareObjectHandles(HANDLE a, HANDLE b);

/* ------------------------------------------------------------------ modules */

#define DONT_RESOLVE_DLL_REFERENCES 0x00000001
#define LOAD_LIBRARY_AS_DATAFILE 0x00000002
#define LOAD_WITH_ALTERED_SEARCH_PATH 0x00000008
#define LOAD_IGNORE_CODE_AUTHZ_LEVEL 0x00000010
#define LOAD_LIBRARY_AS_IMAGE_RESOURCE 0x00000020
#define LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE 0x00000040
#define LOAD_LIBRARY_REQUIRE_SIGNED_TARGET 0x00000080
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#define LOAD_LIBRARY_SEARCH_APPLICATION_DIR 0x00000200
#define LOAD_LIBRARY_SEARCH_USER_DIRS 0x00000400
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
#define LOAD_LIBRARY_SAFE_CURRENT_DIRS 0x00002000
#define LOAD_LIBRARY_SEARCH_SYSTEM32_NO_FORWARDER 0x00004000

#define GET_MODULE_HANDLE_EX_FLAG_PIN 0x00000001
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004

typedef void *DLL_DIRECTORY_COOKIE, **PDLL_DIRECTORY_COOKIE;

typedef struct _MODULEINFO {
    LPVOID lpBaseOfDll;
    DWORD SizeOfImage;
    LPVOID EntryPoint;
} MODULEINFO, *LPMODULEINFO;

WINBASEAPI HMODULE WINAPI GetModuleHandleW(LPCWSTR name);
WINBASEAPI HMODULE WINAPI GetModuleHandleA(LPCSTR name);
WINBASEAPI BOOL WINAPI GetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE *module);
WINBASEAPI BOOL WINAPI GetModuleHandleExA(DWORD flags, LPCSTR name, HMODULE *module);
WINBASEAPI DWORD WINAPI GetModuleFileNameW(HMODULE module, LPWSTR buffer, DWORD size);
WINBASEAPI DWORD WINAPI GetModuleFileNameA(HMODULE module, LPSTR buffer, DWORD size);
WINBASEAPI HMODULE WINAPI LoadLibraryW(LPCWSTR name);
WINBASEAPI HMODULE WINAPI LoadLibraryA(LPCSTR name);
WINBASEAPI HMODULE WINAPI LoadLibraryExW(LPCWSTR name, HANDLE file, DWORD flags);
WINBASEAPI HMODULE WINAPI LoadLibraryExA(LPCSTR name, HANDLE file, DWORD flags);
WINBASEAPI HMODULE WINAPI LoadPackagedLibrary(LPCWSTR name, DWORD reserved);
WINBASEAPI BOOL WINAPI FreeLibrary(HMODULE module);
WINBASEAPI DECLSPEC_NORETURN void WINAPI FreeLibraryAndExitThread(HMODULE module, DWORD code);
WINBASEAPI FARPROC WINAPI GetProcAddress(HMODULE module, LPCSTR name);
WINBASEAPI BOOL WINAPI DisableThreadLibraryCalls(HMODULE module);
WINBASEAPI DLL_DIRECTORY_COOKIE WINAPI AddDllDirectory(LPCWSTR directory);
WINBASEAPI BOOL WINAPI RemoveDllDirectory(DLL_DIRECTORY_COOKIE cookie);
WINBASEAPI BOOL WINAPI SetDefaultDllDirectories(DWORD flags);
WINBASEAPI BOOL WINAPI SetDllDirectoryW(LPCWSTR directory);
WINBASEAPI BOOL WINAPI SetDllDirectoryA(LPCSTR directory);
WINBASEAPI DWORD WINAPI GetDllDirectoryW(DWORD size, LPWSTR buffer);
WINBASEAPI DWORD WINAPI GetDllDirectoryA(DWORD size, LPSTR buffer);
WINBASEAPI BOOL WINAPI K32GetModuleInformation(HANDLE process, HMODULE module, LPMODULEINFO info, DWORD size);
WINBASEAPI BOOL WINAPI K32EnumProcessModules(HANDLE process, HMODULE *modules, DWORD size, LPDWORD needed);
WINBASEAPI BOOL WINAPI K32EnumProcessModulesEx(HANDLE process, HMODULE *modules, DWORD size, LPDWORD needed,
                                               DWORD filter);
WINBASEAPI DWORD WINAPI K32GetModuleBaseNameW(HANDLE process, HMODULE module, LPWSTR name, DWORD size);
WINBASEAPI DWORD WINAPI K32GetModuleBaseNameA(HANDLE process, HMODULE module, LPSTR name, DWORD size);
WINBASEAPI DWORD WINAPI K32GetModuleFileNameExW(HANDLE process, HMODULE module, LPWSTR name, DWORD size);
WINBASEAPI DWORD WINAPI K32GetModuleFileNameExA(HANDLE process, HMODULE module, LPSTR name, DWORD size);
WINBASEAPI DWORD WINAPI K32GetProcessImageFileNameW(HANDLE process, LPWSTR name, DWORD size);
WINBASEAPI DWORD WINAPI K32GetProcessImageFileNameA(HANDLE process, LPSTR name, DWORD size);
WINBASEAPI BOOL WINAPI QueryFullProcessImageNameW(HANDLE process, DWORD flags, LPWSTR name, PDWORD size);
WINBASEAPI BOOL WINAPI QueryFullProcessImageNameA(HANDLE process, DWORD flags, LPSTR name, PDWORD size);

/* ------------------------------------------------------------------ memory */

#define LMEM_FIXED 0x0000
#define LMEM_MOVEABLE 0x0002
#define LMEM_NOCOMPACT 0x0010
#define LMEM_NODISCARD 0x0020
#define LMEM_ZEROINIT 0x0040
#define LMEM_MODIFY 0x0080
#define LMEM_DISCARDABLE 0x0F00
#define LMEM_VALID_FLAGS 0x0F72
#define LMEM_INVALID_HANDLE 0x8000
#define LHND (LMEM_MOVEABLE | LMEM_ZEROINIT)
#define LPTR (LMEM_FIXED | LMEM_ZEROINIT)
#define LMEM_DISCARDED 0x4000
#define LMEM_LOCKCOUNT 0x00FF

#define GMEM_FIXED 0x0000
#define GMEM_MOVEABLE 0x0002
#define GMEM_NOCOMPACT 0x0010
#define GMEM_NODISCARD 0x0020
#define GMEM_ZEROINIT 0x0040
#define GMEM_MODIFY 0x0080
#define GMEM_DISCARDABLE 0x0100
#define GMEM_NOT_BANKED 0x1000
#define GMEM_SHARE 0x2000
#define GMEM_DDESHARE 0x2000
#define GMEM_NOTIFY 0x4000
#define GMEM_LOWER GMEM_NOT_BANKED
#define GMEM_VALID_FLAGS 0x7F72
#define GMEM_INVALID_HANDLE 0x8000
#define GHND (GMEM_MOVEABLE | GMEM_ZEROINIT)
#define GPTR (GMEM_FIXED | GMEM_ZEROINIT)
#define GMEM_DISCARDED 0x4000
#define GMEM_LOCKCOUNT 0x00FF

#define HEAP_CREATE_ALIGN_16 0x00010000
#define HEAP_CREATE_ENABLE_TRACING 0x00020000
#define HEAP_MAXIMUM_TAG 0x0FFF
#define HEAP_PSEUDO_TAG_FLAG 0x8000
#define HEAP_TAIL_CHECKING_ENABLED 0x00000020
#define HEAP_FREE_CHECKING_ENABLED 0x00000040
#define HEAP_DISABLE_COALESCE_ON_FREE 0x00000080

typedef enum _HEAP_INFORMATION_CLASS {
    HeapCompatibilityInformation = 0,
    HeapEnableTerminationOnCorruption = 1,
    HeapOptimizeResources = 3,
} HEAP_INFORMATION_CLASS;

typedef struct _PROCESS_HEAP_ENTRY {
    PVOID lpData;
    DWORD cbData;
    BYTE cbOverhead;
    BYTE iRegionIndex;
    WORD wFlags;
    union {
        struct {
            HANDLE hMem;
            DWORD dwReserved[3];
        } Block;
        struct {
            DWORD dwCommittedSize;
            DWORD dwUnCommittedSize;
            LPVOID lpFirstBlock;
            LPVOID lpLastBlock;
        } Region;
    };
} PROCESS_HEAP_ENTRY, *LPPROCESS_HEAP_ENTRY, *PPROCESS_HEAP_ENTRY;
#define PROCESS_HEAP_REGION 0x0001
#define PROCESS_HEAP_UNCOMMITTED_RANGE 0x0002
#define PROCESS_HEAP_ENTRY_BUSY 0x0004
#define PROCESS_HEAP_SEG_ALLOC 0x0008
#define PROCESS_HEAP_ENTRY_MOVEABLE 0x0010
#define PROCESS_HEAP_ENTRY_DDESHARE 0x0020

typedef struct _HEAP_SUMMARY {
    DWORD cb;
    SIZE_T cbAllocated;
    SIZE_T cbCommitted;
    SIZE_T cbReserved;
    SIZE_T cbMaxReserve;
} HEAP_SUMMARY, *PHEAP_SUMMARY, *LPHEAP_SUMMARY;

typedef struct _MEMORYSTATUS {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    SIZE_T dwTotalPhys;
    SIZE_T dwAvailPhys;
    SIZE_T dwTotalPageFile;
    SIZE_T dwAvailPageFile;
    SIZE_T dwTotalVirtual;
    SIZE_T dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;

typedef struct _MEMORYSTATUSEX {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    DWORDLONG ullTotalPhys;
    DWORDLONG ullAvailPhys;
    DWORDLONG ullTotalPageFile;
    DWORDLONG ullAvailPageFile;
    DWORDLONG ullTotalVirtual;
    DWORDLONG ullAvailVirtual;
    DWORDLONG ullAvailExtendedVirtual;
} MEMORYSTATUSEX, *LPMEMORYSTATUSEX;

typedef struct _WIN32_MEMORY_RANGE_ENTRY {
    PVOID VirtualAddress;
    SIZE_T NumberOfBytes;
} WIN32_MEMORY_RANGE_ENTRY, *PWIN32_MEMORY_RANGE_ENTRY;

#define FILE_MAP_COPY 0x0001
#define FILE_MAP_WRITE 0x0002
#define FILE_MAP_READ 0x0004
#define FILE_MAP_EXECUTE 0x0020
#define FILE_MAP_ALL_ACCESS SECTION_ALL_ACCESS

typedef MEMORY_BASIC_INFORMATION *LPMEMORY_BASIC_INFORMATION;

WINBASEAPI LPVOID WINAPI VirtualAlloc(LPVOID address, SIZE_T size, DWORD type, DWORD protect);
WINBASEAPI LPVOID WINAPI VirtualAllocEx(HANDLE process, LPVOID address, SIZE_T size, DWORD type, DWORD protect);
WINBASEAPI BOOL WINAPI VirtualFree(LPVOID address, SIZE_T size, DWORD type);
WINBASEAPI BOOL WINAPI VirtualFreeEx(HANDLE process, LPVOID address, SIZE_T size, DWORD type);
WINBASEAPI BOOL WINAPI VirtualProtect(LPVOID address, SIZE_T size, DWORD protect, PDWORD old);
WINBASEAPI BOOL WINAPI VirtualProtectEx(HANDLE process, LPVOID address, SIZE_T size, DWORD protect, PDWORD old);
WINBASEAPI SIZE_T WINAPI VirtualQuery(LPCVOID address, PMEMORY_BASIC_INFORMATION info, SIZE_T length);
WINBASEAPI SIZE_T WINAPI VirtualQueryEx(HANDLE process, LPCVOID address, PMEMORY_BASIC_INFORMATION info,
                                        SIZE_T length);
WINBASEAPI BOOL WINAPI VirtualLock(LPVOID address, SIZE_T size);
WINBASEAPI BOOL WINAPI VirtualUnlock(LPVOID address, SIZE_T size);
WINBASEAPI BOOL WINAPI ReadProcessMemory(HANDLE process, LPCVOID base, LPVOID buffer, SIZE_T size, SIZE_T *read);
WINBASEAPI BOOL WINAPI WriteProcessMemory(HANDLE process, LPVOID base, LPCVOID buffer, SIZE_T size,
                                          SIZE_T *written);
WINBASEAPI BOOL WINAPI FlushInstructionCache(HANDLE process, LPCVOID base, SIZE_T size);
WINBASEAPI void WINAPI FlushProcessWriteBuffers(void);
WINBASEAPI BOOL WINAPI PrefetchVirtualMemory(HANDLE process, ULONG_PTR count, PWIN32_MEMORY_RANGE_ENTRY entries,
                                             ULONG flags);
WINBASEAPI UINT WINAPI GetWriteWatch(DWORD flags, PVOID base, SIZE_T size, PVOID *addresses, ULONG_PTR *count,
                                     LPDWORD granularity);
WINBASEAPI UINT WINAPI ResetWriteWatch(LPVOID base, SIZE_T size);
WINBASEAPI SIZE_T WINAPI GetLargePageMinimum(void);

WINBASEAPI HANDLE WINAPI HeapCreate(DWORD options, SIZE_T initial, SIZE_T maximum);
WINBASEAPI BOOL WINAPI HeapDestroy(HANDLE heap);
WINBASEAPI LPVOID WINAPI HeapAlloc(HANDLE heap, DWORD flags, SIZE_T size);
WINBASEAPI LPVOID WINAPI HeapReAlloc(HANDLE heap, DWORD flags, LPVOID p, SIZE_T size);
WINBASEAPI BOOL WINAPI HeapFree(HANDLE heap, DWORD flags, LPVOID p);
WINBASEAPI SIZE_T WINAPI HeapSize(HANDLE heap, DWORD flags, LPCVOID p);
WINBASEAPI BOOL WINAPI HeapValidate(HANDLE heap, DWORD flags, LPCVOID p);
WINBASEAPI SIZE_T WINAPI HeapCompact(HANDLE heap, DWORD flags);
WINBASEAPI BOOL WINAPI HeapLock(HANDLE heap);
WINBASEAPI BOOL WINAPI HeapUnlock(HANDLE heap);
WINBASEAPI BOOL WINAPI HeapWalk(HANDLE heap, LPPROCESS_HEAP_ENTRY entry);
WINBASEAPI BOOL WINAPI HeapSummary(HANDLE heap, DWORD flags, LPHEAP_SUMMARY summary);
WINBASEAPI BOOL WINAPI HeapSetInformation(HANDLE heap, HEAP_INFORMATION_CLASS klass, PVOID info, SIZE_T length);
WINBASEAPI BOOL WINAPI HeapQueryInformation(HANDLE heap, HEAP_INFORMATION_CLASS klass, PVOID info, SIZE_T length,
                                            PSIZE_T returned);
WINBASEAPI HANDLE WINAPI GetProcessHeap(void);
WINBASEAPI DWORD WINAPI GetProcessHeaps(DWORD count, PHANDLE heaps);

WINBASEAPI HLOCAL WINAPI LocalAlloc(UINT flags, SIZE_T size);
WINBASEAPI HLOCAL WINAPI LocalFree(HLOCAL mem);
WINBASEAPI HLOCAL WINAPI LocalReAlloc(HLOCAL mem, SIZE_T size, UINT flags);
WINBASEAPI SIZE_T WINAPI LocalSize(HLOCAL mem);
WINBASEAPI LPVOID WINAPI LocalLock(HLOCAL mem);
WINBASEAPI BOOL WINAPI LocalUnlock(HLOCAL mem);
WINBASEAPI HLOCAL WINAPI LocalHandle(LPCVOID p);
WINBASEAPI UINT WINAPI LocalFlags(HLOCAL mem);
WINBASEAPI SIZE_T WINAPI LocalCompact(UINT min_free);
WINBASEAPI SIZE_T WINAPI LocalShrink(HLOCAL mem, UINT size);
WINBASEAPI HGLOBAL WINAPI GlobalAlloc(UINT flags, SIZE_T size);
WINBASEAPI HGLOBAL WINAPI GlobalFree(HGLOBAL mem);
WINBASEAPI HGLOBAL WINAPI GlobalReAlloc(HGLOBAL mem, SIZE_T size, UINT flags);
WINBASEAPI SIZE_T WINAPI GlobalSize(HGLOBAL mem);
WINBASEAPI LPVOID WINAPI GlobalLock(HGLOBAL mem);
WINBASEAPI BOOL WINAPI GlobalUnlock(HGLOBAL mem);
WINBASEAPI HGLOBAL WINAPI GlobalHandle(LPCVOID p);
WINBASEAPI UINT WINAPI GlobalFlags(HGLOBAL mem);
WINBASEAPI SIZE_T WINAPI GlobalCompact(DWORD min_free);
WINBASEAPI void WINAPI GlobalFix(HGLOBAL mem);
WINBASEAPI void WINAPI GlobalUnfix(HGLOBAL mem);
WINBASEAPI LPVOID WINAPI GlobalWire(HGLOBAL mem);
WINBASEAPI BOOL WINAPI GlobalUnWire(HGLOBAL mem);
#define GlobalDiscard(h) GlobalReAlloc((h), 0, GMEM_MOVEABLE)
#define LocalDiscard(h) LocalReAlloc((h), 0, LMEM_MOVEABLE)

WINBASEAPI BOOL WINAPI IsBadReadPtr(LPCVOID p, UINT_PTR size);
WINBASEAPI BOOL WINAPI IsBadWritePtr(LPVOID p, UINT_PTR size);
WINBASEAPI BOOL WINAPI IsBadHugeReadPtr(LPCVOID p, UINT_PTR size);
WINBASEAPI BOOL WINAPI IsBadHugeWritePtr(LPVOID p, UINT_PTR size);
WINBASEAPI BOOL WINAPI IsBadCodePtr(FARPROC p);
WINBASEAPI BOOL WINAPI IsBadStringPtrA(LPCSTR p, UINT_PTR max);
WINBASEAPI BOOL WINAPI IsBadStringPtrW(LPCWSTR p, UINT_PTR max);

WINBASEAPI HANDLE WINAPI CreateFileMappingW(HANDLE file, LPSECURITY_ATTRIBUTES sa, DWORD protect, DWORD size_high,
                                            DWORD size_low, LPCWSTR name);
WINBASEAPI HANDLE WINAPI CreateFileMappingA(HANDLE file, LPSECURITY_ATTRIBUTES sa, DWORD protect, DWORD size_high,
                                            DWORD size_low, LPCSTR name);
WINBASEAPI HANDLE WINAPI OpenFileMappingW(DWORD access, BOOL inherit, LPCWSTR name);
WINBASEAPI HANDLE WINAPI OpenFileMappingA(DWORD access, BOOL inherit, LPCSTR name);
WINBASEAPI LPVOID WINAPI MapViewOfFile(HANDLE mapping, DWORD access, DWORD offset_high, DWORD offset_low,
                                       SIZE_T size);
WINBASEAPI LPVOID WINAPI MapViewOfFileEx(HANDLE mapping, DWORD access, DWORD offset_high, DWORD offset_low,
                                         SIZE_T size, LPVOID base);
WINBASEAPI BOOL WINAPI UnmapViewOfFile(LPCVOID base);
WINBASEAPI BOOL WINAPI UnmapViewOfFileEx(PVOID base, ULONG flags);
WINBASEAPI BOOL WINAPI FlushViewOfFile(LPCVOID base, SIZE_T size);

/* ------------------------------------------------------------------ files */

#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5

#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2

#define FILE_FLAG_WRITE_THROUGH 0x80000000
#define FILE_FLAG_OVERLAPPED 0x40000000
#define FILE_FLAG_NO_BUFFERING 0x20000000
#define FILE_FLAG_RANDOM_ACCESS 0x10000000
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000
#define FILE_FLAG_POSIX_SEMANTICS 0x01000000
#define FILE_FLAG_SESSION_AWARE 0x00800000
#define FILE_FLAG_OPEN_REPARSE_POINT 0x00200000
#define FILE_FLAG_OPEN_NO_RECALL 0x00100000
#define FILE_FLAG_FIRST_PIPE_INSTANCE 0x00080000

#define SECURITY_ANONYMOUS 0x00000000
#define SECURITY_IDENTIFICATION 0x00010000
#define SECURITY_IMPERSONATION 0x00020000
#define SECURITY_DELEGATION 0x00030000
#define SECURITY_CONTEXT_TRACKING 0x00040000
#define SECURITY_EFFECTIVE_ONLY 0x00080000
#define SECURITY_SQOS_PRESENT 0x00100000

#ifndef FILE_ATTRIBUTE_DEVICE
#define FILE_ATTRIBUTE_DEVICE 0x00000040
#endif
#define FILE_ATTRIBUTE_TEMPORARY 0x00000100
#define FILE_ATTRIBUTE_SPARSE_FILE 0x00000200
#define FILE_ATTRIBUTE_COMPRESSED 0x00000800
#define FILE_ATTRIBUTE_OFFLINE 0x00001000
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED 0x00002000
#define FILE_ATTRIBUTE_ENCRYPTED 0x00004000
#define FILE_ATTRIBUTE_INTEGRITY_STREAM 0x00008000
#define FILE_ATTRIBUTE_VIRTUAL 0x00010000
#define FILE_ATTRIBUTE_NO_SCRUB_DATA 0x00020000
#define FILE_ATTRIBUTE_RECALL_ON_OPEN 0x00040000
#define FILE_ATTRIBUTE_PINNED 0x00080000
#define FILE_ATTRIBUTE_UNPINNED 0x00100000
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000

#define FILE_TYPE_UNKNOWN 0x0000
#define FILE_TYPE_DISK 0x0001
#define FILE_TYPE_CHAR 0x0002
#define FILE_TYPE_PIPE 0x0003
#define FILE_TYPE_REMOTE 0x8000

#define MOVEFILE_REPLACE_EXISTING 0x00000001
#define MOVEFILE_COPY_ALLOWED 0x00000002
#define MOVEFILE_DELAY_UNTIL_REBOOT 0x00000004
#define MOVEFILE_WRITE_THROUGH 0x00000008
#define MOVEFILE_CREATE_HARDLINK 0x00000010
#define MOVEFILE_FAIL_IF_NOT_TRACKABLE 0x00000020

#define COPY_FILE_FAIL_IF_EXISTS 0x00000001
#define COPY_FILE_RESTARTABLE 0x00000002
#define COPY_FILE_OPEN_SOURCE_FOR_WRITE 0x00000004
#define COPY_FILE_ALLOW_DECRYPTED_DESTINATION 0x00000008
#define COPY_FILE_COPY_SYMLINK 0x00000800
#define COPY_FILE_NO_BUFFERING 0x00001000

/* CopyFile2. */
typedef enum COPYFILE2_MESSAGE_TYPE {
    COPYFILE2_CALLBACK_NONE = 0,
    COPYFILE2_CALLBACK_CHUNK_STARTED,
    COPYFILE2_CALLBACK_CHUNK_FINISHED,
    COPYFILE2_CALLBACK_STREAM_STARTED,
    COPYFILE2_CALLBACK_STREAM_FINISHED,
    COPYFILE2_CALLBACK_POLL_CONTINUE,
    COPYFILE2_CALLBACK_ERROR,
    COPYFILE2_CALLBACK_MAX
} COPYFILE2_MESSAGE_TYPE;

typedef enum COPYFILE2_MESSAGE_ACTION {
    COPYFILE2_PROGRESS_CONTINUE = 0,
    COPYFILE2_PROGRESS_CANCEL,
    COPYFILE2_PROGRESS_STOP,
    COPYFILE2_PROGRESS_QUIET,
    COPYFILE2_PROGRESS_PAUSE
} COPYFILE2_MESSAGE_ACTION;

typedef struct COPYFILE2_MESSAGE {
    COPYFILE2_MESSAGE_TYPE Type;
    DWORD dwPadding;
    union {
        struct {
            DWORD dwStreamNumber;
            DWORD dwReserved;
            HANDLE hSourceFile;
            HANDLE hDestinationFile;
            ULARGE_INTEGER uliChunkNumber;
            ULARGE_INTEGER uliChunkSize;
            ULARGE_INTEGER uliStreamSize;
            ULARGE_INTEGER uliTotalFileSize;
        } ChunkStarted;
        struct {
            DWORD dwStreamNumber;
            DWORD dwFlags;
            HANDLE hSourceFile;
            HANDLE hDestinationFile;
            ULARGE_INTEGER uliChunkNumber;
            ULARGE_INTEGER uliChunkSize;
            ULARGE_INTEGER uliStreamSize;
            ULARGE_INTEGER uliStreamBytesTransferred;
            ULARGE_INTEGER uliTotalFileSize;
            ULARGE_INTEGER uliTotalBytesTransferred;
        } ChunkFinished;
        struct {
            DWORD dwStreamNumber;
            DWORD dwReserved;
            HANDLE hSourceFile;
            HANDLE hDestinationFile;
            ULARGE_INTEGER uliStreamSize;
            ULARGE_INTEGER uliTotalFileSize;
        } StreamStarted;
        struct {
            DWORD dwStreamNumber;
            DWORD dwReserved;
            HANDLE hSourceFile;
            HANDLE hDestinationFile;
        } StreamFinished;
        struct {
            DWORD dwReserved;
        } PollContinue;
        struct {
            DWORD CopyPhase;
            DWORD dwStreamNumber;
            HRESULT hrFailure;
            DWORD dwReserved;
            ULARGE_INTEGER uliChunkNumber;
            ULARGE_INTEGER uliStreamSize;
            ULARGE_INTEGER uliStreamBytesTransferred;
            ULARGE_INTEGER uliTotalFileSize;
            ULARGE_INTEGER uliTotalBytesTransferred;
        } Error;
    } Info;
} COPYFILE2_MESSAGE;

typedef COPYFILE2_MESSAGE_ACTION(CALLBACK *PCOPYFILE2_PROGRESS_ROUTINE)(const COPYFILE2_MESSAGE *message,
                                                                        PVOID context);

typedef struct COPYFILE2_EXTENDED_PARAMETERS {
    DWORD dwSize;
    DWORD dwCopyFlags;
    BOOL *pfCancel;
    PCOPYFILE2_PROGRESS_ROUTINE pProgressRoutine;
    PVOID pvCallbackContext;
} COPYFILE2_EXTENDED_PARAMETERS;

WINBASEAPI HRESULT WINAPI CopyFile2(PCWSTR source, PCWSTR destination, COPYFILE2_EXTENDED_PARAMETERS *parameters);
WINBASEAPI HANDLE WINAPI FindFirstFileNameW(LPCWSTR path, DWORD flags, LPDWORD length, PWSTR buffer);
WINBASEAPI BOOL WINAPI FindNextFileNameW(HANDLE find, LPDWORD length, PWSTR buffer);
WINBASEAPI DWORD WINAPI GetCompressedFileSizeW(LPCWSTR name, LPDWORD high);
WINBASEAPI DWORD WINAPI GetCompressedFileSizeA(LPCSTR name, LPDWORD high);

#define PROGRESS_CONTINUE 0
#define PROGRESS_CANCEL 1
#define PROGRESS_STOP 2
#define PROGRESS_QUIET 3
#define CALLBACK_CHUNK_FINISHED 0x00000000
#define CALLBACK_STREAM_SWITCH 0x00000001

typedef DWORD(WINAPI *LPPROGRESS_ROUTINE)(LARGE_INTEGER total, LARGE_INTEGER transferred, LARGE_INTEGER stream_size,
                                          LARGE_INTEGER stream_transferred, DWORD stream, DWORD reason,
                                          HANDLE source, HANDLE destination, LPVOID data);

typedef struct _BY_HANDLE_FILE_INFORMATION {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD dwVolumeSerialNumber;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD nNumberOfLinks;
    DWORD nFileIndexHigh;
    DWORD nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION, *PBY_HANDLE_FILE_INFORMATION, *LPBY_HANDLE_FILE_INFORMATION;

typedef struct _WIN32_FIND_DATAW {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    WCHAR cFileName[MAX_PATH];
    WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW, *PWIN32_FIND_DATAW, *LPWIN32_FIND_DATAW;

typedef struct _WIN32_FIND_DATAA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    CHAR cFileName[MAX_PATH];
    CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, *PWIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;

/* windows' sizes: the sdk's old mac fields only exist under _MAC. */
MIME_STATIC_ASSERT(sizeof(WIN32_FIND_DATAW) == 592 && sizeof(WIN32_FIND_DATAA) == 320, "find data layout");

typedef enum _FINDEX_INFO_LEVELS {
    FindExInfoStandard,
    FindExInfoBasic,
    FindExInfoMaxInfoLevel
} FINDEX_INFO_LEVELS;

typedef enum _FINDEX_SEARCH_OPS {
    FindExSearchNameMatch,
    FindExSearchLimitToDirectories,
    FindExSearchLimitToDevices,
    FindExSearchMaxSearchOp
} FINDEX_SEARCH_OPS;

#define FIND_FIRST_EX_CASE_SENSITIVE 0x00000001
#define FIND_FIRST_EX_LARGE_FETCH 0x00000002
#define FIND_FIRST_EX_ON_DISK_ENTRIES_ONLY 0x00000004

typedef enum _GET_FILEEX_INFO_LEVELS {
    GetFileExInfoStandard,
    GetFileExMaxInfoLevel
} GET_FILEEX_INFO_LEVELS;

typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA, *LPWIN32_FILE_ATTRIBUTE_DATA;

typedef enum _FILE_INFO_BY_HANDLE_CLASS {
    FileBasicInfo,
    FileStandardInfo,
    FileNameInfo,
    FileRenameInfo,
    FileDispositionInfo,
    FileAllocationInfo,
    FileEndOfFileInfo,
    FileStreamInfo,
    FileCompressionInfo,
    FileAttributeTagInfo,
    FileIdBothDirectoryInfo,
    FileIdBothDirectoryRestartInfo,
    FileIoPriorityHintInfo,
    FileRemoteProtocolInfo,
    FileFullDirectoryInfo,
    FileFullDirectoryRestartInfo,
    FileStorageInfo,
    FileAlignmentInfo,
    FileIdInfo,
    FileIdExtdDirectoryInfo,
    FileIdExtdDirectoryRestartInfo,
    FileDispositionInfoEx,
    FileRenameInfoEx,
    MaximumFileInfoByHandleClass
} FILE_INFO_BY_HANDLE_CLASS;

typedef struct _FILE_BASIC_INFO {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    DWORD FileAttributes;
} FILE_BASIC_INFO, *PFILE_BASIC_INFO;

typedef struct _FILE_STANDARD_INFO {
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    DWORD NumberOfLinks;
    BOOLEAN DeletePending;
    BOOLEAN Directory;
} FILE_STANDARD_INFO, *PFILE_STANDARD_INFO;

typedef struct _FILE_NAME_INFO {
    DWORD FileNameLength;
    WCHAR FileName[1];
} FILE_NAME_INFO, *PFILE_NAME_INFO;

typedef struct _FILE_RENAME_INFO {
    union {
        BOOLEAN ReplaceIfExists;
        DWORD Flags;
    };
    HANDLE RootDirectory;
    DWORD FileNameLength;
    WCHAR FileName[1];
} FILE_RENAME_INFO, *PFILE_RENAME_INFO;

typedef struct _FILE_DISPOSITION_INFO {
    BOOLEAN DeleteFile;
} FILE_DISPOSITION_INFO, *PFILE_DISPOSITION_INFO;

typedef struct _FILE_ALLOCATION_INFO {
    LARGE_INTEGER AllocationSize;
} FILE_ALLOCATION_INFO, *PFILE_ALLOCATION_INFO;

typedef struct _FILE_END_OF_FILE_INFO {
    LARGE_INTEGER EndOfFile;
} FILE_END_OF_FILE_INFO, *PFILE_END_OF_FILE_INFO;

typedef struct _FILE_ATTRIBUTE_TAG_INFO {
    DWORD FileAttributes;
    DWORD ReparseTag;
} FILE_ATTRIBUTE_TAG_INFO, *PFILE_ATTRIBUTE_TAG_INFO;

typedef struct _FILE_ID_128 {
    BYTE Identifier[16];
} FILE_ID_128, *PFILE_ID_128;

typedef struct _FILE_ID_INFO {
    ULONGLONG VolumeSerialNumber;
    FILE_ID_128 FileId;
} FILE_ID_INFO, *PFILE_ID_INFO;

typedef enum _FILE_ID_TYPE {
    FileIdType,
    ObjectIdType,
    ExtendedFileIdType,
    MaximumFileIdType
} FILE_ID_TYPE;

typedef struct FILE_ID_DESCRIPTOR {
    DWORD dwSize;
    FILE_ID_TYPE Type;
    union {
        LARGE_INTEGER FileId;
        GUID ObjectId;
        FILE_ID_128 ExtendedFileId;
    };
} FILE_ID_DESCRIPTOR, *LPFILE_ID_DESCRIPTOR;

typedef struct _FILE_ALIGNMENT_INFO {
    ULONG AlignmentRequirement;
} FILE_ALIGNMENT_INFO, *PFILE_ALIGNMENT_INFO;

typedef struct _FILE_STORAGE_INFO {
    ULONG LogicalBytesPerSector;
    ULONG PhysicalBytesPerSectorForAtomicity;
    ULONG PhysicalBytesPerSectorForPerformance;
    ULONG FileSystemEffectivePhysicalBytesPerSectorForAtomicity;
    ULONG Flags;
    ULONG ByteOffsetForSectorAlignment;
    ULONG ByteOffsetForPartitionAlignment;
} FILE_STORAGE_INFO, *PFILE_STORAGE_INFO;

#define FILE_NAME_NORMALIZED 0x0
#define FILE_NAME_OPENED 0x8
#define VOLUME_NAME_DOS 0x0
#define VOLUME_NAME_GUID 0x1
#define VOLUME_NAME_NT 0x2
#define VOLUME_NAME_NONE 0x4

#define DRIVE_UNKNOWN 0
#define DRIVE_NO_ROOT_DIR 1
#define DRIVE_REMOVABLE 2
#define DRIVE_FIXED 3
#define DRIVE_REMOTE 4
#define DRIVE_CDROM 5
#define DRIVE_RAMDISK 6

#define FILE_CASE_SENSITIVE_SEARCH 0x00000001
#define FILE_CASE_PRESERVED_NAMES 0x00000002
#define FILE_UNICODE_ON_DISK 0x00000004
#define FILE_PERSISTENT_ACLS 0x00000008
#define FILE_FILE_COMPRESSION 0x00000010
#define FILE_SUPPORTS_SPARSE_FILES 0x00000040
#define FILE_SUPPORTS_REPARSE_POINTS 0x00000080

WINBASEAPI HANDLE WINAPI CreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                     DWORD disposition, DWORD flags, HANDLE template_file);
WINBASEAPI HANDLE WINAPI CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                     DWORD disposition, DWORD flags, HANDLE template_file);
typedef struct _CREATEFILE2_EXTENDED_PARAMETERS {
    DWORD dwSize;
    DWORD dwFileAttributes;
    DWORD dwFileFlags;
    DWORD dwSecurityQosFlags;
    LPSECURITY_ATTRIBUTES lpSecurityAttributes;
    HANDLE hTemplateFile;
} CREATEFILE2_EXTENDED_PARAMETERS, *PCREATEFILE2_EXTENDED_PARAMETERS, *LPCREATEFILE2_EXTENDED_PARAMETERS;
WINBASEAPI HANDLE WINAPI CreateFile2(LPCWSTR name, DWORD access, DWORD share, DWORD disposition,
                                     LPCREATEFILE2_EXTENDED_PARAMETERS params);
/* OpenFile and its OFSTRUCT. */
typedef struct _OFSTRUCT {
    BYTE cBytes;
    BYTE fFixedDisk;
    WORD nErrCode;
    WORD Reserved1;
    WORD Reserved2;
    CHAR szPathName[128];
} OFSTRUCT, *LPOFSTRUCT, *POFSTRUCT;
#define OF_READ 0x0000
#define OF_WRITE 0x0001
#define OF_READWRITE 0x0002
#define OF_SHARE_COMPAT 0x0000
#define OF_SHARE_EXCLUSIVE 0x0010
#define OF_SHARE_DENY_WRITE 0x0020
#define OF_SHARE_DENY_READ 0x0030
#define OF_SHARE_DENY_NONE 0x0040
#define OF_PARSE 0x0100
#define OF_DELETE 0x0200
#define OF_VERIFY 0x0400
#define OF_CANCEL 0x0800
#define OF_CREATE 0x1000
#define OF_PROMPT 0x2000
#define OF_EXIST 0x4000
#define OF_REOPEN 0x8000
WINBASEAPI HFILE WINAPI OpenFile(LPCSTR name, LPOFSTRUCT buffer, UINT style);
WINBASEAPI HFILE WINAPI _lopen(LPCSTR path, int mode);
WINBASEAPI HFILE WINAPI _lcreat(LPCSTR path, int attributes);
WINBASEAPI HFILE WINAPI _lclose(HFILE file);
WINBASEAPI UINT WINAPI _lread(HFILE file, LPVOID buffer, UINT count);
WINBASEAPI UINT WINAPI _lwrite(HFILE file, LPCCH buffer, UINT count);
WINBASEAPI BOOL WINAPI GetDevicePowerState(HANDLE device, BOOL *on);
WINBASEAPI HANDLE WINAPI FindFirstVolumeMountPointW(LPCWSTR root, LPWSTR name, DWORD length);
WINBASEAPI HANDLE WINAPI FindFirstVolumeMountPointA(LPCSTR root, LPSTR name, DWORD length);
WINBASEAPI BOOL WINAPI FindNextVolumeMountPointW(HANDLE handle, LPWSTR name, DWORD length);
WINBASEAPI BOOL WINAPI FindNextVolumeMountPointA(HANDLE handle, LPSTR name, DWORD length);
WINBASEAPI BOOL WINAPI FindVolumeMountPointClose(HANDLE handle);
WINBASEAPI BOOL WINAPI SetVolumeMountPointW(LPCWSTR mount_point, LPCWSTR volume);
WINBASEAPI BOOL WINAPI SetVolumeMountPointA(LPCSTR mount_point, LPCSTR volume);
WINBASEAPI BOOL WINAPI DeleteVolumeMountPointW(LPCWSTR mount_point);
WINBASEAPI BOOL WINAPI DeleteVolumeMountPointA(LPCSTR mount_point);
WINBASEAPI LONG WINAPI _hread(HFILE file, LPVOID buffer, LONG count);
WINBASEAPI LONG WINAPI _hwrite(HFILE file, LPCCH buffer, LONG count);
WINBASEAPI LONG WINAPI _llseek(HFILE file, LONG offset, int origin);
WINBASEAPI BOOL WINAPI ReadFile(HANDLE file, LPVOID buffer, DWORD length, LPDWORD read, LPOVERLAPPED overlapped);
/* an element of the array of a scatter or gather request holds the address of a page in 64 bits on every processor. */
typedef union _FILE_SEGMENT_ELEMENT {
    ULONGLONG Buffer;
    ULONGLONG Alignment;
} FILE_SEGMENT_ELEMENT, *PFILE_SEGMENT_ELEMENT;
WINBASEAPI BOOL WINAPI ReadFileScatter(HANDLE file, PFILE_SEGMENT_ELEMENT segments, DWORD bytes, LPDWORD reserved,
                                       LPOVERLAPPED overlapped);
WINBASEAPI BOOL WINAPI WriteFileGather(HANDLE file, PFILE_SEGMENT_ELEMENT segments, DWORD bytes, LPDWORD reserved,
                                       LPOVERLAPPED overlapped);
WINBASEAPI BOOL WINAPI WriteFile(HANDLE file, LPCVOID buffer, DWORD length, LPDWORD written,
                                 LPOVERLAPPED overlapped);
WINBASEAPI BOOL WINAPI ReadFileEx(HANDLE file, LPVOID buffer, DWORD length, LPOVERLAPPED overlapped,
                                  LPOVERLAPPED_COMPLETION_ROUTINE completion);
WINBASEAPI BOOL WINAPI WriteFileEx(HANDLE file, LPCVOID buffer, DWORD length, LPOVERLAPPED overlapped,
                                   LPOVERLAPPED_COMPLETION_ROUTINE completion);
WINBASEAPI BOOL WINAPI GetOverlappedResult(HANDLE file, LPOVERLAPPED overlapped, LPDWORD transferred, BOOL wait);
WINBASEAPI BOOL WINAPI DeviceIoControl(HANDLE device, DWORD code, LPVOID in, DWORD in_size, LPVOID out, DWORD out_size,
                                      LPDWORD returned, LPOVERLAPPED overlapped);

/* pipes. */
#define PIPE_ACCESS_INBOUND 0x00000001
#define PIPE_ACCESS_OUTBOUND 0x00000002
#define PIPE_ACCESS_DUPLEX 0x00000003
#define FILE_FLAG_FIRST_PIPE_INSTANCE 0x00080000
#define PIPE_WAIT 0x00000000
#define PIPE_NOWAIT 0x00000001
#define PIPE_READMODE_BYTE 0x00000000
#define PIPE_READMODE_MESSAGE 0x00000002
#define PIPE_TYPE_BYTE 0x00000000
#define PIPE_TYPE_MESSAGE 0x00000004
#define PIPE_ACCEPT_REMOTE_CLIENTS 0x00000000
#define PIPE_REJECT_REMOTE_CLIENTS 0x00000008
#define PIPE_CLIENT_END 0x00000000
#define PIPE_SERVER_END 0x00000001
#define PIPE_UNLIMITED_INSTANCES 255
#define NMPWAIT_WAIT_FOREVER 0xFFFFFFFF
#define NMPWAIT_NOWAIT 0x00000001
#define NMPWAIT_USE_DEFAULT_WAIT 0x00000000
WINBASEAPI BOOL WINAPI CreatePipe(PHANDLE read_pipe, PHANDLE write_pipe, LPSECURITY_ATTRIBUTES sa, DWORD size);
WINBASEAPI HANDLE WINAPI CreateNamedPipeW(LPCWSTR name, DWORD open_mode, DWORD pipe_mode, DWORD max_instances,
                                          DWORD out_size, DWORD in_size, DWORD timeout, LPSECURITY_ATTRIBUTES sa);
WINBASEAPI HANDLE WINAPI CreateNamedPipeA(LPCSTR name, DWORD open_mode, DWORD pipe_mode, DWORD max_instances,
                                          DWORD out_size, DWORD in_size, DWORD timeout, LPSECURITY_ATTRIBUTES sa);
WINBASEAPI BOOL WINAPI ConnectNamedPipe(HANDLE pipe, LPOVERLAPPED overlapped);
WINBASEAPI BOOL WINAPI DisconnectNamedPipe(HANDLE pipe);
WINBASEAPI BOOL WINAPI PeekNamedPipe(HANDLE pipe, LPVOID buffer, DWORD size, LPDWORD read, LPDWORD available,
                                     LPDWORD left);
WINBASEAPI BOOL WINAPI SetNamedPipeHandleState(HANDLE pipe, LPDWORD mode, LPDWORD max_collection,
                                               LPDWORD collect_timeout);
typedef struct _IO_COUNTERS {
    ULONGLONG ReadOperationCount;
    ULONGLONG WriteOperationCount;
    ULONGLONG OtherOperationCount;
    ULONGLONG ReadTransferCount;
    ULONGLONG WriteTransferCount;
    ULONGLONG OtherTransferCount;
} IO_COUNTERS, *PIO_COUNTERS;

#define FILE_SKIP_COMPLETION_PORT_ON_SUCCESS 0x1
#define FILE_SKIP_SET_EVENT_ON_HANDLE 0x2
#define WT_EXECUTEDEFAULT 0x00000000
#define WT_EXECUTEINIOTHREAD 0x00000001
#define WT_EXECUTELONGFUNCTION 0x00000010
#define WT_EXECUTEINWAITTHREAD 0x00000004
#define WT_EXECUTEONLYONCE 0x00000008

typedef void(CALLBACK *WAITORTIMERCALLBACK)(PVOID context, BOOLEAN timed_out);
WINBASEAPI BOOL WINAPI RegisterWaitForSingleObject(PHANDLE result, HANDLE object, WAITORTIMERCALLBACK callback,
                                                   PVOID context, ULONG timeout, ULONG flags);
WINBASEAPI BOOL WINAPI UnregisterWaitEx(HANDLE handle, HANDLE completion);

WINBASEAPI HANDLE WINAPI ReOpenFile(HANDLE original, DWORD access, DWORD share, DWORD flags);
WINBASEAPI BOOL WINAPI SetFileCompletionNotificationModes(HANDLE file, UCHAR flags);
WINBASEAPI BOOL WINAPI GetProcessIoCounters(HANDLE process, PIO_COUNTERS counters);
WINBASEAPI BOOL WINAPI GetNamedPipeClientProcessId(HANDLE pipe, PULONG id);
WINBASEAPI BOOL WINAPI GetNamedPipeClientComputerNameW(HANDLE pipe, LPWSTR name, ULONG length);
WINBASEAPI BOOL WINAPI GetNamedPipeServerProcessId(HANDLE pipe, PULONG id);
WINBASEAPI BOOL WINAPI GetNamedPipeClientSessionId(HANDLE pipe, PULONG id);
WINBASEAPI BOOL WINAPI GetNamedPipeServerSessionId(HANDLE pipe, PULONG id);
WINBASEAPI BOOL WINAPI GetNamedPipeHandleStateA(HANDLE pipe, LPDWORD state, LPDWORD instances, LPDWORD max_collection,
                                                LPDWORD collect_timeout, LPSTR user, DWORD user_size);
WINBASEAPI BOOL WINAPI GetNamedPipeInfo(HANDLE pipe, LPDWORD flags, LPDWORD out_size, LPDWORD in_size,
                                        LPDWORD max_instances);
WINBASEAPI BOOL WINAPI GetNamedPipeHandleStateW(HANDLE pipe, LPDWORD state, LPDWORD instances,
                                                LPDWORD max_collection, LPDWORD collect_timeout, LPWSTR user,
                                                DWORD user_size);
WINBASEAPI BOOL WINAPI WaitNamedPipeW(LPCWSTR name, DWORD timeout);
WINBASEAPI BOOL WINAPI WaitNamedPipeA(LPCSTR name, DWORD timeout);

/* completion ports. */
WINBASEAPI HANDLE WINAPI CreateIoCompletionPort(HANDLE file, HANDLE existing, ULONG_PTR key, DWORD concurrency);
WINBASEAPI BOOL WINAPI GetQueuedCompletionStatus(HANDLE port, LPDWORD bytes, PULONG_PTR key, LPOVERLAPPED *overlapped,
                                                 DWORD timeout);
WINBASEAPI BOOL WINAPI GetQueuedCompletionStatusEx(HANDLE port, LPOVERLAPPED_ENTRY entries, ULONG count,
                                                   PULONG removed, DWORD timeout, BOOL alertable);
WINBASEAPI BOOL WINAPI PostQueuedCompletionStatus(HANDLE port, DWORD bytes, ULONG_PTR key, LPOVERLAPPED overlapped);
WINBASEAPI BOOL WINAPI GetOverlappedResultEx(HANDLE file, LPOVERLAPPED overlapped, LPDWORD transferred,
                                             DWORD timeout, BOOL alertable);
WINBASEAPI BOOL WINAPI CancelIo(HANDLE file);
WINBASEAPI BOOL WINAPI CancelIoEx(HANDLE file, LPOVERLAPPED overlapped);
WINBASEAPI BOOL WINAPI CancelSynchronousIo(HANDLE thread);
WINBASEAPI DWORD WINAPI GetFileSize(HANDLE file, LPDWORD high);
WINBASEAPI BOOL WINAPI GetFileSizeEx(HANDLE file, PLARGE_INTEGER size);
WINBASEAPI DWORD WINAPI SetFilePointer(HANDLE file, LONG distance, PLONG high, DWORD method);
WINBASEAPI BOOL WINAPI SetFilePointerEx(HANDLE file, LARGE_INTEGER distance, PLARGE_INTEGER position, DWORD method);
WINBASEAPI BOOL WINAPI SetEndOfFile(HANDLE file);
WINBASEAPI BOOL WINAPI SetFileValidData(HANDLE file, LONGLONG length);
WINBASEAPI BOOL WINAPI FlushFileBuffers(HANDLE file);
WINBASEAPI DWORD WINAPI GetFileType(HANDLE file);
#define LOCKFILE_FAIL_IMMEDIATELY 0x00000001
#define LOCKFILE_EXCLUSIVE_LOCK 0x00000002
WINBASEAPI BOOL WINAPI LockFile(HANDLE file, DWORD offset_low, DWORD offset_high, DWORD length_low,
                                DWORD length_high);
WINBASEAPI BOOL WINAPI LockFileEx(HANDLE file, DWORD flags, DWORD reserved, DWORD length_low, DWORD length_high,
                                  LPOVERLAPPED overlapped);
WINBASEAPI BOOL WINAPI UnlockFile(HANDLE file, DWORD offset_low, DWORD offset_high, DWORD length_low,
                                  DWORD length_high);
WINBASEAPI BOOL WINAPI UnlockFileEx(HANDLE file, DWORD reserved, DWORD length_low, DWORD length_high,
                                    LPOVERLAPPED overlapped);
WINBASEAPI DWORD WINAPI GetFileAttributesW(LPCWSTR name);
WINBASEAPI DWORD WINAPI GetFileAttributesA(LPCSTR name);
WINBASEAPI BOOL WINAPI GetFileAttributesExW(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info);
WINBASEAPI BOOL WINAPI GetFileAttributesExA(LPCSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info);
WINBASEAPI BOOL WINAPI SetFileAttributesW(LPCWSTR name, DWORD attributes);
WINBASEAPI BOOL WINAPI SetFileAttributesA(LPCSTR name, DWORD attributes);
WINBASEAPI BOOL WINAPI GetFileInformationByHandle(HANDLE file, LPBY_HANDLE_FILE_INFORMATION info);
WINBASEAPI BOOL WINAPI GetFileInformationByHandleEx(HANDLE file, FILE_INFO_BY_HANDLE_CLASS klass, LPVOID info,
                                                    DWORD size);
WINBASEAPI BOOL WINAPI SetFileInformationByHandle(HANDLE file, FILE_INFO_BY_HANDLE_CLASS klass, LPVOID info,
                                                  DWORD size);
WINBASEAPI DWORD WINAPI GetFinalPathNameByHandleW(HANDLE file, LPWSTR path, DWORD size, DWORD flags);
WINBASEAPI DWORD WINAPI GetFinalPathNameByHandleA(HANDLE file, LPSTR path, DWORD size, DWORD flags);
WINBASEAPI BOOL WINAPI GetFileTime(HANDLE file, LPFILETIME creation, LPFILETIME access, LPFILETIME write);
WINBASEAPI BOOL WINAPI SetFileTime(HANDLE file, const FILETIME *creation, const FILETIME *access,
                                   const FILETIME *write);
WINBASEAPI BOOL WINAPI DeleteFileW(LPCWSTR name);
WINBASEAPI BOOL WINAPI DeleteFileA(LPCSTR name);
WINBASEAPI BOOL WINAPI CreateDirectoryW(LPCWSTR name, LPSECURITY_ATTRIBUTES sa);
WINBASEAPI BOOL WINAPI CreateDirectoryA(LPCSTR name, LPSECURITY_ATTRIBUTES sa);
WINBASEAPI BOOL WINAPI CreateDirectoryExW(LPCWSTR template_dir, LPCWSTR name, LPSECURITY_ATTRIBUTES sa);
WINBASEAPI BOOL WINAPI RemoveDirectoryW(LPCWSTR name);
WINBASEAPI BOOL WINAPI RemoveDirectoryA(LPCSTR name);
WINBASEAPI BOOL WINAPI MoveFileW(LPCWSTR source, LPCWSTR destination);
WINBASEAPI BOOL WINAPI MoveFileA(LPCSTR source, LPCSTR destination);
WINBASEAPI BOOL WINAPI MoveFileExW(LPCWSTR source, LPCWSTR destination, DWORD flags);
WINBASEAPI BOOL WINAPI MoveFileExA(LPCSTR source, LPCSTR destination, DWORD flags);
WINBASEAPI BOOL WINAPI MoveFileWithProgressW(LPCWSTR source, LPCWSTR destination, LPPROGRESS_ROUTINE progress,
                                             LPVOID data, DWORD flags);
WINBASEAPI BOOL WINAPI ReplaceFileW(LPCWSTR replaced, LPCWSTR replacement, LPCWSTR backup, DWORD flags,
                                    LPVOID exclude, LPVOID reserved);
WINBASEAPI BOOL WINAPI CopyFileW(LPCWSTR source, LPCWSTR destination, BOOL fail_if_exists);
WINBASEAPI BOOL WINAPI CopyFileA(LPCSTR source, LPCSTR destination, BOOL fail_if_exists);
WINBASEAPI BOOL WINAPI CopyFileExW(LPCWSTR source, LPCWSTR destination, LPPROGRESS_ROUTINE progress, LPVOID data,
                                   LPBOOL cancel, DWORD flags);
WINBASEAPI BOOL WINAPI CreateHardLinkW(LPCWSTR link, LPCWSTR existing, LPSECURITY_ATTRIBUTES sa);
WINBASEAPI HANDLE WINAPI OpenFileById(HANDLE volume, LPFILE_ID_DESCRIPTOR id, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD flags);
WINBASEAPI BOOL WINAPI CreateHardLinkA(LPCSTR link, LPCSTR existing, LPSECURITY_ATTRIBUTES sa);
#define SYMBOLIC_LINK_FLAG_DIRECTORY 0x1
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
WINBASEAPI BOOLEAN WINAPI CreateSymbolicLinkW(LPCWSTR link, LPCWSTR target, DWORD flags);
WINBASEAPI BOOLEAN WINAPI CreateSymbolicLinkA(LPCSTR link, LPCSTR target, DWORD flags);
WINBASEAPI BOOL WINAPI ReplaceFileA(LPCSTR replaced, LPCSTR replacement, LPCSTR backup, DWORD flags, LPVOID exclude,
                                    LPVOID reserved);
WINBASEAPI BOOL WINAPI SetVolumeLabelW(LPCWSTR root, LPCWSTR label);
WINBASEAPI BOOL WINAPI SetVolumeLabelA(LPCSTR root, LPCSTR label);

typedef enum _STREAM_INFO_LEVELS {
    FindStreamInfoStandard,
    FindStreamInfoMaxInfoLevel
} STREAM_INFO_LEVELS;

typedef struct _WIN32_FIND_STREAM_DATA {
    LARGE_INTEGER StreamSize;
    WCHAR cStreamName[MAX_PATH + 36];
} WIN32_FIND_STREAM_DATA, *PWIN32_FIND_STREAM_DATA;

WINBASEAPI HANDLE WINAPI FindFirstStreamW(LPCWSTR path, STREAM_INFO_LEVELS level, LPVOID data, DWORD flags);
WINBASEAPI BOOL WINAPI FindNextStreamW(HANDLE find, LPVOID data);

#define BACKUP_INVALID 0x00000000
#define BACKUP_DATA 0x00000001
#define BACKUP_EA_DATA 0x00000002
#define BACKUP_SECURITY_DATA 0x00000003
#define BACKUP_ALTERNATE_DATA 0x00000004
#define BACKUP_LINK 0x00000005
#define BACKUP_PROPERTY_DATA 0x00000006
#define BACKUP_OBJECT_ID 0x00000007
#define BACKUP_REPARSE_DATA 0x00000008
#define BACKUP_SPARSE_BLOCK 0x00000009
#define BACKUP_TXFS_DATA 0x0000000A

typedef struct _WIN32_STREAM_ID {
    DWORD dwStreamId;
    DWORD dwStreamAttributes;
    LARGE_INTEGER Size;
    DWORD dwStreamNameSize;
    WCHAR cStreamName[1];
} WIN32_STREAM_ID, *LPWIN32_STREAM_ID;

WINBASEAPI BOOL WINAPI BackupRead(HANDLE file, LPBYTE buffer, DWORD size, LPDWORD read, BOOL abort,
                                  BOOL process_security, LPVOID *context);
WINBASEAPI BOOL WINAPI BackupSeek(HANDLE file, DWORD low, DWORD high, LPDWORD low_seeked, LPDWORD high_seeked,
                                  LPVOID *context);
WINBASEAPI BOOL WINAPI BackupWrite(HANDLE file, LPBYTE buffer, DWORD size, LPDWORD written, BOOL abort,
                                   BOOL process_security, LPVOID *context);

typedef struct _LDT_ENTRY {
    WORD LimitLow;
    WORD BaseLow;
    union {
        struct {
            BYTE BaseMid;
            BYTE Flags1;
            BYTE Flags2;
            BYTE BaseHi;
        } Bytes;
        struct {
            DWORD BaseMid : 8;
            DWORD Type : 5;
            DWORD Dpl : 2;
            DWORD Pres : 1;
            DWORD LimitHi : 4;
            DWORD Sys : 1;
            DWORD Reserved_0 : 1;
            DWORD Default_Big : 1;
            DWORD Granularity : 1;
            DWORD BaseHi : 8;
        } Bits;
    } HighWord;
} LDT_ENTRY, *PLDT_ENTRY, *LPLDT_ENTRY;

WINBASEAPI BOOL WINAPI GetThreadSelectorEntry(HANDLE thread, DWORD selector, LPLDT_ENTRY entry);
WINBASEAPI HANDLE WINAPI CreateWaitableTimerExA(LPSECURITY_ATTRIBUTES sa, LPCSTR name, DWORD flags, DWORD access);
WINBASEAPI HANDLE WINAPI FindFirstFileW(LPCWSTR name, LPWIN32_FIND_DATAW data);
WINBASEAPI HANDLE WINAPI FindFirstFileA(LPCSTR name, LPWIN32_FIND_DATAA data);
WINBASEAPI HANDLE WINAPI FindFirstFileExW(LPCWSTR name, FINDEX_INFO_LEVELS level, LPVOID data,
                                          FINDEX_SEARCH_OPS op, LPVOID filter, DWORD flags);
WINBASEAPI HANDLE WINAPI FindFirstFileExA(LPCSTR name, FINDEX_INFO_LEVELS level, LPVOID data,
                                          FINDEX_SEARCH_OPS op, LPVOID filter, DWORD flags);
WINBASEAPI BOOL WINAPI FindNextFileW(HANDLE find, LPWIN32_FIND_DATAW data);
WINBASEAPI BOOL WINAPI FindNextFileA(HANDLE find, LPWIN32_FIND_DATAA data);
WINBASEAPI BOOL WINAPI FindClose(HANDLE find);
WINBASEAPI HANDLE WINAPI FindFirstChangeNotificationW(LPCWSTR path, BOOL subtree, DWORD filter);
WINBASEAPI HANDLE WINAPI FindFirstChangeNotificationA(LPCSTR path, BOOL subtree, DWORD filter);
WINBASEAPI BOOL WINAPI ReadDirectoryChangesW(HANDLE directory, LPVOID buffer, DWORD length, BOOL subtree, DWORD filter,
                                             LPDWORD returned, LPOVERLAPPED overlapped,
                                             LPOVERLAPPED_COMPLETION_ROUTINE routine);

#define FILE_NOTIFY_CHANGE_FILE_NAME 0x00000001
#define FILE_NOTIFY_CHANGE_DIR_NAME 0x00000002
#define FILE_NOTIFY_CHANGE_ATTRIBUTES 0x00000004
#define FILE_NOTIFY_CHANGE_SIZE 0x00000008
#define FILE_NOTIFY_CHANGE_LAST_WRITE 0x00000010
#define FILE_NOTIFY_CHANGE_LAST_ACCESS 0x00000020
#define FILE_NOTIFY_CHANGE_CREATION 0x00000040
#define FILE_NOTIFY_CHANGE_SECURITY 0x00000100
#define FILE_ACTION_ADDED 0x00000001
#define FILE_ACTION_REMOVED 0x00000002
#define FILE_ACTION_MODIFIED 0x00000003
#define FILE_ACTION_RENAMED_OLD_NAME 0x00000004
#define FILE_ACTION_RENAMED_NEW_NAME 0x00000005

typedef struct _FILE_NOTIFY_INFORMATION {
    DWORD NextEntryOffset;
    DWORD Action;
    DWORD FileNameLength;
    WCHAR FileName[1];
} FILE_NOTIFY_INFORMATION, *PFILE_NOTIFY_INFORMATION;
WINBASEAPI BOOL WINAPI FindNextChangeNotification(HANDLE handle);
WINBASEAPI BOOL WINAPI FindCloseChangeNotification(HANDLE handle);
WINBASEAPI BOOL WINAPI AreFileApisANSI(void);
WINBASEAPI void WINAPI SetFileApisToANSI(void);
WINBASEAPI void WINAPI SetFileApisToOEM(void);

/* ------------------------------------------------------------------ paths and volumes */

WINBASEAPI DWORD WINAPI GetFullPathNameW(LPCWSTR name, DWORD size, LPWSTR buffer, LPWSTR *file_part);
WINBASEAPI DWORD WINAPI GetFullPathNameA(LPCSTR name, DWORD size, LPSTR buffer, LPSTR *file_part);
WINBASEAPI DWORD WINAPI GetLongPathNameW(LPCWSTR short_path, LPWSTR long_path, DWORD size);
WINBASEAPI DWORD WINAPI GetLongPathNameA(LPCSTR short_path, LPSTR long_path, DWORD size);
WINBASEAPI DWORD WINAPI GetShortPathNameW(LPCWSTR long_path, LPWSTR short_path, DWORD size);
WINBASEAPI DWORD WINAPI GetShortPathNameA(LPCSTR long_path, LPSTR short_path, DWORD size);
WINBASEAPI DWORD WINAPI GetCurrentDirectoryW(DWORD size, LPWSTR buffer);
WINBASEAPI DWORD WINAPI GetCurrentDirectoryA(DWORD size, LPSTR buffer);
WINBASEAPI BOOL WINAPI SetCurrentDirectoryW(LPCWSTR path);
WINBASEAPI BOOL WINAPI SetCurrentDirectoryA(LPCSTR path);
WINBASEAPI UINT WINAPI GetSystemDirectoryW(LPWSTR buffer, UINT size);
WINBASEAPI UINT WINAPI GetSystemDirectoryA(LPSTR buffer, UINT size);
WINBASEAPI UINT WINAPI GetWindowsDirectoryW(LPWSTR buffer, UINT size);
WINBASEAPI UINT WINAPI GetWindowsDirectoryA(LPSTR buffer, UINT size);
WINBASEAPI UINT WINAPI GetSystemWindowsDirectoryW(LPWSTR buffer, UINT size);
WINBASEAPI UINT WINAPI GetSystemWindowsDirectoryA(LPSTR buffer, UINT size);
WINBASEAPI UINT WINAPI GetSystemWow64DirectoryW(LPWSTR buffer, UINT size);
WINBASEAPI UINT WINAPI GetSystemWow64DirectoryA(LPSTR buffer, UINT size);
WINBASEAPI DWORD WINAPI GetTempPathW(DWORD size, LPWSTR buffer);
WINBASEAPI DWORD WINAPI GetTempPathA(DWORD size, LPSTR buffer);
WINBASEAPI DWORD WINAPI GetTempPath2W(DWORD size, LPWSTR buffer);
WINBASEAPI UINT WINAPI GetTempFileNameW(LPCWSTR path, LPCWSTR prefix, UINT unique, LPWSTR buffer);
WINBASEAPI UINT WINAPI GetTempFileNameA(LPCSTR path, LPCSTR prefix, UINT unique, LPSTR buffer);
WINBASEAPI DWORD WINAPI SearchPathW(LPCWSTR path, LPCWSTR name, LPCWSTR extension, DWORD size, LPWSTR buffer,
                                    LPWSTR *file_part);
WINBASEAPI DWORD WINAPI SearchPathA(LPCSTR path, LPCSTR name, LPCSTR extension, DWORD size, LPSTR buffer,
                                    LPSTR *file_part);
WINBASEAPI BOOL WINAPI NeedCurrentDirectoryForExePathW(LPCWSTR name);
WINBASEAPI UINT WINAPI GetDriveTypeW(LPCWSTR root);
WINBASEAPI UINT WINAPI GetDriveTypeA(LPCSTR root);
WINBASEAPI DWORD WINAPI GetLogicalDrives(void);
WINBASEAPI DWORD WINAPI GetLogicalDriveStringsW(DWORD size, LPWSTR buffer);
WINBASEAPI DWORD WINAPI GetLogicalDriveStringsA(DWORD size, LPSTR buffer);
WINBASEAPI BOOL WINAPI GetVolumeInformationW(LPCWSTR root, LPWSTR name, DWORD name_size, LPDWORD serial,
                                             LPDWORD max_component, LPDWORD flags, LPWSTR fs_name,
                                             DWORD fs_name_size);
WINBASEAPI BOOL WINAPI GetVolumeInformationA(LPCSTR root, LPSTR name, DWORD name_size, LPDWORD serial,
                                             LPDWORD max_component, LPDWORD flags, LPSTR fs_name,
                                             DWORD fs_name_size);
WINBASEAPI BOOL WINAPI GetVolumeInformationByHandleW(HANDLE file, LPWSTR name, DWORD name_size, LPDWORD serial,
                                                     LPDWORD max_component, LPDWORD flags, LPWSTR fs_name,
                                                     DWORD fs_name_size);
WINBASEAPI BOOL WINAPI GetVolumePathNameW(LPCWSTR path, LPWSTR volume, DWORD size);
WINBASEAPI BOOL WINAPI GetDiskFreeSpaceExW(LPCWSTR root, PULARGE_INTEGER available, PULARGE_INTEGER total,
                                           PULARGE_INTEGER free);
WINBASEAPI BOOL WINAPI GetDiskFreeSpaceExA(LPCSTR root, PULARGE_INTEGER available, PULARGE_INTEGER total,
                                           PULARGE_INTEGER free);
WINBASEAPI BOOL WINAPI GetDiskFreeSpaceW(LPCWSTR root, LPDWORD sectors_per_cluster, LPDWORD bytes_per_sector,
                                         LPDWORD free_clusters, LPDWORD total_clusters);
WINBASEAPI BOOL WINAPI GetDiskFreeSpaceA(LPCSTR root, LPDWORD sectors_per_cluster, LPDWORD bytes_per_sector,
                                         LPDWORD free_clusters, LPDWORD total_clusters);
WINBASEAPI DWORD WINAPI QueryDosDeviceW(LPCWSTR device, LPWSTR target, DWORD size);
WINBASEAPI DWORD WINAPI QueryDosDeviceA(LPCSTR device, LPSTR target, DWORD size);
#define DDD_RAW_TARGET_PATH 0x00000001
#define DDD_REMOVE_DEFINITION 0x00000002
#define DDD_EXACT_MATCH_ON_REMOVE 0x00000004
#define DDD_NO_BROADCAST_SYSTEM 0x00000008
#define DDD_LUID_BROADCAST_DRIVE 0x00000010
WINBASEAPI BOOL WINAPI DefineDosDeviceW(DWORD flags, LPCWSTR device, LPCWSTR target);
WINBASEAPI BOOL WINAPI DefineDosDeviceA(DWORD flags, LPCSTR device, LPCSTR target);
WINBASEAPI BOOL WINAPI Wow64DisableWow64FsRedirection(PVOID *old);
WINBASEAPI BOOL WINAPI Wow64RevertWow64FsRedirection(PVOID old);
WINBASEAPI BOOLEAN WINAPI Wow64EnableWow64FsRedirection(BOOLEAN enable);

/* ------------------------------------------------------------------ console */

#define STD_INPUT_HANDLE ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE ((DWORD)-12)
#define ATTACH_PARENT_PROCESS ((DWORD)-1)

#define ENABLE_PROCESSED_INPUT 0x0001
#define ENABLE_LINE_INPUT 0x0002
#define ENABLE_ECHO_INPUT 0x0004
#define ENABLE_WINDOW_INPUT 0x0008
#define ENABLE_MOUSE_INPUT 0x0010
#define ENABLE_INSERT_MODE 0x0020
#define ENABLE_QUICK_EDIT_MODE 0x0040
#define ENABLE_EXTENDED_FLAGS 0x0080
#define ENABLE_AUTO_POSITION 0x0100
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#define ENABLE_PROCESSED_OUTPUT 0x0001
#define ENABLE_WRAP_AT_EOL_OUTPUT 0x0002
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#define ENABLE_LVB_GRID_WORLDWIDE 0x0010

#define FOREGROUND_BLUE 0x0001
#define FOREGROUND_GREEN 0x0002
#define FOREGROUND_RED 0x0004
#define FOREGROUND_INTENSITY 0x0008
#define BACKGROUND_BLUE 0x0010
#define BACKGROUND_GREEN 0x0020
#define BACKGROUND_RED 0x0040
#define BACKGROUND_INTENSITY 0x0080

#ifndef SHRT_MAX
#define SHRT_MAX 32767
#define SHRT_MIN (-32768)
#endif

#define CTRL_C_EVENT 0
#define CTRL_BREAK_EVENT 1
#define CTRL_CLOSE_EVENT 2
#define CTRL_LOGOFF_EVENT 5
#define CTRL_SHUTDOWN_EVENT 6

typedef struct _COORD {
    SHORT X;
    SHORT Y;
} COORD, *PCOORD;

typedef struct _SMALL_RECT {
    SHORT Left;
    SHORT Top;
    SHORT Right;
    SHORT Bottom;
} SMALL_RECT, *PSMALL_RECT;

typedef struct _CONSOLE_SCREEN_BUFFER_INFO {
    COORD dwSize;
    COORD dwCursorPosition;
    WORD wAttributes;
    SMALL_RECT srWindow;
    COORD dwMaximumWindowSize;
} CONSOLE_SCREEN_BUFFER_INFO, *PCONSOLE_SCREEN_BUFFER_INFO;

typedef struct _CONSOLE_SCREEN_BUFFER_INFOEX {
    ULONG cbSize;
    COORD dwSize;
    COORD dwCursorPosition;
    WORD wAttributes;
    SMALL_RECT srWindow;
    COORD dwMaximumWindowSize;
    WORD wPopupAttributes;
    BOOL bFullscreenSupported;
    COLORREF ColorTable[16];
} CONSOLE_SCREEN_BUFFER_INFOEX, *PCONSOLE_SCREEN_BUFFER_INFOEX;

typedef struct _CONSOLE_CURSOR_INFO {
    DWORD dwSize;
    BOOL bVisible;
} CONSOLE_CURSOR_INFO, *PCONSOLE_CURSOR_INFO;

typedef struct _CONSOLE_FONT_INFO {
    DWORD nFont;
    COORD dwFontSize;
} CONSOLE_FONT_INFO, *PCONSOLE_FONT_INFO;

typedef struct _CONSOLE_FONT_INFOEX {
    ULONG cbSize;
    DWORD nFont;
    COORD dwFontSize;
    UINT FontFamily;
    UINT FontWeight;
    WCHAR FaceName[32];
} CONSOLE_FONT_INFOEX, *PCONSOLE_FONT_INFOEX;

typedef struct _CHAR_INFO {
    union {
        WCHAR UnicodeChar;
        CHAR AsciiChar;
    } Char;
    WORD Attributes;
} CHAR_INFO, *PCHAR_INFO;

#define KEY_EVENT 0x0001
#define RIGHT_ALT_PRESSED 0x0001
#define LEFT_ALT_PRESSED 0x0002
#define RIGHT_CTRL_PRESSED 0x0004
#define LEFT_CTRL_PRESSED 0x0008
#define SHIFT_PRESSED 0x0010
#define ENHANCED_KEY 0x0100

typedef struct _KEY_EVENT_RECORD {
    BOOL bKeyDown;
    WORD wRepeatCount;
    WORD wVirtualKeyCode;
    WORD wVirtualScanCode;
    union {
        WCHAR UnicodeChar;
        CHAR AsciiChar;
    } uChar;
    DWORD dwControlKeyState;
} KEY_EVENT_RECORD;

#define MOUSE_EVENT 0x0002
#define WINDOW_BUFFER_SIZE_EVENT 0x0004
#define MENU_EVENT 0x0008
#define FOCUS_EVENT 0x0010

#define FROM_LEFT_1ST_BUTTON_PRESSED 0x0001
#define RIGHTMOST_BUTTON_PRESSED 0x0002
#define FROM_LEFT_2ND_BUTTON_PRESSED 0x0004
#define MOUSE_MOVED 0x0001
#define DOUBLE_CLICK 0x0002
#define MOUSE_WHEELED 0x0004
#define MOUSE_HWHEELED 0x0008

typedef struct _MOUSE_EVENT_RECORD {
    COORD dwMousePosition;
    DWORD dwButtonState;
    DWORD dwControlKeyState;
    DWORD dwEventFlags;
} MOUSE_EVENT_RECORD;

typedef struct _WINDOW_BUFFER_SIZE_RECORD {
    COORD dwSize;
} WINDOW_BUFFER_SIZE_RECORD;

typedef struct _MENU_EVENT_RECORD {
    UINT dwCommandId;
} MENU_EVENT_RECORD;

typedef struct _FOCUS_EVENT_RECORD {
    BOOL bSetFocus;
} FOCUS_EVENT_RECORD;

typedef struct _INPUT_RECORD {
    WORD EventType;
    union {
        KEY_EVENT_RECORD KeyEvent;
        MOUSE_EVENT_RECORD MouseEvent;
        WINDOW_BUFFER_SIZE_RECORD WindowBufferSizeEvent;
        MENU_EVENT_RECORD MenuEvent;
        FOCUS_EVENT_RECORD FocusEvent;
        BYTE Raw[16];
    } Event;
} INPUT_RECORD, *PINPUT_RECORD;

#define CONSOLE_READ_NOREMOVE 0x0001
#define CONSOLE_READ_NOWAIT 0x0002

#define CONSOLE_TEXTMODE_BUFFER 1
#define CONSOLE_GRAPHICS_BUFFER 2
#define COMMON_LVB_LEADING_BYTE 0x0100
#define COMMON_LVB_TRAILING_BYTE 0x0200
#define COMMON_LVB_GRID_HORIZONTAL 0x0400
#define COMMON_LVB_GRID_LVERTICAL 0x0800
#define COMMON_LVB_GRID_RVERTICAL 0x1000
#define COMMON_LVB_REVERSE_VIDEO 0x4000
#define COMMON_LVB_UNDERSCORE 0x8000
#define CONSOLE_FULLSCREEN_MODE 1
#define CONSOLE_WINDOWED_MODE 2
#define HISTORY_NO_DUP_FLAG 1

typedef struct _CONSOLE_SELECTION_INFO {
    DWORD dwFlags;
    COORD dwSelectionAnchor;
    SMALL_RECT srSelection;
} CONSOLE_SELECTION_INFO, *PCONSOLE_SELECTION_INFO;

typedef struct _CONSOLE_HISTORY_INFO {
    UINT cbSize;
    UINT HistoryBufferSize;
    UINT NumberOfHistoryBuffers;
    DWORD dwFlags;
} CONSOLE_HISTORY_INFO, *PCONSOLE_HISTORY_INFO;

typedef struct _CONSOLE_READCONSOLE_CONTROL {
    ULONG nLength;
    ULONG nInitialChars;
    ULONG dwCtrlWakeupMask;
    ULONG dwControlKeyState;
} CONSOLE_READCONSOLE_CONTROL, *PCONSOLE_READCONSOLE_CONTROL;

typedef BOOL(WINAPI *PHANDLER_ROUTINE)(DWORD type);

WINBASEAPI HANDLE WINAPI GetStdHandle(DWORD which);
WINBASEAPI BOOL WINAPI SetStdHandle(DWORD which, HANDLE handle);
WINBASEAPI BOOL WINAPI SetStdHandleEx(DWORD which, HANDLE handle, PHANDLE previous);
WINBASEAPI BOOL WINAPI AllocConsole(void);
WINBASEAPI BOOL WINAPI FreeConsole(void);
WINBASEAPI BOOL WINAPI AttachConsole(DWORD process);
WINBASEAPI HWND WINAPI GetConsoleWindow(void);
WINBASEAPI UINT WINAPI GetConsoleCP(void);
WINBASEAPI UINT WINAPI GetConsoleOutputCP(void);
WINBASEAPI BOOL WINAPI SetConsoleCP(UINT cp);
WINBASEAPI BOOL WINAPI SetConsoleOutputCP(UINT cp);
WINBASEAPI BOOL WINAPI GetConsoleMode(HANDLE console, LPDWORD mode);
WINBASEAPI BOOL WINAPI SetConsoleMode(HANDLE console, DWORD mode);
WINBASEAPI BOOL WINAPI WriteConsoleW(HANDLE console, const void *buffer, DWORD count, LPDWORD written,
                                     LPVOID reserved);
WINBASEAPI BOOL WINAPI WriteConsoleA(HANDLE console, const void *buffer, DWORD count, LPDWORD written,
                                     LPVOID reserved);
WINBASEAPI BOOL WINAPI ReadConsoleW(HANDLE console, LPVOID buffer, DWORD count, LPDWORD read,
                                    PCONSOLE_READCONSOLE_CONTROL control);
WINBASEAPI BOOL WINAPI ReadConsoleA(HANDLE console, LPVOID buffer, DWORD count, LPDWORD read,
                                    PCONSOLE_READCONSOLE_CONTROL control);
WINBASEAPI BOOL WINAPI ReadConsoleInputW(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read);
WINBASEAPI BOOL WINAPI WriteConsoleInputW(HANDLE console, const INPUT_RECORD *buffer, DWORD count, LPDWORD written);
WINBASEAPI BOOL WINAPI WriteConsoleInputA(HANDLE console, const INPUT_RECORD *buffer, DWORD count, LPDWORD written);
WINBASEAPI BOOL WINAPI WriteConsoleInputVDMW(HANDLE console, const INPUT_RECORD *buffer, DWORD count, LPDWORD written);
WINBASEAPI BOOL WINAPI WriteConsoleInputVDMA(HANDLE console, const INPUT_RECORD *buffer, DWORD count, LPDWORD written);
WINBASEAPI BOOL WINAPI ReadConsoleInputExW(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read, USHORT flags);
WINBASEAPI BOOL WINAPI ReadConsoleInputExA(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read, USHORT flags);
WINBASEAPI BOOL WINAPI ReadConsoleInputA(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read);
WINBASEAPI BOOL WINAPI PeekConsoleInputW(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read);
WINBASEAPI BOOL WINAPI PeekConsoleInputA(HANDLE console, PINPUT_RECORD buffer, DWORD count, LPDWORD read);
WINBASEAPI BOOL WINAPI GetNumberOfConsoleInputEvents(HANDLE console, LPDWORD count);
WINBASEAPI BOOL WINAPI FlushConsoleInputBuffer(HANDLE console);
WINBASEAPI BOOL WINAPI SetConsoleTitleW(LPCWSTR title);
WINBASEAPI BOOL WINAPI SetConsoleTitleA(LPCSTR title);
WINBASEAPI DWORD WINAPI GetConsoleTitleW(LPWSTR title, DWORD size);
WINBASEAPI DWORD WINAPI GetConsoleTitleA(LPSTR title, DWORD size);
WINBASEAPI BOOL WINAPI GetConsoleScreenBufferInfo(HANDLE console, PCONSOLE_SCREEN_BUFFER_INFO info);
WINBASEAPI BOOL WINAPI GetConsoleScreenBufferInfoEx(HANDLE console, PCONSOLE_SCREEN_BUFFER_INFOEX info);
WINBASEAPI BOOL WINAPI SetConsoleScreenBufferSize(HANDLE console, COORD size);
WINBASEAPI BOOL WINAPI SetConsoleTextAttribute(HANDLE console, WORD attributes);
WINBASEAPI BOOL WINAPI SetConsoleCursorPosition(HANDLE console, COORD position);
WINBASEAPI BOOL WINAPI GetConsoleCursorInfo(HANDLE console, PCONSOLE_CURSOR_INFO info);
WINBASEAPI BOOL WINAPI SetConsoleCursorInfo(HANDLE console, const CONSOLE_CURSOR_INFO *info);
WINBASEAPI BOOL WINAPI SetConsoleWindowInfo(HANDLE console, BOOL absolute, const SMALL_RECT *window);
WINBASEAPI COORD WINAPI GetLargestConsoleWindowSize(HANDLE console);
WINBASEAPI BOOL WINAPI FillConsoleOutputCharacterW(HANDLE console, WCHAR c, DWORD length, COORD start,
                                                   LPDWORD written);
WINBASEAPI BOOL WINAPI FillConsoleOutputCharacterA(HANDLE console, CHAR c, DWORD length, COORD start,
                                                   LPDWORD written);
WINBASEAPI BOOL WINAPI FillConsoleOutputAttribute(HANDLE console, WORD attribute, DWORD length, COORD start,
                                                  LPDWORD written);
WINBASEAPI BOOL WINAPI WriteConsoleOutputCharacterW(HANDLE console, LPCWSTR text, DWORD length, COORD start,
                                                    LPDWORD written);
WINBASEAPI BOOL WINAPI SetConsoleCtrlHandler(PHANDLER_ROUTINE handler, BOOL add);
WINBASEAPI BOOL WINAPI GenerateConsoleCtrlEvent(DWORD event, DWORD group);
WINBASEAPI BOOL WINAPI GetCurrentConsoleFont(HANDLE console, BOOL maximum, PCONSOLE_FONT_INFO info);
WINBASEAPI BOOL WINAPI GetCurrentConsoleFontEx(HANDLE console, BOOL maximum, PCONSOLE_FONT_INFOEX info);
WINBASEAPI DWORD WINAPI GetConsoleProcessList(LPDWORD list, DWORD count);
WINBASEAPI HANDLE WINAPI CreateConsoleScreenBuffer(DWORD access, DWORD share, const SECURITY_ATTRIBUTES *sa,
                                                   DWORD flags, LPVOID data);
WINBASEAPI BOOL WINAPI SetConsoleActiveScreenBuffer(HANDLE console);
WINBASEAPI BOOL WINAPI ReadConsoleOutputW(HANDLE console, PCHAR_INFO buffer, COORD size, COORD origin, PSMALL_RECT region);
WINBASEAPI BOOL WINAPI ReadConsoleOutputA(HANDLE console, PCHAR_INFO buffer, COORD size, COORD origin, PSMALL_RECT region);
WINBASEAPI BOOL WINAPI WriteConsoleOutputW(HANDLE console, const CHAR_INFO *buffer, COORD size, COORD origin,
                                           PSMALL_RECT region);
WINBASEAPI BOOL WINAPI WriteConsoleOutputA(HANDLE console, const CHAR_INFO *buffer, COORD size, COORD origin,
                                           PSMALL_RECT region);
WINBASEAPI BOOL WINAPI ReadConsoleOutputAttribute(HANDLE console, LPWORD attributes, DWORD length, COORD start,
                                                  LPDWORD read);
WINBASEAPI BOOL WINAPI ReadConsoleOutputCharacterW(HANDLE console, LPWSTR chars, DWORD length, COORD start, LPDWORD read);
WINBASEAPI BOOL WINAPI ReadConsoleOutputCharacterA(HANDLE console, LPSTR chars, DWORD length, COORD start, LPDWORD read);
WINBASEAPI BOOL WINAPI WriteConsoleOutputCharacterA(HANDLE console, LPCSTR text, DWORD length, COORD start,
                                                    LPDWORD written);
WINBASEAPI BOOL WINAPI WriteConsoleOutputAttribute(HANDLE console, const WORD *attributes, DWORD length, COORD start,
                                                   LPDWORD written);
WINBASEAPI BOOL WINAPI ScrollConsoleScreenBufferW(HANDLE console, const SMALL_RECT *source, const SMALL_RECT *clip,
                                                  COORD destination, const CHAR_INFO *fill);
WINBASEAPI BOOL WINAPI ScrollConsoleScreenBufferA(HANDLE console, const SMALL_RECT *source, const SMALL_RECT *clip,
                                                  COORD destination, const CHAR_INFO *fill);
WINBASEAPI BOOL WINAPI SetConsoleScreenBufferInfoEx(HANDLE console, PCONSOLE_SCREEN_BUFFER_INFOEX info);
WINBASEAPI HRESULT WINAPI CreatePseudoConsole(COORD size, HANDLE input, HANDLE output, DWORD flags, HPCON *console);
WINBASEAPI HRESULT WINAPI ResizePseudoConsole(HPCON console, COORD size);
WINBASEAPI VOID WINAPI ClosePseudoConsole(HPCON console);
WINBASEAPI HRESULT WINAPI ReleasePseudoConsole(HPCON console);
WINBASEAPI BOOL WINAPI SetCurrentConsoleFontEx(HANDLE console, BOOL maximum, PCONSOLE_FONT_INFOEX info);
WINBASEAPI COORD WINAPI GetConsoleFontSize(HANDLE console, DWORD font);
WINBASEAPI BOOL WINAPI GetConsoleSelectionInfo(PCONSOLE_SELECTION_INFO info);
WINBASEAPI BOOL WINAPI GetConsoleDisplayMode(LPDWORD flags);
WINBASEAPI BOOL WINAPI SetConsoleDisplayMode(HANDLE console, DWORD flags, PCOORD dimensions);
WINBASEAPI BOOL WINAPI GetNumberOfConsoleMouseButtons(LPDWORD buttons);
WINBASEAPI DWORD WINAPI GetConsoleOriginalTitleW(LPWSTR title, DWORD size);
WINBASEAPI DWORD WINAPI GetConsoleOriginalTitleA(LPSTR title, DWORD size);
WINBASEAPI BOOL WINAPI GetConsoleHistoryInfo(PCONSOLE_HISTORY_INFO info);
WINBASEAPI BOOL WINAPI SetConsoleHistoryInfo(PCONSOLE_HISTORY_INFO info);
WINBASEAPI DWORD WINAPI GetConsoleCommandHistoryW(LPWSTR commands, DWORD length, LPCWSTR exe);
WINBASEAPI DWORD WINAPI GetConsoleCommandHistoryA(LPSTR commands, DWORD length, LPCSTR exe);
WINBASEAPI DWORD WINAPI GetConsoleCommandHistoryLengthW(LPCWSTR exe);
WINBASEAPI DWORD WINAPI GetConsoleCommandHistoryLengthA(LPCSTR exe);
WINBASEAPI VOID WINAPI ExpungeConsoleCommandHistoryW(LPCWSTR exe);
WINBASEAPI VOID WINAPI ExpungeConsoleCommandHistoryA(LPCSTR exe);
WINBASEAPI BOOL WINAPI SetConsoleNumberOfCommandsW(DWORD number, LPCWSTR exe);
WINBASEAPI BOOL WINAPI SetConsoleNumberOfCommandsA(DWORD number, LPCSTR exe);
WINBASEAPI BOOL WINAPI AddConsoleAliasW(LPWSTR source, LPWSTR target, LPWSTR exe);
WINBASEAPI BOOL WINAPI AddConsoleAliasA(LPSTR source, LPSTR target, LPSTR exe);
WINBASEAPI DWORD WINAPI GetConsoleAliasW(LPWSTR source, LPWSTR buffer, DWORD length, LPWSTR exe);
WINBASEAPI DWORD WINAPI GetConsoleAliasA(LPSTR source, LPSTR buffer, DWORD length, LPSTR exe);
WINBASEAPI DWORD WINAPI GetConsoleAliasesW(LPWSTR buffer, DWORD length, LPWSTR exe);
WINBASEAPI DWORD WINAPI GetConsoleAliasesA(LPSTR buffer, DWORD length, LPSTR exe);
WINBASEAPI DWORD WINAPI GetConsoleAliasesLengthW(LPWSTR exe);
WINBASEAPI DWORD WINAPI GetConsoleAliasesLengthA(LPSTR exe);
WINBASEAPI DWORD WINAPI GetConsoleAliasExesW(LPWSTR buffer, DWORD length);
WINBASEAPI DWORD WINAPI GetConsoleAliasExesA(LPSTR buffer, DWORD length);
WINBASEAPI DWORD WINAPI GetConsoleAliasExesLengthW(void);
WINBASEAPI DWORD WINAPI GetConsoleAliasExesLengthA(void);
WINBASEAPI BOOL WINAPI SetConsoleInputExeNameW(LPCWSTR name);
WINBASEAPI BOOL WINAPI SetConsoleInputExeNameA(LPCSTR name);
WINBASEAPI DWORD WINAPI GetConsoleInputExeNameW(DWORD size, LPWSTR buffer);
WINBASEAPI DWORD WINAPI GetConsoleInputExeNameA(DWORD size, LPSTR buffer);


/* ------------------------------------------------------------------ processes and threads */

#define CREATE_SUSPENDED 0x00000004
#define CREATE_BREAKAWAY_FROM_JOB 0x01000000
#define DEBUG_PROCESS 0x00000001
#define DEBUG_ONLY_THIS_PROCESS 0x00000002
#define DETACHED_PROCESS 0x00000008
#define CREATE_NEW_CONSOLE 0x00000010
#define NORMAL_PRIORITY_CLASS 0x00000020
#define IDLE_PRIORITY_CLASS 0x00000040
#define HIGH_PRIORITY_CLASS 0x00000080
#define REALTIME_PRIORITY_CLASS 0x00000100
#define BELOW_NORMAL_PRIORITY_CLASS 0x00004000
#define ABOVE_NORMAL_PRIORITY_CLASS 0x00008000
#define CREATE_NEW_PROCESS_GROUP 0x00000200
#define CREATE_UNICODE_ENVIRONMENT 0x00000400
#define CREATE_NO_WINDOW 0x08000000
#define EXTENDED_STARTUPINFO_PRESENT 0x00080000
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x00010000
#define PROCESS_MODE_BACKGROUND_BEGIN 0x00100000
#define PROCESS_MODE_BACKGROUND_END 0x00200000

#define THREAD_PRIORITY_LOWEST (-2)
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_ERROR_RETURN 0x7FFFFFFF
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define THREAD_PRIORITY_IDLE (-15)
#define THREAD_MODE_BACKGROUND_BEGIN 0x00010000
#define THREAD_MODE_BACKGROUND_END 0x00020000

#define STILL_ACTIVE 259
#define STARTF_USESHOWWINDOW 0x00000001
#define STARTF_USESIZE 0x00000002
#define STARTF_USEPOSITION 0x00000004
#define STARTF_USECOUNTCHARS 0x00000008
#define STARTF_USEFILLATTRIBUTE 0x00000010
#define STARTF_RUNFULLSCREEN 0x00000020
#define STARTF_FORCEONFEEDBACK 0x00000040
#define STARTF_FORCEOFFFEEDBACK 0x00000080
#define STARTF_USESTDHANDLES 0x00000100
#define STARTF_USEHOTKEY 0x00000200
#define STARTF_TITLEISLINKNAME 0x00000800
#define STARTF_TITLEISAPPID 0x00001000
#define STARTF_PREVENTPINNING 0x00002000

#define PROCESSOR_ARCHITECTURE_INTEL 0
#define PROCESSOR_ARCHITECTURE_ARM 5
#define PROCESSOR_ARCHITECTURE_IA64 6
#define PROCESSOR_ARCHITECTURE_UNKNOWN 0xFFFF
#define PROCESSOR_INTEL_386 386
#define PROCESSOR_INTEL_486 486
#define PROCESSOR_INTEL_PENTIUM 586
#define PROCESSOR_AMD_X8664 8664
#define PROCESSOR_ARM 7
#define IMAGE_FILE_MACHINE_UNKNOWN 0
#define IMAGE_FILE_MACHINE_ARMNT 0x01c4

typedef DWORD(WINAPI *PTHREAD_START_ROUTINE)(LPVOID parameter);
typedef PTHREAD_START_ROUTINE LPTHREAD_START_ROUTINE;
WINBASEAPI BOOL WINAPI QueueUserWorkItem(LPTHREAD_START_ROUTINE function, PVOID context, ULONG flags);
typedef void(NTAPI *PAPCFUNC)(ULONG_PTR parameter);
typedef void(WINAPI *PFLS_CALLBACK_FUNCTION)(PVOID data);
typedef void(WINAPI *LPFIBER_START_ROUTINE)(LPVOID parameter);
typedef LPFIBER_START_ROUTINE PFIBER_START_ROUTINE;

typedef struct _STARTUPINFOW {
    DWORD cb;
    LPWSTR lpReserved;
    LPWSTR lpDesktop;
    LPWSTR lpTitle;
    DWORD dwX;
    DWORD dwY;
    DWORD dwXSize;
    DWORD dwYSize;
    DWORD dwXCountChars;
    DWORD dwYCountChars;
    DWORD dwFillAttribute;
    DWORD dwFlags;
    WORD wShowWindow;
    WORD cbReserved2;
    LPBYTE lpReserved2;
    HANDLE hStdInput;
    HANDLE hStdOutput;
    HANDLE hStdError;
} STARTUPINFOW, *LPSTARTUPINFOW;

typedef struct _STARTUPINFOA {
    DWORD cb;
    LPSTR lpReserved;
    LPSTR lpDesktop;
    LPSTR lpTitle;
    DWORD dwX;
    DWORD dwY;
    DWORD dwXSize;
    DWORD dwYSize;
    DWORD dwXCountChars;
    DWORD dwYCountChars;
    DWORD dwFillAttribute;
    DWORD dwFlags;
    WORD wShowWindow;
    WORD cbReserved2;
    LPBYTE lpReserved2;
    HANDLE hStdInput;
    HANDLE hStdOutput;
    HANDLE hStdError;
} STARTUPINFOA, *LPSTARTUPINFOA;

typedef struct _PROC_THREAD_ATTRIBUTE_LIST *PPROC_THREAD_ATTRIBUTE_LIST, *LPPROC_THREAD_ATTRIBUTE_LIST;

typedef struct _STARTUPINFOEXW {
    STARTUPINFOW StartupInfo;
    LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList;
} STARTUPINFOEXW, *LPSTARTUPINFOEXW;

typedef struct _STARTUPINFOEXA {
    STARTUPINFOA StartupInfo;
    LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList;
} STARTUPINFOEXA, *LPSTARTUPINFOEXA;

typedef struct _PROCESS_INFORMATION {
    HANDLE hProcess;
    HANDLE hThread;
    DWORD dwProcessId;
    DWORD dwThreadId;
} PROCESS_INFORMATION, *PPROCESS_INFORMATION, *LPPROCESS_INFORMATION;

typedef struct _PROCESSOR_NUMBER {
    WORD Group;
    BYTE Number;
    BYTE Reserved;
} PROCESSOR_NUMBER, *PPROCESSOR_NUMBER;

typedef struct _GROUP_AFFINITY {
    KAFFINITY Mask;
    WORD Group;
    WORD Reserved[3];
} GROUP_AFFINITY, *PGROUP_AFFINITY;

typedef enum _PROCESS_MITIGATION_POLICY {
    ProcessDEPPolicy,
    ProcessASLRPolicy,
    ProcessDynamicCodePolicy,
    ProcessStrictHandleCheckPolicy,
    ProcessSystemCallDisablePolicy,
    ProcessMitigationOptionsMask,
    ProcessExtensionPointDisablePolicy,
    ProcessControlFlowGuardPolicy,
    ProcessSignaturePolicy,
    ProcessFontDisablePolicy,
    ProcessImageLoadPolicy,
    ProcessSystemCallFilterPolicy,
    ProcessPayloadRestrictionPolicy,
    ProcessChildProcessPolicy,
    ProcessSideChannelIsolationPolicy,
    ProcessUserShadowStackPolicy,
    ProcessRedirectionTrustPolicy,
    ProcessUserPointerAuthPolicy,
    ProcessSEHOPPolicy,
    MaxProcessMitigationPolicy
} PROCESS_MITIGATION_POLICY;

typedef enum _THREAD_INFORMATION_CLASS {
    ThreadMemoryPriority,
    ThreadAbsoluteCpuPriority,
    ThreadDynamicCodePolicy,
    ThreadPowerThrottling,
    ThreadInformationClassMax
} THREAD_INFORMATION_CLASS;

typedef enum _PROCESS_INFORMATION_CLASS {
    ProcessMemoryPriority,
    ProcessMemoryExhaustionInfo,
    ProcessAppMemoryInfo,
    ProcessInPrivateInfo,
    ProcessPowerThrottling,
    ProcessReservedValue1,
    ProcessTelemetryCoverageInfo,
    ProcessProtectionLevelInfo,
    ProcessLeapSecondInfo,
    ProcessMachineTypeInfo,
    ProcessInformationClassMax
} PROCESS_INFORMATION_CLASS;

#define PROC_THREAD_ATTRIBUTE_PARENT_PROCESS 0x00020000
#define PROC_THREAD_ATTRIBUTE_HANDLE_LIST 0x00020002
#define PROC_THREAD_ATTRIBUTE_GROUP_AFFINITY 0x00030003
#define PROC_THREAD_ATTRIBUTE_PREFERRED_NODE 0x00020004
#define PROC_THREAD_ATTRIBUTE_IDEAL_PROCESSOR 0x00030005
#define PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY 0x00020007
#define PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES 0x00020009
#define PROC_THREAD_ATTRIBUTE_PROTECTION_LEVEL 0x0002000B
#define PROC_THREAD_ATTRIBUTE_JOB_LIST 0x0002000D
#define PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY 0x0002000E
#define PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY 0x0002000F
#define PROC_THREAD_ATTRIBUTE_WIN32K_FILTER 0x00020010
#define PROC_THREAD_ATTRIBUTE_DESKTOP_APP_POLICY 0x00020012
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#define PROC_THREAD_ATTRIBUTE_MACHINE_TYPE 0x00020019
#define PROC_THREAD_ATTRIBUTE_COMPONENT_FILTER 0x0002001A
#define PROC_THREAD_ATTRIBUTE_ENABLE_OPTIONAL_XSTATE_FEATURES 0x0003001B
#define PROC_THREAD_ATTRIBUTE_EXTENDED_FLAGS 0x00060001
#define PROC_THREAD_ATTRIBUTE_REPLACE_VALUE 0x00000001

#define PROCESS_DEP_ENABLE 0x00000001
#define PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION 0x00000002

typedef enum _DEP_SYSTEM_POLICY_TYPE {
    DEPPolicyAlwaysOff = 0,
    DEPPolicyAlwaysOn,
    DEPPolicyOptIn,
    DEPPolicyOptOut,
    DEPTotalPolicyCount
} DEP_SYSTEM_POLICY_TYPE;

#define BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE 0x00000001
#define BASE_SEARCH_PATH_DISABLE_SAFE_SEARCHMODE 0x00010000
#define BASE_SEARCH_PATH_PERMANENT 0x00008000

WINBASEAPI HANDLE WINAPI GetCurrentProcess(void);
WINBASEAPI HANDLE WINAPI GetCurrentThread(void);
WINBASEAPI DWORD WINAPI GetCurrentProcessId(void);
WINBASEAPI DWORD WINAPI GetCurrentThreadId(void);
WINBASEAPI DECLSPEC_NORETURN void WINAPI ExitProcess(UINT code);
WINBASEAPI BOOL WINAPI TerminateProcess(HANDLE process, UINT code);
WINBASEAPI DECLSPEC_NORETURN void WINAPI ExitThread(DWORD code);
WINBASEAPI BOOL WINAPI TerminateThread(HANDLE thread, DWORD code);
WINBASEAPI HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T stack, LPTHREAD_START_ROUTINE start,
                                      LPVOID parameter, DWORD flags, LPDWORD id);
WINBASEAPI HANDLE WINAPI CreateRemoteThread(HANDLE process, LPSECURITY_ATTRIBUTES sa, SIZE_T stack,
                                            LPTHREAD_START_ROUTINE start, LPVOID parameter, DWORD flags,
                                            LPDWORD id);
WINBASEAPI HANDLE WINAPI CreateRemoteThreadEx(HANDLE process, LPSECURITY_ATTRIBUTES sa, SIZE_T stack,
                                              LPTHREAD_START_ROUTINE start, LPVOID parameter, DWORD flags,
                                              LPPROC_THREAD_ATTRIBUTE_LIST attributes, LPDWORD id);
WINBASEAPI BOOL WINAPI GetExitCodeProcess(HANDLE process, LPDWORD code);
WINBASEAPI BOOL WINAPI GetExitCodeThread(HANDLE thread, LPDWORD code);
WINBASEAPI DWORD WINAPI GetThreadId(HANDLE thread);
WINBASEAPI DWORD WINAPI GetProcessId(HANDLE process);
WINBASEAPI DWORD WINAPI GetProcessIdOfThread(HANDLE thread);
WINBASEAPI HANDLE WINAPI OpenProcess(DWORD access, BOOL inherit, DWORD id);
WINBASEAPI HANDLE WINAPI OpenThread(DWORD access, BOOL inherit, DWORD id);
WINBASEAPI DWORD WINAPI SuspendThread(HANDLE thread);
WINBASEAPI BOOL WINAPI GetThreadContext(HANDLE thread, PCONTEXT context);
WINBASEAPI BOOL WINAPI SetThreadContext(HANDLE thread, const CONTEXT *context);
WINBASEAPI DWORD WINAPI ResumeThread(HANDLE thread);
WINBASEAPI void WINAPI Sleep(DWORD milliseconds);
WINBASEAPI DWORD WINAPI SleepEx(DWORD milliseconds, BOOL alertable);
WINBASEAPI BOOL WINAPI SwitchToThread(void);
WINBASEAPI int WINAPI GetThreadPriority(HANDLE thread);
WINBASEAPI BOOL WINAPI SetThreadPriority(HANDLE thread, int priority);
WINBASEAPI BOOL WINAPI GetThreadPriorityBoost(HANDLE thread, PBOOL disabled);
WINBASEAPI BOOL WINAPI SetThreadPriorityBoost(HANDLE thread, BOOL disable);
WINBASEAPI DWORD WINAPI GetPriorityClass(HANDLE process);
WINBASEAPI BOOL WINAPI SetPriorityClass(HANDLE process, DWORD klass);
WINBASEAPI BOOL WINAPI GetProcessPriorityBoost(HANDLE process, PBOOL disabled);
WINBASEAPI BOOL WINAPI SetProcessPriorityBoost(HANDLE process, BOOL disable);
WINBASEAPI BOOL WINAPI GetProcessAffinityMask(HANDLE process, PDWORD_PTR process_mask, PDWORD_PTR system_mask);
WINBASEAPI BOOL WINAPI SetProcessAffinityMask(HANDLE process, DWORD_PTR mask);
WINBASEAPI DWORD_PTR WINAPI SetThreadAffinityMask(HANDLE thread, DWORD_PTR mask);
WINBASEAPI DWORD WINAPI SetThreadIdealProcessor(HANDLE thread, DWORD processor);
WINBASEAPI BOOL WINAPI SetThreadIdealProcessorEx(HANDLE thread, PPROCESSOR_NUMBER ideal, PPROCESSOR_NUMBER previous);
WINBASEAPI BOOL WINAPI GetThreadIdealProcessorEx(HANDLE thread, PPROCESSOR_NUMBER ideal);
WINBASEAPI BOOL WINAPI GetThreadGroupAffinity(HANDLE thread, PGROUP_AFFINITY affinity);
WINBASEAPI BOOL WINAPI SetThreadGroupAffinity(HANDLE thread, const GROUP_AFFINITY *affinity,
                                              PGROUP_AFFINITY previous);
WINBASEAPI void WINAPI GetStartupInfoW(LPSTARTUPINFOW info);
WINBASEAPI void WINAPI GetStartupInfoA(LPSTARTUPINFOA info);
WINBASEAPI LPWSTR WINAPI GetCommandLineW(void);
WINBASEAPI LPSTR WINAPI GetCommandLineA(void);
WINBASEAPI BOOL WINAPI CreateProcessW(LPCWSTR application, LPWSTR command_line, LPSECURITY_ATTRIBUTES process_sa,
                                      LPSECURITY_ATTRIBUTES thread_sa, BOOL inherit, DWORD flags, LPVOID environment,
                                      LPCWSTR directory, LPSTARTUPINFOW startup, LPPROCESS_INFORMATION info);
WINBASEAPI BOOL WINAPI CreateProcessA(LPCSTR application, LPSTR command_line, LPSECURITY_ATTRIBUTES process_sa,
                                      LPSECURITY_ATTRIBUTES thread_sa, BOOL inherit, DWORD flags, LPVOID environment,
                                      LPCSTR directory, LPSTARTUPINFOA startup, LPPROCESS_INFORMATION info);
WINBASEAPI BOOL WINAPI CreateProcessAsUserW(HANDLE token, LPCWSTR application, LPWSTR command_line,
                                            LPSECURITY_ATTRIBUTES process_sa, LPSECURITY_ATTRIBUTES thread_sa,
                                            BOOL inherit, DWORD flags, LPVOID environment, LPCWSTR directory,
                                            LPSTARTUPINFOW startup, LPPROCESS_INFORMATION info);
WINBASEAPI BOOL WINAPI CreateProcessAsUserA(HANDLE token, LPCSTR application, LPSTR command_line,
                                            LPSECURITY_ATTRIBUTES process_sa, LPSECURITY_ATTRIBUTES thread_sa,
                                            BOOL inherit, DWORD flags, LPVOID environment, LPCSTR directory,
                                            LPSTARTUPINFOA startup, LPPROCESS_INFORMATION info);
WINBASEAPI BOOL WINAPI CreateProcessInternalW(HANDLE token, LPCWSTR application, LPWSTR command_line,
                                              LPSECURITY_ATTRIBUTES process_sa, LPSECURITY_ATTRIBUTES thread_sa,
                                              BOOL inherit, DWORD flags, LPVOID environment, LPCWSTR directory,
                                              LPSTARTUPINFOW startup, LPPROCESS_INFORMATION info, PHANDLE new_token);
WINBASEAPI BOOL WINAPI CreateProcessInternalA(HANDLE token, LPCSTR application, LPSTR command_line,
                                              LPSECURITY_ATTRIBUTES process_sa, LPSECURITY_ATTRIBUTES thread_sa,
                                              BOOL inherit, DWORD flags, LPVOID environment, LPCSTR directory,
                                              LPSTARTUPINFOA startup, LPPROCESS_INFORMATION info, PHANDLE new_token);
WINBASEAPI UINT WINAPI WinExec(LPCSTR command_line, UINT show);
WINBASEAPI BOOL WINAPI GetProcessTimes(HANDLE process, LPFILETIME creation, LPFILETIME exit, LPFILETIME kernel,
                                       LPFILETIME user);
WINBASEAPI BOOL WINAPI GetThreadTimes(HANDLE thread, LPFILETIME creation, LPFILETIME exit, LPFILETIME kernel,
                                      LPFILETIME user);
WINBASEAPI BOOL WINAPI GetSystemTimes(LPFILETIME idle, LPFILETIME kernel, LPFILETIME user);
WINBASEAPI BOOL WINAPI QueryProcessCycleTime(HANDLE process, PULONG64 cycles);
WINBASEAPI BOOL WINAPI QueryThreadCycleTime(HANDLE thread, PULONG64 cycles);
WINBASEAPI DWORD WINAPI QueueUserAPC(PAPCFUNC apc, HANDLE thread, ULONG_PTR data);
WINBASEAPI BOOL WINAPI IsWow64Process(HANDLE process, PBOOL wow64);
WINBASEAPI BOOL WINAPI IsWow64Process2(HANDLE process, USHORT *process_machine, USHORT *native_machine);
typedef enum _MACHINE_ATTRIBUTES {
    UserEnabled = 0x1,
    KernelEnabled = 0x2,
    Wow64Container = 0x4,
} MACHINE_ATTRIBUTES;
WINBASEAPI HRESULT WINAPI GetMachineTypeAttributes(USHORT machine, MACHINE_ATTRIBUTES *attributes);
WINBASEAPI USHORT WINAPI Wow64SetThreadDefaultGuestMachine(USHORT machine);
WINBASEAPI DWORD WINAPI GetCurrentProcessorNumber(void);
WINBASEAPI void WINAPI GetCurrentProcessorNumberEx(PPROCESSOR_NUMBER number);
WINBASEAPI BOOL WINAPI GetProcessHandleCount(HANDLE process, PDWORD count);
WINBASEAPI DWORD WINAPI GetProcessVersion(DWORD id);
WINBASEAPI BOOL WINAPI GetProcessWorkingSetSize(HANDLE process, PSIZE_T minimum, PSIZE_T maximum);
WINBASEAPI BOOL WINAPI SetProcessWorkingSetSize(HANDLE process, SIZE_T minimum, SIZE_T maximum);
WINBASEAPI BOOL WINAPI SetProcessWorkingSetSizeEx(HANDLE process, SIZE_T minimum, SIZE_T maximum, DWORD flags);
WINBASEAPI BOOL WINAPI ProcessIdToSessionId(DWORD process, DWORD *session);
WINBASEAPI DWORD WINAPI WTSGetActiveConsoleSessionId(void);
WINBASEAPI BOOL WINAPI SetProcessShutdownParameters(DWORD level, DWORD flags);
WINBASEAPI BOOL WINAPI GetProcessShutdownParameters(LPDWORD level, LPDWORD flags);
WINBASEAPI BOOL WINAPI IsProcessInJob(HANDLE process, HANDLE job, PBOOL result);
WINBASEAPI BOOL WINAPI IsProcessCritical(HANDLE process, PBOOL critical);
WINBASEAPI BOOL WINAPI GetProcessInformation(HANDLE process, PROCESS_INFORMATION_CLASS klass, LPVOID info,
                                             DWORD size);
WINBASEAPI BOOL WINAPI SetProcessInformation(HANDLE process, PROCESS_INFORMATION_CLASS klass, LPVOID info,
                                             DWORD size);
WINBASEAPI BOOL WINAPI GetThreadInformation(HANDLE thread, THREAD_INFORMATION_CLASS klass, LPVOID info, DWORD size);
WINBASEAPI BOOL WINAPI SetThreadInformation(HANDLE thread, THREAD_INFORMATION_CLASS klass, LPVOID info, DWORD size);
WINBASEAPI HRESULT WINAPI SetThreadDescription(HANDLE thread, PCWSTR description);
WINBASEAPI HRESULT WINAPI GetThreadDescription(HANDLE thread, PWSTR *description);
WINBASEAPI BOOL WINAPI SetThreadStackGuarantee(PULONG size);
WINBASEAPI void WINAPI GetCurrentThreadStackLimits(PULONG_PTR low, PULONG_PTR high);
WINBASEAPI BOOL WINAPI IsThreadAFiber(void);
WINBASEAPI BOOL WINAPI GetThreadIOPendingFlag(HANDLE thread, PBOOL pending);
WINBASEAPI BOOL WINAPI InitializeProcThreadAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST list, DWORD count,
                                                         DWORD flags, PSIZE_T size);
WINBASEAPI BOOL WINAPI UpdateProcThreadAttribute(LPPROC_THREAD_ATTRIBUTE_LIST list, DWORD flags,
                                                 DWORD_PTR attribute, PVOID value, SIZE_T size, PVOID previous,
                                                 PSIZE_T returned);
WINBASEAPI void WINAPI DeleteProcThreadAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST list);
WINBASEAPI BOOL WINAPI SetProcessDEPPolicy(DWORD flags);
WINBASEAPI BOOL WINAPI GetProcessDEPPolicy(HANDLE process, LPDWORD flags, PBOOL permanent);
WINBASEAPI DEP_SYSTEM_POLICY_TYPE WINAPI GetSystemDEPPolicy(void);
WINBASEAPI BOOL WINAPI SetProcessMitigationPolicy(PROCESS_MITIGATION_POLICY policy, PVOID buffer, SIZE_T length);
WINBASEAPI BOOL WINAPI GetProcessMitigationPolicy(HANDLE process, PROCESS_MITIGATION_POLICY policy, PVOID buffer,
                                                  SIZE_T length);
WINBASEAPI BOOL WINAPI SetSearchPathMode(DWORD flags);
WINBASEAPI LPVOID WINAPI ConvertThreadToFiber(LPVOID parameter);
WINBASEAPI LPVOID WINAPI ConvertThreadToFiberEx(LPVOID parameter, DWORD flags);
WINBASEAPI BOOL WINAPI ConvertFiberToThread(void);
WINBASEAPI LPVOID WINAPI CreateFiber(SIZE_T stack, LPFIBER_START_ROUTINE start, LPVOID parameter);
WINBASEAPI LPVOID WINAPI CreateFiberEx(SIZE_T commit, SIZE_T reserve, DWORD flags, LPFIBER_START_ROUTINE start,
                                       LPVOID parameter);
WINBASEAPI void WINAPI DeleteFiber(LPVOID fiber);
WINBASEAPI void WINAPI SwitchToFiber(LPVOID fiber);

/* ------------------------------------------------------------------ thread local storage */

#define TLS_OUT_OF_INDEXES ((DWORD)0xFFFFFFFF)
#define FLS_OUT_OF_INDEXES ((DWORD)0xFFFFFFFF)

WINBASEAPI DWORD WINAPI TlsAlloc(void);
WINBASEAPI BOOL WINAPI TlsFree(DWORD index);
WINBASEAPI LPVOID WINAPI TlsGetValue(DWORD index);
WINBASEAPI LPVOID WINAPI TlsGetValue2(DWORD index);
WINBASEAPI BOOL WINAPI TlsSetValue(DWORD index, LPVOID value);
WINBASEAPI DWORD WINAPI FlsAlloc(PFLS_CALLBACK_FUNCTION callback);
WINBASEAPI BOOL WINAPI FlsFree(DWORD index);
WINBASEAPI PVOID WINAPI FlsGetValue(DWORD index);
WINBASEAPI PVOID WINAPI FlsGetValue2(DWORD index);
WINBASEAPI BOOL WINAPI FlsSetValue(DWORD index, PVOID value);

/* ------------------------------------------------------------------ synchronization */

#define INFINITE 0xFFFFFFFF
#define WAIT_FAILED ((DWORD)0xFFFFFFFF)
#define WAIT_OBJECT_0 0x00000000L
#define WAIT_ABANDONED 0x00000080L
#define WAIT_ABANDONED_0 0x00000080L
#define WAIT_IO_COMPLETION 0x000000C0L
#define MAXIMUM_WAIT_OBJECTS 64

#define CREATE_EVENT_MANUAL_RESET 0x00000001
#define CREATE_EVENT_INITIAL_SET 0x00000002
#define CREATE_MUTEX_INITIAL_OWNER 0x00000001
#define CREATE_WAITABLE_TIMER_MANUAL_RESET 0x00000001
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002

#define MUTEX_MODIFY_STATE MUTANT_QUERY_STATE
#define MUTEX_ALL_ACCESS MUTANT_ALL_ACCESS
#define TIMER_QUERY_STATE 0x0001
#define TIMER_MODIFY_STATE 0x0002

#define CRITICAL_SECTION_NO_DEBUG_INFO 0x01000000
#define CONDITION_VARIABLE_LOCKMODE_SHARED 0x1
#define INIT_ONCE_CHECK_ONLY 0x00000001
#define INIT_ONCE_ASYNC 0x00000002
#define INIT_ONCE_INIT_FAILED 0x00000004
#define INIT_ONCE_CTX_RESERVED_BITS 2

#define SRWLOCK_INIT {0}
#define CONDITION_VARIABLE_INIT {0}
#define INIT_ONCE_STATIC_INIT {0}

#define SYNCHRONIZATION_BARRIER_FLAGS_SPIN_ONLY 0x01
#define SYNCHRONIZATION_BARRIER_FLAGS_BLOCK_ONLY 0x02
#define SYNCHRONIZATION_BARRIER_FLAGS_NO_DELETE 0x04

typedef RTL_CRITICAL_SECTION CRITICAL_SECTION, *PCRITICAL_SECTION, *LPCRITICAL_SECTION;
typedef RTL_CRITICAL_SECTION_DEBUG CRITICAL_SECTION_DEBUG, *PCRITICAL_SECTION_DEBUG;
typedef RTL_SRWLOCK SRWLOCK, *PSRWLOCK;
typedef RTL_CONDITION_VARIABLE CONDITION_VARIABLE, *PCONDITION_VARIABLE;
typedef RTL_RUN_ONCE INIT_ONCE, *PINIT_ONCE, *LPINIT_ONCE;
typedef BOOL(WINAPI *PINIT_ONCE_FN)(PINIT_ONCE once, PVOID parameter, PVOID *context);
typedef void(WINAPI *PTIMERAPCROUTINE)(LPVOID arg, DWORD low, DWORD high);

typedef struct _SYNCHRONIZATION_BARRIER {
    DWORD Reserved1;
    DWORD Reserved2;
    ULONG_PTR Reserved3[2];
    DWORD Reserved4;
    DWORD Reserved5;
} SYNCHRONIZATION_BARRIER, *PSYNCHRONIZATION_BARRIER, *LPSYNCHRONIZATION_BARRIER;

typedef struct _REASON_CONTEXT {
    ULONG Version;
    DWORD Flags;
    union {
        struct {
            HMODULE LocalizedReasonModule;
            ULONG LocalizedReasonId;
            ULONG ReasonStringCount;
            LPWSTR *ReasonStrings;
        } Detailed;
        LPWSTR SimpleReasonString;
    } Reason;
} REASON_CONTEXT, *PREASON_CONTEXT;

WINBASEAPI HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL initial, LPCWSTR name);
WINBASEAPI HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL initial, LPCSTR name);
WINBASEAPI HANDLE WINAPI CreateEventExW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name, DWORD flags, DWORD access);
WINBASEAPI HANDLE WINAPI CreateEventExA(LPSECURITY_ATTRIBUTES sa, LPCSTR name, DWORD flags, DWORD access);
WINBASEAPI HANDLE WINAPI OpenEventW(DWORD access, BOOL inherit, LPCWSTR name);
WINBASEAPI HANDLE WINAPI OpenEventA(DWORD access, BOOL inherit, LPCSTR name);
WINBASEAPI BOOL WINAPI SetEvent(HANDLE event);
WINBASEAPI BOOL WINAPI ResetEvent(HANDLE event);
WINBASEAPI BOOL WINAPI PulseEvent(HANDLE event);
WINBASEAPI HANDLE WINAPI CreateMutexW(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCWSTR name);
WINBASEAPI HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCSTR name);
WINBASEAPI HANDLE WINAPI CreateMutexExW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name, DWORD flags, DWORD access);
WINBASEAPI HANDLE WINAPI CreateMutexExA(LPSECURITY_ATTRIBUTES sa, LPCSTR name, DWORD flags, DWORD access);
WINBASEAPI HANDLE WINAPI OpenMutexW(DWORD access, BOOL inherit, LPCWSTR name);
#define CREATE_BOUNDARY_DESCRIPTOR_ADD_APPCONTAINER_SID 0x1
WINBASEAPI HANDLE WINAPI CreateBoundaryDescriptorW(LPCWSTR name, ULONG flags);
WINBASEAPI HANDLE WINAPI CreateBoundaryDescriptorA(LPCSTR name, ULONG flags);
WINBASEAPI BOOL WINAPI AddSIDToBoundaryDescriptor(HANDLE *descriptor, PSID sid);
WINBASEAPI BOOL WINAPI AddIntegrityLabelToBoundaryDescriptor(HANDLE *descriptor, PSID label);
WINBASEAPI VOID WINAPI DeleteBoundaryDescriptor(HANDLE descriptor);
WINBASEAPI HANDLE WINAPI CreatePrivateNamespaceW(LPSECURITY_ATTRIBUTES sa, LPVOID descriptor, LPCWSTR alias);
WINBASEAPI HANDLE WINAPI CreatePrivateNamespaceA(LPSECURITY_ATTRIBUTES sa, LPVOID descriptor, LPCSTR alias);
WINBASEAPI HANDLE WINAPI OpenPrivateNamespaceW(LPVOID descriptor, LPCWSTR alias);
WINBASEAPI HANDLE WINAPI OpenPrivateNamespaceA(LPVOID descriptor, LPCSTR alias);
WINBASEAPI BOOLEAN WINAPI ClosePrivateNamespace(HANDLE handle, ULONG flags);
WINBASEAPI HANDLE WINAPI OpenMutexA(DWORD access, BOOL inherit, LPCSTR name);
WINBASEAPI BOOL WINAPI ReleaseMutex(HANDLE mutex);
WINBASEAPI HANDLE WINAPI CreateSemaphoreW(LPSECURITY_ATTRIBUTES sa, LONG initial, LONG maximum, LPCWSTR name);
WINBASEAPI HANDLE WINAPI CreateSemaphoreA(LPSECURITY_ATTRIBUTES sa, LONG initial, LONG maximum, LPCSTR name);
WINBASEAPI HANDLE WINAPI CreateSemaphoreExW(LPSECURITY_ATTRIBUTES sa, LONG initial, LONG maximum, LPCWSTR name,
                                            DWORD flags, DWORD access);
WINBASEAPI HANDLE WINAPI CreateSemaphoreExA(LPSECURITY_ATTRIBUTES sa, LONG initial, LONG maximum, LPCSTR name,
                                            DWORD flags, DWORD access);
WINBASEAPI HANDLE WINAPI OpenSemaphoreW(DWORD access, BOOL inherit, LPCWSTR name);
WINBASEAPI HANDLE WINAPI OpenSemaphoreA(DWORD access, BOOL inherit, LPCSTR name);
WINBASEAPI BOOL WINAPI ReleaseSemaphore(HANDLE semaphore, LONG count, LPLONG previous);
WINBASEAPI HANDLE WINAPI CreateWaitableTimerW(LPSECURITY_ATTRIBUTES sa, BOOL manual, LPCWSTR name);
WINBASEAPI HANDLE WINAPI CreateWaitableTimerA(LPSECURITY_ATTRIBUTES sa, BOOL manual, LPCSTR name);
WINBASEAPI HANDLE WINAPI CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name, DWORD flags, DWORD access);
WINBASEAPI HANDLE WINAPI OpenWaitableTimerW(DWORD access, BOOL inherit, LPCWSTR name);
WINBASEAPI BOOL WINAPI SetWaitableTimer(HANDLE timer, const LARGE_INTEGER *due, LONG period,
                                        PTIMERAPCROUTINE completion, LPVOID arg, BOOL resume);
WINBASEAPI BOOL WINAPI SetWaitableTimerEx(HANDLE timer, const LARGE_INTEGER *due, LONG period,
                                          PTIMERAPCROUTINE completion, LPVOID arg, PREASON_CONTEXT reason,
                                          ULONG tolerable_delay);
WINBASEAPI BOOL WINAPI CancelWaitableTimer(HANDLE timer);
WINBASEAPI DWORD WINAPI WaitForSingleObject(HANDLE handle, DWORD milliseconds);
WINBASEAPI DWORD WINAPI WaitForSingleObjectEx(HANDLE handle, DWORD milliseconds, BOOL alertable);
WINBASEAPI DWORD WINAPI WaitForMultipleObjects(DWORD count, const HANDLE *handles, BOOL all, DWORD milliseconds);
WINBASEAPI DWORD WINAPI WaitForMultipleObjectsEx(DWORD count, const HANDLE *handles, BOOL all, DWORD milliseconds,
                                                 BOOL alertable);
WINBASEAPI DWORD WINAPI SignalObjectAndWait(HANDLE signal, HANDLE wait, DWORD milliseconds, BOOL alertable);
WINBASEAPI void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION cs);
WINBASEAPI BOOL WINAPI InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION cs, DWORD spin);
WINBASEAPI BOOL WINAPI InitializeCriticalSectionEx(LPCRITICAL_SECTION cs, DWORD spin, DWORD flags);
WINBASEAPI DWORD WINAPI SetCriticalSectionSpinCount(LPCRITICAL_SECTION cs, DWORD spin);
WINBASEAPI void WINAPI EnterCriticalSection(LPCRITICAL_SECTION cs);
WINBASEAPI BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION cs);
WINBASEAPI void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION cs);
WINBASEAPI void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION cs);
WINBASEAPI void WINAPI InitializeSRWLock(PSRWLOCK lock);
WINBASEAPI void WINAPI AcquireSRWLockExclusive(PSRWLOCK lock);
WINBASEAPI void WINAPI AcquireSRWLockShared(PSRWLOCK lock);
WINBASEAPI void WINAPI ReleaseSRWLockExclusive(PSRWLOCK lock);
WINBASEAPI void WINAPI ReleaseSRWLockShared(PSRWLOCK lock);
WINBASEAPI BOOLEAN WINAPI TryAcquireSRWLockExclusive(PSRWLOCK lock);
WINBASEAPI BOOLEAN WINAPI TryAcquireSRWLockShared(PSRWLOCK lock);
WINBASEAPI void WINAPI InitializeConditionVariable(PCONDITION_VARIABLE cv);
WINBASEAPI BOOL WINAPI SleepConditionVariableCS(PCONDITION_VARIABLE cv, PCRITICAL_SECTION cs, DWORD milliseconds);
WINBASEAPI BOOL WINAPI SleepConditionVariableSRW(PCONDITION_VARIABLE cv, PSRWLOCK lock, DWORD milliseconds,
                                                 ULONG flags);
WINBASEAPI void WINAPI WakeConditionVariable(PCONDITION_VARIABLE cv);
WINBASEAPI void WINAPI WakeAllConditionVariable(PCONDITION_VARIABLE cv);
WINBASEAPI void WINAPI InitOnceInitialize(PINIT_ONCE once);
WINBASEAPI BOOL WINAPI InitOnceExecuteOnce(PINIT_ONCE once, PINIT_ONCE_FN fn, PVOID parameter, LPVOID *context);
WINBASEAPI BOOL WINAPI InitOnceBeginInitialize(LPINIT_ONCE once, DWORD flags, PBOOL pending, LPVOID *context);
WINBASEAPI BOOL WINAPI InitOnceComplete(LPINIT_ONCE once, DWORD flags, LPVOID context);
WINBASEAPI BOOL WINAPI WaitOnAddress(volatile void *address, PVOID compare, SIZE_T size, DWORD milliseconds);
WINBASEAPI void WINAPI WakeByAddressSingle(PVOID address);
WINBASEAPI void WINAPI WakeByAddressAll(PVOID address);
WINBASEAPI BOOL WINAPI InitializeSynchronizationBarrier(LPSYNCHRONIZATION_BARRIER barrier, LONG total, LONG spin);
WINBASEAPI BOOL WINAPI EnterSynchronizationBarrier(LPSYNCHRONIZATION_BARRIER barrier, DWORD flags);
WINBASEAPI BOOL WINAPI DeleteSynchronizationBarrier(LPSYNCHRONIZATION_BARRIER barrier);
WINBASEAPI void WINAPI InitializeSListHead(PSLIST_HEADER head);
WINBASEAPI int WINAPI MulDiv(int number, int numerator, int denominator);

/* fiber accessors are inline on windows too. */
FORCEINLINE PVOID GetCurrentFiber(void)
{
    return NtCurrentTeb()->NtTib.FiberData;
}

FORCEINLINE PVOID GetFiberData(void)
{
    return *(PVOID *)GetCurrentFiber();
}

/* interlocked operations are compiler intrinsics on windows; full barriers here too. */
FORCEINLINE LONG InterlockedIncrement(LONG volatile *p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
FORCEINLINE LONG InterlockedDecrement(LONG volatile *p) { return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST); }
FORCEINLINE LONG InterlockedExchange(LONG volatile *p, LONG v) { return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST); }
FORCEINLINE LONG InterlockedExchangeAdd(LONG volatile *p, LONG v) { return __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST); }
FORCEINLINE LONG InterlockedAnd(LONG volatile *p, LONG v) { return __atomic_fetch_and(p, v, __ATOMIC_SEQ_CST); }
FORCEINLINE LONG InterlockedOr(LONG volatile *p, LONG v) { return __atomic_fetch_or(p, v, __ATOMIC_SEQ_CST); }
FORCEINLINE LONG InterlockedXor(LONG volatile *p, LONG v) { return __atomic_fetch_xor(p, v, __ATOMIC_SEQ_CST); }
FORCEINLINE LONG InterlockedCompareExchange(LONG volatile *p, LONG exchange, LONG comparand)
{
    __atomic_compare_exchange_n(p, &comparand, exchange, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}
FORCEINLINE LONG64 InterlockedIncrement64(LONG64 volatile *p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
FORCEINLINE LONG64 InterlockedDecrement64(LONG64 volatile *p) { return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST); }
FORCEINLINE LONG64 InterlockedExchange64(LONG64 volatile *p, LONG64 v)
{
    return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST);
}
FORCEINLINE LONG64 InterlockedExchangeAdd64(LONG64 volatile *p, LONG64 v)
{
    return __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST);
}
FORCEINLINE LONG64 InterlockedCompareExchange64(LONG64 volatile *p, LONG64 exchange, LONG64 comparand)
{
    __atomic_compare_exchange_n(p, &comparand, exchange, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}
FORCEINLINE PVOID InterlockedExchangePointer(PVOID volatile *p, PVOID v)
{
    return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST);
}
FORCEINLINE PVOID InterlockedCompareExchangePointer(PVOID volatile *p, PVOID exchange, PVOID comparand)
{
    __atomic_compare_exchange_n(p, &comparand, exchange, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}
FORCEINLINE void MemoryBarrier(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
#ifdef __x86_64__
FORCEINLINE void YieldProcessor(void) { __asm__ volatile("pause"); }
#else
#if defined(__i386__) || defined(__x86_64__)
FORCEINLINE void YieldProcessor(void) { __asm__ volatile("pause"); }
#else
FORCEINLINE void YieldProcessor(void) { __asm__ volatile("yield"); }
#endif
#endif

WINBASEAPI PSLIST_ENTRY WINAPI InterlockedPushEntrySList(PSLIST_HEADER head, PSLIST_ENTRY entry);
WINBASEAPI PSLIST_ENTRY WINAPI InterlockedPushListSList(PSLIST_HEADER head, PSLIST_ENTRY first, PSLIST_ENTRY last,
                                                        ULONG count);
WINBASEAPI PSLIST_ENTRY WINAPI InterlockedPushListSListEx(PSLIST_HEADER head, PSLIST_ENTRY first,
                                                          PSLIST_ENTRY last, ULONG count);
WINBASEAPI PSLIST_ENTRY WINAPI InterlockedPopEntrySList(PSLIST_HEADER head);
WINBASEAPI PSLIST_ENTRY WINAPI InterlockedFlushSList(PSLIST_HEADER head);
WINBASEAPI USHORT WINAPI QueryDepthSList(PSLIST_HEADER head);

/* ------------------------------------------------------------------ time */

WINBASEAPI DWORD WINAPI GetTickCount(void);
WINBASEAPI ULONGLONG WINAPI GetTickCount64(void);
WINBASEAPI BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *counter);
WINBASEAPI BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *frequency);
WINBASEAPI BOOL WINAPI QueryUnbiasedInterruptTime(PULONGLONG time);
WINBASEAPI void WINAPI QueryInterruptTime(PULONGLONG time);
WINBASEAPI void WINAPI QueryInterruptTimePrecise(PULONGLONG time);
WINBASEAPI void WINAPI QueryUnbiasedInterruptTimePrecise(PULONGLONG time);
WINBASEAPI void WINAPI GetSystemTime(LPSYSTEMTIME time);
WINBASEAPI void WINAPI GetLocalTime(LPSYSTEMTIME time);
WINBASEAPI BOOL WINAPI SetSystemTime(const SYSTEMTIME *time);
WINBASEAPI BOOL WINAPI SetLocalTime(const SYSTEMTIME *time);
WINBASEAPI void WINAPI GetSystemTimeAsFileTime(LPFILETIME time);
WINBASEAPI void WINAPI GetSystemTimePreciseAsFileTime(LPFILETIME time);
WINBASEAPI BOOL WINAPI FileTimeToSystemTime(const FILETIME *file_time, LPSYSTEMTIME system_time);
WINBASEAPI BOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME *system_time, LPFILETIME file_time);
WINBASEAPI BOOL WINAPI FileTimeToLocalFileTime(const FILETIME *file_time, LPFILETIME local);
WINBASEAPI BOOL WINAPI LocalFileTimeToFileTime(const FILETIME *local, LPFILETIME file_time);
WINBASEAPI LONG WINAPI CompareFileTime(const FILETIME *a, const FILETIME *b);
WINBASEAPI DWORD WINAPI GetTimeZoneInformation(LPTIME_ZONE_INFORMATION info);
WINBASEAPI BOOL WINAPI SetTimeZoneInformation(const TIME_ZONE_INFORMATION *info);
WINBASEAPI DWORD WINAPI GetDynamicTimeZoneInformation(PDYNAMIC_TIME_ZONE_INFORMATION info);
WINBASEAPI BOOL WINAPI GetTimeZoneInformationForYear(USHORT year, PDYNAMIC_TIME_ZONE_INFORMATION dtzi,
                                                     LPTIME_ZONE_INFORMATION info);
WINBASEAPI BOOL WINAPI SystemTimeToTzSpecificLocalTime(const TIME_ZONE_INFORMATION *tz, const SYSTEMTIME *universal,
                                                       LPSYSTEMTIME local);
WINBASEAPI BOOL WINAPI TzSpecificLocalTimeToSystemTime(const TIME_ZONE_INFORMATION *tz, const SYSTEMTIME *local,
                                                       LPSYSTEMTIME universal);
WINBASEAPI DWORD WINAPI GetDynamicTimeZoneInformationEffectiveYears(const DYNAMIC_TIME_ZONE_INFORMATION *info,
                                                                    LPDWORD first, LPDWORD last);
WINBASEAPI BOOL WINAPI TzSpecificLocalTimeToSystemTimeEx(const DYNAMIC_TIME_ZONE_INFORMATION *tz,
                                                         const SYSTEMTIME *local, LPSYSTEMTIME universal);
WINBASEAPI BOOL WINAPI SystemTimeToTzSpecificLocalTimeEx(const DYNAMIC_TIME_ZONE_INFORMATION *tz,
                                                         const SYSTEMTIME *universal, LPSYSTEMTIME local);
WINBASEAPI BOOL WINAPI DosDateTimeToFileTime(WORD date, WORD time, LPFILETIME file_time);
WINBASEAPI BOOL WINAPI FileTimeToDosDateTime(const FILETIME *file_time, LPWORD date, LPWORD time);
WINBASEAPI BOOL WINAPI GetSystemTimeAdjustment(PDWORD adjustment, PDWORD increment, PBOOL disabled);
WINBASEAPI BOOL WINAPI SetSystemTimeAdjustment(DWORD adjustment, BOOL disabled);
WINBASEAPI void WINAPI GetSystemTimeAsFileTimeRaw(LPFILETIME time);

/* ------------------------------------------------------------------ system information */

#define VER_PLATFORM_WIN32s 0
#define VER_PLATFORM_WIN32_WINDOWS 1
#define VER_PLATFORM_WIN32_NT 2

#define VER_MINORVERSION 0x0000001
#define VER_MAJORVERSION 0x0000002
#define VER_BUILDNUMBER 0x0000004
#define VER_PLATFORMID 0x0000008
#define VER_SERVICEPACKMINOR 0x0000010
#define VER_SERVICEPACKMAJOR 0x0000020
#define VER_SUITENAME 0x0000040
#define VER_PRODUCT_TYPE 0x0000080

#define VER_EQUAL 1
#define VER_GREATER 2
#define VER_GREATER_EQUAL 3
#define VER_LESS 4
#define VER_LESS_EQUAL 5
#define VER_AND 6
#define VER_OR 7

#define VER_NT_WORKSTATION 0x0000001
#define VER_NT_DOMAIN_CONTROLLER 0x0000002
#define VER_NT_SERVER 0x0000003
#define VER_SUITE_SINGLEUSERTS 0x00000100

#define PRODUCT_UNDEFINED 0x00000000
#define PRODUCT_ULTIMATE 0x00000001
#define PRODUCT_HOME_BASIC 0x00000002
#define PRODUCT_ENTERPRISE 0x00000004
#define PRODUCT_PROFESSIONAL 0x00000030
#define PRODUCT_CORE 0x00000065

#define PF_FLOATING_POINT_PRECISION_ERRATA 0
#define PF_FLOATING_POINT_EMULATED 1
#define PF_COMPARE_EXCHANGE_DOUBLE 2
#define PF_MMX_INSTRUCTIONS_AVAILABLE 3
#define PF_XMMI_INSTRUCTIONS_AVAILABLE 6
#define PF_3DNOW_INSTRUCTIONS_AVAILABLE 7
#define PF_RDTSC_INSTRUCTION_AVAILABLE 8
#define PF_PAE_ENABLED 9
#define PF_XMMI64_INSTRUCTIONS_AVAILABLE 10
#define PF_NX_ENABLED 12
#define PF_SSE3_INSTRUCTIONS_AVAILABLE 13
#define PF_COMPARE_EXCHANGE128 14
#define PF_COMPARE64_EXCHANGE128 15
#define PF_FASTFAIL_AVAILABLE 23
#define PF_AVX_INSTRUCTIONS_AVAILABLE 39
#define PF_AVX2_INSTRUCTIONS_AVAILABLE 40
#define PF_ARM_VFP_32_REGISTERS_AVAILABLE 18
#define PF_ARM_NEON_INSTRUCTIONS_AVAILABLE 19
#define PF_SECOND_LEVEL_ADDRESS_TRANSLATION 20
#define PF_VIRT_FIRMWARE_ENABLED 21
#define PF_ARM_V8_INSTRUCTIONS_AVAILABLE 29
#define PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE 30
#define PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE 31
#define PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE 34
#define PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE 43
#define PF_ARM_V83_JSCVT_INSTRUCTIONS_AVAILABLE 44
#define PF_ARM_V83_LRCPC_INSTRUCTIONS_AVAILABLE 45

typedef struct _SYSTEM_INFO {
    union {
        DWORD dwOemId;
        struct {
            WORD wProcessorArchitecture;
            WORD wReserved;
        };
    };
    DWORD dwPageSize;
    LPVOID lpMinimumApplicationAddress;
    LPVOID lpMaximumApplicationAddress;
    DWORD_PTR dwActiveProcessorMask;
    DWORD dwNumberOfProcessors;
    DWORD dwProcessorType;
    DWORD dwAllocationGranularity;
    WORD wProcessorLevel;
    WORD wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;

typedef struct _OSVERSIONINFOW {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    WCHAR szCSDVersion[128];
} OSVERSIONINFOW, *POSVERSIONINFOW, *LPOSVERSIONINFOW;

typedef struct _OSVERSIONINFOA {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    CHAR szCSDVersion[128];
} OSVERSIONINFOA, *POSVERSIONINFOA, *LPOSVERSIONINFOA;

typedef RTL_OSVERSIONINFOEXW OSVERSIONINFOEXW, *POSVERSIONINFOEXW, *LPOSVERSIONINFOEXW;

typedef struct _OSVERSIONINFOEXA {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    CHAR szCSDVersion[128];
    WORD wServicePackMajor;
    WORD wServicePackMinor;
    WORD wSuiteMask;
    BYTE wProductType;
    BYTE wReserved;
} OSVERSIONINFOEXA, *POSVERSIONINFOEXA, *LPOSVERSIONINFOEXA;

typedef enum _LOGICAL_PROCESSOR_RELATIONSHIP {
    RelationProcessorCore,
    RelationNumaNode,
    RelationCache,
    RelationProcessorPackage,
    RelationGroup,
    RelationProcessorDie,
    RelationNumaNodeEx,
    RelationProcessorModule,
    RelationAll = 0xFFFF
} LOGICAL_PROCESSOR_RELATIONSHIP;

typedef enum _PROCESSOR_CACHE_TYPE {
    CacheUnified,
    CacheInstruction,
    CacheData,
    CacheTrace
} PROCESSOR_CACHE_TYPE;

typedef struct _CACHE_DESCRIPTOR {
    BYTE Level;
    BYTE Associativity;
    WORD LineSize;
    DWORD Size;
    PROCESSOR_CACHE_TYPE Type;
} CACHE_DESCRIPTOR, *PCACHE_DESCRIPTOR;

typedef struct _SYSTEM_LOGICAL_PROCESSOR_INFORMATION {
    ULONG_PTR ProcessorMask;
    LOGICAL_PROCESSOR_RELATIONSHIP Relationship;
    union {
        struct {
            BYTE Flags;
        } ProcessorCore;
        struct {
            DWORD NodeNumber;
        } NumaNode;
        CACHE_DESCRIPTOR Cache;
        ULONGLONG Reserved[2];
    };
} SYSTEM_LOGICAL_PROCESSOR_INFORMATION, *PSYSTEM_LOGICAL_PROCESSOR_INFORMATION;

typedef struct _PROCESSOR_RELATIONSHIP {
    BYTE Flags;
    BYTE EfficiencyClass;
    BYTE Reserved[20];
    WORD GroupCount;
    GROUP_AFFINITY GroupMask[1];
} PROCESSOR_RELATIONSHIP, *PPROCESSOR_RELATIONSHIP;

typedef struct _NUMA_NODE_RELATIONSHIP {
    DWORD NodeNumber;
    BYTE Reserved[18];
    WORD GroupCount;
    GROUP_AFFINITY GroupMask;
} NUMA_NODE_RELATIONSHIP, *PNUMA_NODE_RELATIONSHIP;

typedef struct _CACHE_RELATIONSHIP {
    BYTE Level;
    BYTE Associativity;
    WORD LineSize;
    DWORD CacheSize;
    PROCESSOR_CACHE_TYPE Type;
    BYTE Reserved[18];
    WORD GroupCount;
    GROUP_AFFINITY GroupMask;
} CACHE_RELATIONSHIP, *PCACHE_RELATIONSHIP;

typedef struct _PROCESSOR_GROUP_INFO {
    BYTE MaximumProcessorCount;
    BYTE ActiveProcessorCount;
    BYTE Reserved[38];
    KAFFINITY ActiveProcessorMask;
} PROCESSOR_GROUP_INFO, *PPROCESSOR_GROUP_INFO;

typedef struct _GROUP_RELATIONSHIP {
    WORD MaximumGroupCount;
    WORD ActiveGroupCount;
    BYTE Reserved[20];
    PROCESSOR_GROUP_INFO GroupInfo[1];
} GROUP_RELATIONSHIP, *PGROUP_RELATIONSHIP;

typedef struct _SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX {
    LOGICAL_PROCESSOR_RELATIONSHIP Relationship;
    DWORD Size;
    union {
        PROCESSOR_RELATIONSHIP Processor;
        NUMA_NODE_RELATIONSHIP NumaNode;
        CACHE_RELATIONSHIP Cache;
        GROUP_RELATIONSHIP Group;
    };
} SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, *PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX;

typedef enum _COMPUTER_NAME_FORMAT {
    ComputerNameNetBIOS,
    ComputerNameDnsHostname,
    ComputerNameDnsDomain,
    ComputerNameDnsFullyQualified,
    ComputerNamePhysicalNetBIOS,
    ComputerNamePhysicalDnsHostname,
    ComputerNamePhysicalDnsDomain,
    ComputerNamePhysicalDnsFullyQualified,
    ComputerNameMax
} COMPUTER_NAME_FORMAT;

typedef struct _SYSTEM_POWER_STATUS {
    BYTE ACLineStatus;
    BYTE BatteryFlag;
    BYTE BatteryLifePercent;
    BYTE SystemStatusFlag;
    DWORD BatteryLifeTime;
    DWORD BatteryFullLifeTime;
} SYSTEM_POWER_STATUS, *LPSYSTEM_POWER_STATUS;

#define ES_SYSTEM_REQUIRED ((DWORD)0x00000001)
#define ES_DISPLAY_REQUIRED ((DWORD)0x00000002)
#define ES_USER_PRESENT ((DWORD)0x00000004)
#define ES_AWAYMODE_REQUIRED ((DWORD)0x00000040)
#define ES_CONTINUOUS ((DWORD)0x80000000)

WINBASEAPI void WINAPI GetSystemInfo(LPSYSTEM_INFO info);
WINBASEAPI void WINAPI GetNativeSystemInfo(LPSYSTEM_INFO info);
WINBASEAPI DWORD WINAPI GetVersion(void);
WINBASEAPI UINT WINAPI SetHandleCount(UINT count);
WINBASEAPI BOOL WINAPI GetVersionExW(LPOSVERSIONINFOW info);
WINBASEAPI BOOL WINAPI GetVersionExA(LPOSVERSIONINFOA info);
WINBASEAPI BOOL WINAPI VerifyVersionInfoW(LPOSVERSIONINFOEXW info, DWORD type, DWORDLONG condition);
WINBASEAPI BOOL WINAPI VerifyVersionInfoA(LPOSVERSIONINFOEXA info, DWORD type, DWORDLONG condition);
WINBASEAPI BOOL WINAPI GetProductInfo(DWORD major, DWORD minor, DWORD sp_major, DWORD sp_minor, PDWORD type);
WINBASEAPI BOOL WINAPI IsProcessorFeaturePresent(DWORD feature);
WINBASEAPI void WINAPI GlobalMemoryStatus(LPMEMORYSTATUS status);
WINBASEAPI BOOL WINAPI GlobalMemoryStatusEx(LPMEMORYSTATUSEX status);
WINBASEAPI BOOL WINAPI GetPhysicallyInstalledSystemMemory(PULONGLONG kilobytes);
WINBASEAPI BOOL WINAPI GetComputerNameW(LPWSTR buffer, LPDWORD size);
WINBASEAPI BOOL WINAPI GetComputerNameA(LPSTR buffer, LPDWORD size);
WINBASEAPI BOOL WINAPI GetComputerNameExW(COMPUTER_NAME_FORMAT format, LPWSTR buffer, LPDWORD size);
WINBASEAPI BOOL WINAPI GetComputerNameExA(COMPUTER_NAME_FORMAT format, LPSTR buffer, LPDWORD size);
WINBASEAPI BOOL WINAPI SetComputerNameW(LPCWSTR name);
WINBASEAPI BOOL WINAPI SetComputerNameA(LPCSTR name);
WINBASEAPI BOOL WINAPI SetComputerNameExA(COMPUTER_NAME_FORMAT format, LPCSTR name);
WINBASEAPI BOOL WINAPI SetComputerNameExW(COMPUTER_NAME_FORMAT format, LPCWSTR name);
WINBASEAPI BOOL WINAPI SetComputerNameEx2W(COMPUTER_NAME_FORMAT format, DWORD flags, LPCWSTR name);
WINBASEAPI BOOL WINAPI GetOsSafeBootMode(PDWORD flags);
WINBASEAPI BOOL WINAPI DnsHostnameToComputerNameExW(LPCWSTR host, LPWSTR name, LPDWORD size);
WINBASEAPI BOOL WINAPI GetLogicalProcessorInformation(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer,
                                                      PDWORD length);
WINBASEAPI BOOL WINAPI GetLogicalProcessorInformationEx(LOGICAL_PROCESSOR_RELATIONSHIP relationship,
                                                        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX buffer,
                                                        PDWORD length);
WINBASEAPI WORD WINAPI GetActiveProcessorGroupCount(void);
WINBASEAPI WORD WINAPI GetMaximumProcessorGroupCount(void);
WINBASEAPI DWORD WINAPI GetActiveProcessorCount(WORD group);
WINBASEAPI DWORD WINAPI GetMaximumProcessorCount(WORD group);
WINBASEAPI BOOL WINAPI GetNumaHighestNodeNumber(PULONG node);
WINBASEAPI BOOL WINAPI GetNumaProcessorNode(UCHAR processor, PUCHAR node);
WINBASEAPI BOOL WINAPI GetNumaNodeProcessorMask(UCHAR node, PULONGLONG mask);
WINBASEAPI BOOL WINAPI GetNumaAvailableMemoryNode(UCHAR node, PULONGLONG bytes);
WINBASEAPI UINT WINAPI GetSystemFirmwareTable(DWORD provider, DWORD id, PVOID buffer, DWORD size);
WINBASEAPI UINT WINAPI EnumSystemFirmwareTables(DWORD provider, PVOID buffer, DWORD size);
WINBASEAPI BOOL WINAPI GetSystemPowerStatus(LPSYSTEM_POWER_STATUS status);
WINBASEAPI EXECUTION_STATE WINAPI SetThreadExecutionState(EXECUTION_STATE flags);
WINBASEAPI HANDLE WINAPI PowerCreateRequest(PREASON_CONTEXT context);
WINBASEAPI BOOL WINAPI PowerSetRequest(HANDLE request, int type);
WINBASEAPI BOOL WINAPI PowerClearRequest(HANDLE request, int type);
WINBASEAPI BOOL WINAPI IsNativeVhdBoot(PBOOL native);
WINBASEAPI BOOL WINAPI GetFirmwareType(PDWORD type);
WINBASEAPI DWORD WINAPI GetFirmwareEnvironmentVariableA(LPCSTR name, LPCSTR guid, PVOID buffer, DWORD size);
WINBASEAPI DWORD WINAPI GetFirmwareEnvironmentVariableW(LPCWSTR name, LPCWSTR guid, PVOID buffer, DWORD size);
WINBASEAPI DWORD WINAPI GetFirmwareEnvironmentVariableExA(LPCSTR name, LPCSTR guid, PVOID buffer, DWORD size,
                                                          PDWORD attributes);
WINBASEAPI DWORD WINAPI GetFirmwareEnvironmentVariableExW(LPCWSTR name, LPCWSTR guid, PVOID buffer, DWORD size,
                                                          PDWORD attributes);
WINBASEAPI BOOL WINAPI SetFirmwareEnvironmentVariableA(LPCSTR name, LPCSTR guid, PVOID buffer, DWORD size);
WINBASEAPI BOOL WINAPI SetFirmwareEnvironmentVariableW(LPCWSTR name, LPCWSTR guid, PVOID buffer, DWORD size);
WINBASEAPI BOOL WINAPI SetFirmwareEnvironmentVariableExA(LPCSTR name, LPCSTR guid, PVOID buffer, DWORD size,
                                                         DWORD attributes);
WINBASEAPI BOOL WINAPI SetFirmwareEnvironmentVariableExW(LPCWSTR name, LPCWSTR guid, PVOID buffer, DWORD size,
                                                         DWORD attributes);
WINBASEAPI BOOL WINAPI Beep(DWORD frequency, DWORD duration);

/* ------------------------------------------------------------------ environment */

WINBASEAPI DWORD WINAPI GetEnvironmentVariableW(LPCWSTR name, LPWSTR buffer, DWORD size);
WINBASEAPI DWORD WINAPI GetEnvironmentVariableA(LPCSTR name, LPSTR buffer, DWORD size);
WINBASEAPI BOOL WINAPI SetEnvironmentVariableW(LPCWSTR name, LPCWSTR value);
WINBASEAPI BOOL WINAPI SetEnvironmentVariableA(LPCSTR name, LPCSTR value);
WINBASEAPI LPWCH WINAPI GetEnvironmentStringsW(void);
WINBASEAPI LPCH WINAPI GetEnvironmentStringsA(void);
WINBASEAPI LPCH WINAPI GetEnvironmentStrings(void);
WINBASEAPI BOOL WINAPI SetEnvironmentStringsW(LPWCH env);
WINBASEAPI BOOL WINAPI FreeEnvironmentStringsW(LPWCH env);
WINBASEAPI BOOL WINAPI FreeEnvironmentStringsA(LPCH env);
WINBASEAPI DWORD WINAPI ExpandEnvironmentStringsW(LPCWSTR src, LPWSTR dst, DWORD size);
WINBASEAPI DWORD WINAPI ExpandEnvironmentStringsA(LPCSTR src, LPSTR dst, DWORD size);

/* ------------------------------------------------------------------ strings */

WINBASEAPI int WINAPI lstrlenW(LPCWSTR s);
WINBASEAPI int WINAPI lstrlenA(LPCSTR s);
WINBASEAPI LPWSTR WINAPI lstrcpyW(LPWSTR dst, LPCWSTR src);
WINBASEAPI LPSTR WINAPI lstrcpyA(LPSTR dst, LPCSTR src);
WINBASEAPI LPWSTR WINAPI lstrcpynW(LPWSTR dst, LPCWSTR src, int count);
WINBASEAPI LPSTR WINAPI lstrcpynA(LPSTR dst, LPCSTR src, int count);
WINBASEAPI LPWSTR WINAPI lstrcatW(LPWSTR dst, LPCWSTR src);
WINBASEAPI LPSTR WINAPI lstrcatA(LPSTR dst, LPCSTR src);
WINBASEAPI int WINAPI lstrcmpW(LPCWSTR a, LPCWSTR b);
WINBASEAPI int WINAPI lstrcmpA(LPCSTR a, LPCSTR b);
WINBASEAPI int WINAPI lstrcmpiW(LPCWSTR a, LPCWSTR b);
WINBASEAPI int WINAPI lstrcmpiA(LPCSTR a, LPCSTR b);

/* ------------------------------------------------------------------ atoms */

WINBASEAPI ATOM WINAPI AddAtomW(LPCWSTR name);
WINBASEAPI ATOM WINAPI AddAtomA(LPCSTR name);
WINBASEAPI ATOM WINAPI FindAtomW(LPCWSTR name);
WINBASEAPI ATOM WINAPI FindAtomA(LPCSTR name);
WINBASEAPI ATOM WINAPI DeleteAtom(ATOM atom);
WINBASEAPI UINT WINAPI GetAtomNameW(ATOM atom, LPWSTR buffer, int size);
WINBASEAPI UINT WINAPI GetAtomNameA(ATOM atom, LPSTR buffer, int size);
WINBASEAPI BOOL WINAPI InitAtomTable(DWORD size);
WINBASEAPI ATOM WINAPI GlobalAddAtomW(LPCWSTR name);
WINBASEAPI ATOM WINAPI GlobalAddAtomA(LPCSTR name);
WINBASEAPI ATOM WINAPI GlobalFindAtomW(LPCWSTR name);
WINBASEAPI ATOM WINAPI GlobalFindAtomA(LPCSTR name);
WINBASEAPI ATOM WINAPI GlobalDeleteAtom(ATOM atom);
WINBASEAPI UINT WINAPI GlobalGetAtomNameW(ATOM atom, LPWSTR buffer, int size);
WINBASEAPI UINT WINAPI GlobalGetAtomNameA(ATOM atom, LPSTR buffer, int size);

/* ------------------------------------------------------------------ exceptions and debugging */

#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION (-1)

#define EXCEPTION_ACCESS_VIOLATION STATUS_ACCESS_VIOLATION
#define EXCEPTION_DATATYPE_MISALIGNMENT STATUS_DATATYPE_MISALIGNMENT
#define EXCEPTION_BREAKPOINT STATUS_BREAKPOINT
#define EXCEPTION_SINGLE_STEP STATUS_SINGLE_STEP
#define EXCEPTION_INT_DIVIDE_BY_ZERO STATUS_INTEGER_DIVIDE_BY_ZERO
#define EXCEPTION_PRIV_INSTRUCTION STATUS_PRIVILEGED_INSTRUCTION
#define EXCEPTION_IN_PAGE_ERROR STATUS_IN_PAGE_ERROR
#define EXCEPTION_ILLEGAL_INSTRUCTION STATUS_ILLEGAL_INSTRUCTION
#define EXCEPTION_NONCONTINUABLE_EXCEPTION STATUS_NONCONTINUABLE_EXCEPTION
#define EXCEPTION_STACK_OVERFLOW STATUS_STACK_OVERFLOW
#define EXCEPTION_INVALID_DISPOSITION STATUS_INVALID_DISPOSITION
#define EXCEPTION_GUARD_PAGE STATUS_GUARD_PAGE_VIOLATION
#define EXCEPTION_INVALID_HANDLE STATUS_INVALID_HANDLE
#define DBG_PRINTEXCEPTION_C ((DWORD)0x40010006L)
#define DBG_PRINTEXCEPTION_WIDE_C ((DWORD)0x4001000AL)
#define DBG_CONTROL_C ((DWORD)0x40010005L)

typedef LONG(WINAPI *PTOP_LEVEL_EXCEPTION_FILTER)(PEXCEPTION_POINTERS info);
typedef PTOP_LEVEL_EXCEPTION_FILTER LPTOP_LEVEL_EXCEPTION_FILTER;
typedef EXCEPTION_RECORD *LPEXCEPTION_RECORD;
typedef CONTEXT *LPCONTEXT;
typedef DWORD(WINAPI *APPLICATION_RECOVERY_CALLBACK)(PVOID parameter);

#define FAIL_FAST_GENERATE_EXCEPTION_ADDRESS 0x1
#define FAIL_FAST_NO_HARD_ERROR_DLG 0x2

WINBASEAPI void WINAPI RaiseException(DWORD code, DWORD flags, DWORD count, const ULONG_PTR *arguments);
WINBASEAPI DECLSPEC_NORETURN void WINAPI RaiseFailFastException(PEXCEPTION_RECORD record, PCONTEXT context,
                                                                DWORD flags);
WINBASEAPI LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter);
WINBASEAPI LONG WINAPI UnhandledExceptionFilter(PEXCEPTION_POINTERS info);
WINBASEAPI PVOID WINAPI AddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER handler);
WINBASEAPI ULONG WINAPI RemoveVectoredExceptionHandler(PVOID handle);
WINBASEAPI PVOID WINAPI AddVectoredContinueHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER handler);
WINBASEAPI ULONG WINAPI RemoveVectoredContinueHandler(PVOID handle);
WINBASEAPI void WINAPI OutputDebugStringW(LPCWSTR text);
WINBASEAPI void WINAPI OutputDebugStringA(LPCSTR text);
WINBASEAPI BOOL WINAPI IsDebuggerPresent(void);
WINBASEAPI BOOL WINAPI CheckRemoteDebuggerPresent(HANDLE process, PBOOL present);
WINBASEAPI void WINAPI DebugBreak(void);
WINBASEAPI BOOL WINAPI DebugBreakProcess(HANDLE process);
/* the event a debugger waits for, its union of what happened held as words. */
typedef struct _DEBUG_EVENT {
    DWORD dwDebugEventCode;
    DWORD dwProcessId;
    DWORD dwThreadId;
    union {
        ULONG_PTR Words[20];
    } u;
} DEBUG_EVENT, *LPDEBUG_EVENT;
WINBASEAPI BOOL WINAPI WaitForDebugEvent(LPDEBUG_EVENT event, DWORD milliseconds);
WINBASEAPI BOOL WINAPI WaitForDebugEventEx(LPDEBUG_EVENT event, DWORD milliseconds);
WINBASEAPI BOOL WINAPI ContinueDebugEvent(DWORD process, DWORD thread, DWORD status);
WINBASEAPI BOOL WINAPI DebugSetProcessKillOnExit(BOOL kill);
WINBASEAPI BOOL WINAPI DebugActiveProcess(DWORD id);
WINBASEAPI BOOL WINAPI DebugActiveProcessStop(DWORD id);
WINBASEAPI DECLSPEC_NORETURN void WINAPI FatalAppExitW(UINT action, LPCWSTR message);
WINBASEAPI DECLSPEC_NORETURN void WINAPI FatalAppExitA(UINT action, LPCSTR message);
WINBASEAPI DECLSPEC_NORETURN void WINAPI FatalExit(int code);
WINBASEAPI HRESULT WINAPI RegisterApplicationRestart(PCWSTR command_line, DWORD flags);
WINBASEAPI HRESULT WINAPI UnregisterApplicationRestart(void);
WINBASEAPI HRESULT WINAPI RegisterApplicationRecoveryCallback(APPLICATION_RECOVERY_CALLBACK callback, PVOID parameter,
                                                              DWORD ping, DWORD flags);
WINBASEAPI HRESULT WINAPI UnregisterApplicationRecoveryCallback(void);
WINBASEAPI HRESULT WINAPI GetApplicationRecoveryCallback(HANDLE process, APPLICATION_RECOVERY_CALLBACK *callback,
                                                         PVOID *parameter, PDWORD ping, PDWORD flags);
WINBASEAPI HRESULT WINAPI GetApplicationRestartSettings(HANDLE process, PWSTR command_line, PDWORD size,
                                                        PDWORD flags);
WINBASEAPI HRESULT WINAPI ApplicationRecoveryInProgress(PBOOL cancelled);
WINBASEAPI void WINAPI ApplicationRecoveryFinished(BOOL success);
WINBASEAPI PVOID WINAPI EncodePointer(PVOID p);
WINBASEAPI PVOID WINAPI DecodePointer(PVOID p);
WINBASEAPI PVOID WINAPI EncodeSystemPointer(PVOID p);
WINBASEAPI PVOID WINAPI DecodeSystemPointer(PVOID p);
WINBASEAPI DWORD WINAPI WerRegisterRuntimeExceptionModule(PCWSTR dll, PVOID context);
WINBASEAPI DWORD WINAPI WerUnregisterRuntimeExceptionModule(PCWSTR dll, PVOID context);
WINBASEAPI HRESULT WINAPI WerGetFlags(HANDLE process, PDWORD flags);
WINBASEAPI HRESULT WINAPI WerSetFlags(DWORD flags);
WINBASEAPI HRESULT WINAPI WerRegisterMemoryBlock(PVOID address, DWORD size);
WINBASEAPI HRESULT WINAPI WerRegisterFile(PCWSTR file, int type, DWORD flags);

/* ------------------------------------------------------------------ resources */

#define RT_CURSOR MAKEINTRESOURCE(1)
#define RT_BITMAP MAKEINTRESOURCE(2)
#define RT_ICON MAKEINTRESOURCE(3)
#define RT_MENU MAKEINTRESOURCE(4)
#define RT_DIALOG MAKEINTRESOURCE(5)
#define RT_STRING MAKEINTRESOURCE(6)
#define RT_FONTDIR MAKEINTRESOURCE(7)
#define RT_FONT MAKEINTRESOURCE(8)
#define RT_ACCELERATOR MAKEINTRESOURCE(9)
#define RT_RCDATA MAKEINTRESOURCE(10)
#define RT_MESSAGETABLE MAKEINTRESOURCE(11)
#define RT_GROUP_CURSOR MAKEINTRESOURCE(12)
#define RT_GROUP_ICON MAKEINTRESOURCE(14)
#define RT_VERSION MAKEINTRESOURCE(16)
#define RT_DLGINCLUDE MAKEINTRESOURCE(17)
#define RT_PLUGPLAY MAKEINTRESOURCE(19)
#define RT_VXD MAKEINTRESOURCE(20)
#define RT_ANICURSOR MAKEINTRESOURCE(21)
#define RT_ANIICON MAKEINTRESOURCE(22)
#define RT_HTML MAKEINTRESOURCE(23)
#define RT_MANIFEST MAKEINTRESOURCE(24)

#ifndef MAKEINTRESOURCE
#ifdef UNICODE
#define MAKEINTRESOURCE MAKEINTRESOURCEW
#else
#define MAKEINTRESOURCE MAKEINTRESOURCEA
#endif
#endif

#define RESOURCE_ENUM_LN 0x0001
#define RESOURCE_ENUM_MUI 0x0002
#define RESOURCE_ENUM_MUI_SYSTEM 0x0004
#define RESOURCE_ENUM_VALIDATE 0x0008

typedef BOOL(CALLBACK *ENUMRESTYPEPROCW)(HMODULE module, LPWSTR type, LONG_PTR param);
typedef BOOL(CALLBACK *ENUMRESTYPEPROCA)(HMODULE module, LPSTR type, LONG_PTR param);
typedef BOOL(CALLBACK *ENUMRESNAMEPROCW)(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR param);
typedef BOOL(CALLBACK *ENUMRESNAMEPROCA)(HMODULE module, LPCSTR type, LPSTR name, LONG_PTR param);
typedef BOOL(CALLBACK *ENUMRESLANGPROCW)(HMODULE module, LPCWSTR type, LPCWSTR name, WORD language, LONG_PTR param);
typedef BOOL(CALLBACK *ENUMRESLANGPROCA)(HMODULE module, LPCSTR type, LPCSTR name, WORD language, LONG_PTR param);

WINBASEAPI HRSRC WINAPI FindResourceW(HMODULE module, LPCWSTR name, LPCWSTR type);
WINBASEAPI HRSRC WINAPI FindResourceA(HMODULE module, LPCSTR name, LPCSTR type);
WINBASEAPI HRSRC WINAPI FindResourceExW(HMODULE module, LPCWSTR type, LPCWSTR name, WORD language);
WINBASEAPI HRSRC WINAPI FindResourceExA(HMODULE module, LPCSTR type, LPCSTR name, WORD language);
WINBASEAPI HGLOBAL WINAPI LoadResource(HMODULE module, HRSRC resource);
WINBASEAPI LPVOID WINAPI LockResource(HGLOBAL data);
WINBASEAPI DWORD WINAPI SizeofResource(HMODULE module, HRSRC resource);
WINBASEAPI BOOL WINAPI FreeResource(HGLOBAL data);
WINBASEAPI BOOL WINAPI EnumResourceTypesW(HMODULE module, ENUMRESTYPEPROCW proc, LONG_PTR param);
WINBASEAPI BOOL WINAPI EnumResourceTypesA(HMODULE module, ENUMRESTYPEPROCA proc, LONG_PTR param);
WINBASEAPI BOOL WINAPI EnumResourceTypesExW(HMODULE module, ENUMRESTYPEPROCW proc, LONG_PTR param, DWORD flags,
                                            LANGID lang);
WINBASEAPI BOOL WINAPI EnumResourceNamesW(HMODULE module, LPCWSTR type, ENUMRESNAMEPROCW proc, LONG_PTR param);
WINBASEAPI BOOL WINAPI EnumResourceNamesA(HMODULE module, LPCSTR type, ENUMRESNAMEPROCA proc, LONG_PTR param);
WINBASEAPI BOOL WINAPI EnumResourceNamesExW(HMODULE module, LPCWSTR type, ENUMRESNAMEPROCW proc, LONG_PTR param,
                                            DWORD flags, LANGID lang);
WINBASEAPI BOOL WINAPI EnumResourceLanguagesW(HMODULE module, LPCWSTR type, LPCWSTR name, ENUMRESLANGPROCW proc,
                                              LONG_PTR param);
WINBASEAPI BOOL WINAPI EnumResourceLanguagesA(HMODULE module, LPCSTR type, LPCSTR name, ENUMRESLANGPROCA proc,
                                              LONG_PTR param);
WINBASEAPI BOOL WINAPI EnumResourceLanguagesExW(HMODULE module, LPCWSTR type, LPCWSTR name, ENUMRESLANGPROCW proc,
                                                LONG_PTR param, DWORD flags, LANGID lang);
WINBASEAPI HANDLE WINAPI BeginUpdateResourceW(LPCWSTR file, BOOL delete_existing);
WINBASEAPI BOOL WINAPI UpdateResourceW(HANDLE update, LPCWSTR type, LPCWSTR name, WORD language, LPVOID data,
                                       DWORD size);
WINBASEAPI BOOL WINAPI EndUpdateResourceW(HANDLE update, BOOL discard);

/* ------------------------------------------------------------------ packages and app model */

WINBASEAPI LONG WINAPI GetCurrentPackageId(UINT32 *length, BYTE *buffer);
WINBASEAPI LONG WINAPI GetCurrentPackageFullName(UINT32 *length, PWSTR name);
WINBASEAPI LONG WINAPI GetCurrentPackageFamilyName(UINT32 *length, PWSTR name);
WINBASEAPI LONG WINAPI GetCurrentApplicationUserModelId(UINT32 *length, PWSTR id);
WINBASEAPI LONG WINAPI GetPackageFullName(HANDLE process, UINT32 *length, PWSTR name);
WINBASEAPI LONG WINAPI GetPackageFamilyName(HANDLE process, UINT32 *length, PWSTR name);
WINBASEAPI LONG WINAPI GetApplicationUserModelId(HANDLE process, UINT32 *length, PWSTR id);

/* ------------------------------------------------------------------ registry */

#define HKEY_CLASSES_ROOT ((HKEY)(ULONG_PTR)(LONG)0x80000000)
#define HKEY_CURRENT_USER ((HKEY)(ULONG_PTR)(LONG)0x80000001)
#define HKEY_LOCAL_MACHINE ((HKEY)(ULONG_PTR)(LONG)0x80000002)
#define HKEY_USERS ((HKEY)(ULONG_PTR)(LONG)0x80000003)
#define HKEY_PERFORMANCE_DATA ((HKEY)(ULONG_PTR)(LONG)0x80000004)
#define HKEY_CURRENT_CONFIG ((HKEY)(ULONG_PTR)(LONG)0x80000005)
#define HKEY_DYN_DATA ((HKEY)(ULONG_PTR)(LONG)0x80000006)
#define HKEY_CURRENT_USER_LOCAL_SETTINGS ((HKEY)(ULONG_PTR)(LONG)0x80000007)

#define KEY_QUERY_VALUE 0x0001
#define KEY_SET_VALUE 0x0002
#define KEY_CREATE_SUB_KEY 0x0004
#define KEY_ENUMERATE_SUB_KEYS 0x0008
#define KEY_NOTIFY 0x0010
#define KEY_CREATE_LINK 0x0020
#define KEY_WOW64_32KEY 0x0200
#define KEY_WOW64_64KEY 0x0100
#define KEY_WOW64_RES 0x0300
#define KEY_READ ((STANDARD_RIGHTS_READ | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | KEY_NOTIFY) & ~SYNCHRONIZE)
#define KEY_WRITE ((STANDARD_RIGHTS_WRITE | KEY_SET_VALUE | KEY_CREATE_SUB_KEY) & ~SYNCHRONIZE)
#define KEY_EXECUTE (KEY_READ & ~SYNCHRONIZE)
#define KEY_ALL_ACCESS                                                                                            \
    ((STANDARD_RIGHTS_ALL | KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_CREATE_SUB_KEY | KEY_ENUMERATE_SUB_KEYS |      \
      KEY_NOTIFY | KEY_CREATE_LINK) &                                                                             \
     ~SYNCHRONIZE)

#define REG_OPTION_RESERVED 0x00000000
#define REG_OPTION_NON_VOLATILE 0x00000000
#define REG_OPTION_VOLATILE 0x00000001
#define REG_OPTION_CREATE_LINK 0x00000002
#define REG_OPTION_BACKUP_RESTORE 0x00000004
#define REG_OPTION_OPEN_LINK 0x00000008
#define REG_CREATED_NEW_KEY 0x00000001
#define REG_OPENED_EXISTING_KEY 0x00000002

#define REG_NONE 0
#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_DWORD_LITTLE_ENDIAN 4
#define REG_DWORD_BIG_ENDIAN 5
#define REG_LINK 6
#define REG_MULTI_SZ 7
#define REG_RESOURCE_LIST 8
#define REG_FULL_RESOURCE_DESCRIPTOR 9
#define REG_RESOURCE_REQUIREMENTS_LIST 10
#define REG_QWORD 11
#define REG_QWORD_LITTLE_ENDIAN 11

#define RRF_RT_REG_NONE 0x00000001
#define RRF_RT_REG_SZ 0x00000002
#define RRF_RT_REG_EXPAND_SZ 0x00000004
#define RRF_RT_REG_BINARY 0x00000008
#define RRF_RT_REG_DWORD 0x00000010
#define RRF_RT_REG_MULTI_SZ 0x00000020
#define RRF_RT_REG_QWORD 0x00000040
#define RRF_RT_DWORD (RRF_RT_REG_BINARY | RRF_RT_REG_DWORD)
#define RRF_RT_QWORD (RRF_RT_REG_BINARY | RRF_RT_REG_QWORD)
#define RRF_RT_ANY 0x0000FFFF
#define RRF_SUBKEY_WOW6464KEY 0x00010000
#define RRF_SUBKEY_WOW6432KEY 0x00020000
#define RRF_NOEXPAND 0x10000000
#define REG_MUI_STRING_TRUNCATE 0x00000001
#define RRF_ZEROONFAILURE 0x20000000

WINBASEAPI LSTATUS WINAPI RegOpenKeyExW(HKEY key, LPCWSTR subkey, DWORD options, REGSAM access, HKEY *result);
WINBASEAPI LSTATUS WINAPI RegDuplicateHKey(HKEY key, HKEY *result);
WINBASEAPI LSTATUS WINAPI RegOpenKeyExA(HKEY key, LPCSTR subkey, DWORD options, REGSAM access, HKEY *result);
WINBASEAPI LSTATUS WINAPI RegOpenKeyW(HKEY key, LPCWSTR subkey, HKEY *result);
WINBASEAPI LSTATUS WINAPI RegOpenKeyA(HKEY key, LPCSTR subkey, HKEY *result);
WINBASEAPI LSTATUS WINAPI RegCreateKeyExW(HKEY key, LPCWSTR subkey, DWORD reserved, LPWSTR klass, DWORD options,
                                          REGSAM access, const SECURITY_ATTRIBUTES *sa, HKEY *result,
                                          LPDWORD disposition);
WINBASEAPI LSTATUS WINAPI RegCreateKeyExA(HKEY key, LPCSTR subkey, DWORD reserved, LPSTR klass, DWORD options,
                                          REGSAM access, const SECURITY_ATTRIBUTES *sa, HKEY *result,
                                          LPDWORD disposition);
WINBASEAPI LSTATUS WINAPI RegCreateKeyW(HKEY key, LPCWSTR subkey, HKEY *result);
WINBASEAPI LSTATUS WINAPI RegCreateKeyA(HKEY key, LPCSTR subkey, HKEY *result);
WINBASEAPI LSTATUS WINAPI RegCloseKey(HKEY key);
WINBASEAPI LSTATUS WINAPI RegQueryValueExW(HKEY key, LPCWSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data,
                                           LPDWORD size);
WINBASEAPI LSTATUS WINAPI RegQueryValueExA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data,
                                           LPDWORD size);
WINBASEAPI LSTATUS WINAPI RegSetValueExW(HKEY key, LPCWSTR name, DWORD reserved, DWORD type, const BYTE *data,
                                         DWORD size);
WINBASEAPI LSTATUS WINAPI RegSetValueExA(HKEY key, LPCSTR name, DWORD reserved, DWORD type, const BYTE *data,
                                         DWORD size);
typedef struct _VALENTW {
    LPWSTR ve_valuename;
    DWORD ve_valuelen;
    DWORD_PTR ve_valueptr;
    DWORD ve_type;
} VALENTW, *PVALENTW;
typedef struct _VALENTA {
    LPSTR ve_valuename;
    DWORD ve_valuelen;
    DWORD_PTR ve_valueptr;
    DWORD ve_type;
} VALENTA, *PVALENTA;
WINBASEAPI LSTATUS WINAPI RegQueryMultipleValuesW(HKEY key, PVALENTW list, DWORD count, LPWSTR buffer, LPDWORD total);
WINBASEAPI LSTATUS WINAPI RegQueryMultipleValuesA(HKEY key, PVALENTA list, DWORD count, LPSTR buffer, LPDWORD total);
WINBASEAPI LSTATUS WINAPI RegCopyTreeW(HKEY source, LPCWSTR subkey, HKEY destination);
WINBASEAPI LSTATUS WINAPI RegCopyTreeA(HKEY source, LPCSTR subkey, HKEY destination);
WINBASEAPI LSTATUS WINAPI RegOpenUserClassesRoot(HANDLE token, DWORD options, REGSAM access, HKEY *result);
WINBASEAPI LSTATUS WINAPI RegSaveKeyW(HKEY key, LPCWSTR file, LPSECURITY_ATTRIBUTES attributes);
WINBASEAPI LSTATUS WINAPI RegSaveKeyA(HKEY key, LPCSTR file, LPSECURITY_ATTRIBUTES attributes);
WINBASEAPI LSTATUS WINAPI RegSaveKeyExW(HKEY key, LPCWSTR file, LPSECURITY_ATTRIBUTES attributes, DWORD flags);
WINBASEAPI LSTATUS WINAPI RegSaveKeyExA(HKEY key, LPCSTR file, LPSECURITY_ATTRIBUTES attributes, DWORD flags);
WINBASEAPI LSTATUS WINAPI RegRestoreKeyW(HKEY key, LPCWSTR file, DWORD flags);
WINBASEAPI LSTATUS WINAPI RegRestoreKeyA(HKEY key, LPCSTR file, DWORD flags);
WINBASEAPI LSTATUS WINAPI RegReplaceKeyW(HKEY key, LPCWSTR subkey, LPCWSTR new_file, LPCWSTR old_file);
WINBASEAPI LSTATUS WINAPI RegReplaceKeyA(HKEY key, LPCSTR subkey, LPCSTR new_file, LPCSTR old_file);
WINBASEAPI LSTATUS WINAPI RegLoadAppKeyW(LPCWSTR file, HKEY *result, REGSAM access, DWORD options, DWORD reserved);
WINBASEAPI LSTATUS WINAPI RegLoadAppKeyA(LPCSTR file, HKEY *result, REGSAM access, DWORD options, DWORD reserved);
WINBASEAPI LSTATUS WINAPI RegGetValueW(HKEY key, LPCWSTR subkey, LPCWSTR value, DWORD flags, LPDWORD type,
                                       PVOID data, LPDWORD size);
WINBASEAPI LSTATUS WINAPI RegLoadMUIStringA(HKEY key, LPCSTR value, LPSTR out, DWORD out_size, LPDWORD needed, DWORD flags,
                                            LPCSTR directory);
WINBASEAPI LSTATUS WINAPI RegLoadMUIStringW(HKEY key, LPCWSTR value, LPWSTR out, DWORD out_size, LPDWORD needed,
                                            DWORD flags, LPCWSTR directory);
WINBASEAPI LSTATUS WINAPI RegGetValueA(HKEY key, LPCSTR subkey, LPCSTR value, DWORD flags, LPDWORD type, PVOID data,
                                       LPDWORD size);
WINBASEAPI LSTATUS WINAPI RegEnumKeyExW(HKEY key, DWORD index, LPWSTR name, LPDWORD name_size, LPDWORD reserved,
                                        LPWSTR klass, LPDWORD class_size, PFILETIME last_write);
WINBASEAPI LSTATUS WINAPI RegEnumKeyExA(HKEY key, DWORD index, LPSTR name, LPDWORD name_size, LPDWORD reserved,
                                        LPSTR klass, LPDWORD class_size, PFILETIME last_write);
WINBASEAPI LSTATUS WINAPI RegEnumKeyW(HKEY key, DWORD index, LPWSTR name, DWORD size);
WINBASEAPI LSTATUS WINAPI RegEnumValueW(HKEY key, DWORD index, LPWSTR name, LPDWORD name_size, LPDWORD reserved,
                                        LPDWORD type, LPBYTE data, LPDWORD data_size);
WINBASEAPI LSTATUS WINAPI RegEnumValueA(HKEY key, DWORD index, LPSTR name, LPDWORD name_size, LPDWORD reserved,
                                        LPDWORD type, LPBYTE data, LPDWORD data_size);
WINBASEAPI LSTATUS WINAPI RegQueryInfoKeyW(HKEY key, LPWSTR klass, LPDWORD class_size, LPDWORD reserved,
                                           LPDWORD subkeys, LPDWORD max_subkey, LPDWORD max_class, LPDWORD values,
                                           LPDWORD max_value_name, LPDWORD max_value, LPDWORD security,
                                           PFILETIME last_write);
WINBASEAPI LSTATUS WINAPI RegDeleteKeyW(HKEY key, LPCWSTR subkey);
WINBASEAPI LSTATUS WINAPI RegDeleteKeyA(HKEY key, LPCSTR subkey);
WINBASEAPI LSTATUS WINAPI RegDeleteKeyExW(HKEY key, LPCWSTR subkey, REGSAM access, DWORD reserved);
WINBASEAPI LSTATUS WINAPI RegDeleteValueW(HKEY key, LPCWSTR name);
WINBASEAPI LSTATUS WINAPI RegDeleteValueA(HKEY key, LPCSTR name);
WINBASEAPI LSTATUS WINAPI RegDeleteTreeW(HKEY key, LPCWSTR subkey);
WINBASEAPI LSTATUS WINAPI RegFlushKey(HKEY key);
WINBASEAPI LSTATUS WINAPI RegNotifyChangeKeyValue(HKEY key, BOOL subtree, DWORD filter, HANDLE event, BOOL async);
WINBASEAPI LSTATUS WINAPI RegOpenCurrentUser(REGSAM access, HKEY *result);
WINBASEAPI LSTATUS WINAPI RegOverridePredefKey(HKEY key, HKEY new_key);
WINBASEAPI LSTATUS WINAPI RegDisablePredefinedCacheEx(void);
WINBASEAPI LSTATUS WINAPI RegEnumKeyA(HKEY key, DWORD index, LPSTR name, DWORD size);
WINBASEAPI LSTATUS WINAPI RegQueryInfoKeyA(HKEY key, LPSTR klass, LPDWORD class_size, LPDWORD reserved,
                                           LPDWORD subkeys, LPDWORD max_subkey, LPDWORD max_class, LPDWORD values,
                                           LPDWORD max_value_name, LPDWORD max_value, LPDWORD security,
                                           PFILETIME last_write);
WINBASEAPI LSTATUS WINAPI RegDeleteKeyExA(HKEY key, LPCSTR subkey, REGSAM access, DWORD reserved);
WINBASEAPI LSTATUS WINAPI RegDeleteTreeA(HKEY key, LPCSTR subkey);
WINBASEAPI LSTATUS WINAPI RegSetKeyValueW(HKEY key, LPCWSTR subkey, LPCWSTR value, DWORD type, LPCVOID data,
                                          DWORD size);
WINBASEAPI LSTATUS WINAPI RegSetKeyValueA(HKEY key, LPCSTR subkey, LPCSTR value, DWORD type, LPCVOID data,
                                          DWORD size);
WINBASEAPI LSTATUS WINAPI RegDeleteKeyValueW(HKEY key, LPCWSTR subkey, LPCWSTR value);
WINBASEAPI LSTATUS WINAPI RegDeleteKeyValueA(HKEY key, LPCSTR subkey, LPCSTR value);

/* serial ports. */
typedef struct _DCB {
    DWORD DCBlength;
    DWORD BaudRate;
    DWORD fBinary : 1;
    DWORD fParity : 1;
    DWORD fOutxCtsFlow : 1;
    DWORD fOutxDsrFlow : 1;
    DWORD fDtrControl : 2;
    DWORD fDsrSensitivity : 1;
    DWORD fTXContinueOnXoff : 1;
    DWORD fOutX : 1;
    DWORD fInX : 1;
    DWORD fErrorChar : 1;
    DWORD fNull : 1;
    DWORD fRtsControl : 2;
    DWORD fAbortOnError : 1;
    DWORD fDummy2 : 17;
    WORD wReserved;
    WORD XonLim;
    WORD XoffLim;
    BYTE ByteSize;
    BYTE Parity;
    BYTE StopBits;
    char XonChar;
    char XoffChar;
    char ErrorChar;
    char EofChar;
    char EvtChar;
    WORD wReserved1;
} DCB, *LPDCB;

typedef struct _COMMTIMEOUTS {
    DWORD ReadIntervalTimeout;
    DWORD ReadTotalTimeoutMultiplier;
    DWORD ReadTotalTimeoutConstant;
    DWORD WriteTotalTimeoutMultiplier;
    DWORD WriteTotalTimeoutConstant;
} COMMTIMEOUTS, *LPCOMMTIMEOUTS;

typedef struct _COMSTAT {
    DWORD fCtsHold : 1;
    DWORD fDsrHold : 1;
    DWORD fRlsdHold : 1;
    DWORD fXoffHold : 1;
    DWORD fXoffSent : 1;
    DWORD fEof : 1;
    DWORD fTxim : 1;
    DWORD fReserved : 25;
    DWORD cbInQue;
    DWORD cbOutQue;
} COMSTAT, *LPCOMSTAT;

typedef struct _COMMPROP {
    WORD wPacketLength;
    WORD wPacketVersion;
    DWORD dwServiceMask;
    DWORD dwReserved1;
    DWORD dwMaxTxQueue;
    DWORD dwMaxRxQueue;
    DWORD dwMaxBaud;
    DWORD dwProvSubType;
    DWORD dwProvCapabilities;
    DWORD dwSettableParams;
    DWORD dwSettableBaud;
    WORD wSettableData;
    WORD wSettableStopParity;
    DWORD dwCurrentTxQueue;
    DWORD dwCurrentRxQueue;
    DWORD dwProvSpec1;
    DWORD dwProvSpec2;
    WCHAR wcProvChar[1];
} COMMPROP, *LPCOMMPROP;

#define DTR_CONTROL_DISABLE 0
#define DTR_CONTROL_ENABLE 1
#define DTR_CONTROL_HANDSHAKE 2
#define RTS_CONTROL_DISABLE 0
#define RTS_CONTROL_ENABLE 1
#define RTS_CONTROL_HANDSHAKE 2
#define RTS_CONTROL_TOGGLE 3
#define CE_RXOVER 0x0001
#define CE_OVERRUN 0x0002
#define CE_RXPARITY 0x0004
#define CE_FRAME 0x0008
#define CE_BREAK 0x0010

#define NOPARITY 0
#define ODDPARITY 1
#define EVENPARITY 2
#define MARKPARITY 3
#define SPACEPARITY 4
#define ONESTOPBIT 0
#define ONE5STOPBITS 1
#define TWOSTOPBITS 2

WINBASEAPI BOOL WINAPI BuildCommDCBA(LPCSTR definition, LPDCB dcb);
WINBASEAPI BOOL WINAPI BuildCommDCBW(LPCWSTR definition, LPDCB dcb);
WINBASEAPI BOOL WINAPI BuildCommDCBAndTimeoutsA(LPCSTR definition, LPDCB dcb, LPCOMMTIMEOUTS timeouts);
WINBASEAPI BOOL WINAPI BuildCommDCBAndTimeoutsW(LPCWSTR definition, LPDCB dcb, LPCOMMTIMEOUTS timeouts);
WINBASEAPI BOOL WINAPI GetCommState(HANDLE file, LPDCB dcb);
WINBASEAPI BOOL WINAPI SetCommState(HANDLE file, LPDCB dcb);
WINBASEAPI BOOL WINAPI GetCommTimeouts(HANDLE file, LPCOMMTIMEOUTS timeouts);
WINBASEAPI BOOL WINAPI SetCommTimeouts(HANDLE file, LPCOMMTIMEOUTS timeouts);
WINBASEAPI BOOL WINAPI ClearCommError(HANDLE file, LPDWORD errors, LPCOMSTAT stat);
WINBASEAPI BOOL WINAPI EscapeCommFunction(HANDLE file, DWORD function);
WINBASEAPI BOOL WINAPI SetCommBreak(HANDLE file);
WINBASEAPI BOOL WINAPI ClearCommBreak(HANDLE file);
WINBASEAPI BOOL WINAPI GetCommModemStatus(HANDLE file, LPDWORD status);
WINBASEAPI BOOL WINAPI PurgeComm(HANDLE file, DWORD flags);
WINBASEAPI BOOL WINAPI SetupComm(HANDLE file, DWORD in_queue, DWORD out_queue);
WINBASEAPI BOOL WINAPI GetCommMask(HANDLE file, LPDWORD mask);
WINBASEAPI BOOL WINAPI SetCommMask(HANDLE file, DWORD mask);
WINBASEAPI BOOL WINAPI TransmitCommChar(HANDLE file, char c);
WINBASEAPI BOOL WINAPI GetCommProperties(HANDLE file, LPCOMMPROP prop);
WINBASEAPI BOOL WINAPI WaitCommEvent(HANDLE file, LPDWORD mask, LPOVERLAPPED overlapped);
WINBASEAPI BOOL WINAPI TransactNamedPipe(HANDLE pipe, LPVOID in, DWORD in_size, LPVOID out, DWORD out_size,
                                         LPDWORD read, LPOVERLAPPED overlapped);
WINBASEAPI BOOL WINAPI CallNamedPipeW(LPCWSTR name, LPVOID in, DWORD in_size, LPVOID out, DWORD out_size, LPDWORD read,
                                      DWORD timeout);
WINBASEAPI BOOL WINAPI CallNamedPipeA(LPCSTR name, LPVOID in, DWORD in_size, LPVOID out, DWORD out_size, LPDWORD read,
                                      DWORD timeout);

#include <mime/win32/winnls.h>
#include <mime/win32/winactctx.h>

WINBASEAPI DWORD WINAPI GetPrivateProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR fallback, LPWSTR out,
                                                 DWORD size, LPCWSTR filename);
WINBASEAPI UINT WINAPI GetPrivateProfileIntW(LPCWSTR section, LPCWSTR key, INT fallback, LPCWSTR filename);
WINBASEAPI BOOL WINAPI WritePrivateProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR value, LPCWSTR filename);
WINBASEAPI BOOL WINAPI QuirkIsEnabled(UINT quirk);
WINBASEAPI HRESULT WINAPI QuirkIsEnabledForProcess(HANDLE process, UINT quirk, PBOOL enabled);
WINBASEAPI BOOL WINAPI IsApiSetImplemented(PCSTR contract);
WINBASEAPI FARPROC WINAPI GetProcAddressForCaller(HMODULE module, LPCSTR name, PVOID caller);
WINBASEAPI void WINAPI TerminateProcessOnMemoryExhaustion(SIZE_T failed_size);
WINBASEAPI DWORD WINAPI GetTempPath2A(DWORD size, LPSTR buffer);
WINBASEAPI BOOL WINAPI NeedCurrentDirectoryForExePathA(LPCSTR name);
WINBASEAPI UINT WINAPI GetSystemWow64Directory2W(LPWSTR buffer, UINT size, WORD machine);
WINBASEAPI DWORD WINAPI GetProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR fallback, LPWSTR out, DWORD size);
WINBASEAPI DWORD WINAPI GetPrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR fallback, LPSTR out, DWORD size,
                                                 LPCSTR filename);
WINBASEAPI UINT WINAPI GetPrivateProfileIntA(LPCSTR section, LPCSTR key, INT fallback, LPCSTR filename);
WINBASEAPI BOOL WINAPI WritePrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR value, LPCSTR filename);
WINBASEAPI DWORD WINAPI GetPrivateProfileSectionA(LPCSTR section, LPSTR out, DWORD size, LPCSTR filename);
WINBASEAPI DWORD WINAPI GetPrivateProfileSectionW(LPCWSTR section, LPWSTR out, DWORD size, LPCWSTR filename);
WINBASEAPI DWORD WINAPI GetPrivateProfileSectionNamesA(LPSTR out, DWORD size, LPCSTR filename);
WINBASEAPI DWORD WINAPI GetPrivateProfileSectionNamesW(LPWSTR out, DWORD size, LPCWSTR filename);
WINBASEAPI BOOL WINAPI WritePrivateProfileSectionA(LPCSTR section, LPCSTR lines, LPCSTR filename);
WINBASEAPI BOOL WINAPI WritePrivateProfileSectionW(LPCWSTR section, LPCWSTR lines, LPCWSTR filename);
WINBASEAPI BOOL WINAPI GetPrivateProfileStructA(LPCSTR section, LPCSTR key, LPVOID structure, UINT size, LPCSTR filename);
WINBASEAPI BOOL WINAPI GetPrivateProfileStructW(LPCWSTR section, LPCWSTR key, LPVOID structure, UINT size, LPCWSTR filename);
WINBASEAPI BOOL WINAPI WritePrivateProfileStructA(LPCSTR section, LPCSTR key, LPVOID structure, UINT size, LPCSTR filename);
WINBASEAPI BOOL WINAPI WritePrivateProfileStructW(LPCWSTR section, LPCWSTR key, LPVOID structure, UINT size, LPCWSTR filename);
WINBASEAPI DWORD WINAPI GetProfileStringA(LPCSTR section, LPCSTR key, LPCSTR fallback, LPSTR out, DWORD size);
WINBASEAPI UINT WINAPI GetProfileIntA(LPCSTR section, LPCSTR key, INT fallback);
WINBASEAPI BOOL WINAPI WriteProfileStringA(LPCSTR section, LPCSTR key, LPCSTR value);
WINBASEAPI DWORD WINAPI GetProfileSectionA(LPCSTR section, LPSTR out, DWORD size);
WINBASEAPI DWORD WINAPI GetProfileSectionW(LPCWSTR section, LPWSTR out, DWORD size);
WINBASEAPI BOOL WINAPI WriteProfileSectionA(LPCSTR section, LPCSTR lines);
WINBASEAPI BOOL WINAPI WriteProfileSectionW(LPCWSTR section, LPCWSTR lines);
WINBASEAPI UINT WINAPI GetProfileIntW(LPCWSTR section, LPCWSTR key, INT fallback);
WINBASEAPI BOOL WINAPI WriteProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR value);

#endif
