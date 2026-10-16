/*
 * user mode runtime exported by ntdll.dll: heaps, locks, the loader,
 * paths, environment, errors and exceptions.
 */
#ifndef MIME_NTRTL_H
#define MIME_NTRTL_H

#include <mime/nt.h>
#include <mime/ntapi.h>
#include <rtl.h>

#ifndef NTSYSAPI
#ifdef MIME_NTDLL
#define NTSYSAPI
#else
#define NTSYSAPI DECLSPEC_IMPORT
#endif
#endif

/* heap flags. */
#define HEAP_NO_SERIALIZE 0x00000001
#define HEAP_GROWABLE 0x00000002
#define HEAP_GENERATE_EXCEPTIONS 0x00000004
#define HEAP_ZERO_MEMORY 0x00000008
#define HEAP_REALLOC_IN_PLACE_ONLY 0x00000010
#define HEAP_CREATE_ENABLE_EXECUTE 0x00040000

/* dll entry reasons. */
#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1
#define DLL_THREAD_ATTACH 2
#define DLL_THREAD_DETACH 3

#define TLS_MINIMUM_AVAILABLE 64
#define TLS_EXPANSION_SLOTS 1024

typedef BOOLEAN(NTAPI *PDLL_INIT_ROUTINE)(PVOID base, ULONG reason, PCONTEXT context);
typedef void(NTAPI *PIMAGE_TLS_CALLBACK)(PVOID base, ULONG reason, PVOID reserved);
typedef LONG(NTAPI *PVECTORED_EXCEPTION_HANDLER)(PEXCEPTION_POINTERS info);

typedef struct _RTL_CRITICAL_SECTION_DEBUG {
    USHORT Type;
    USHORT CreatorBackTraceIndex;
    struct _RTL_CRITICAL_SECTION *CriticalSection;
    LIST_ENTRY ProcessLocksList;
    ULONG EntryCount;
    ULONG ContentionCount;
    ULONG Flags;
    USHORT CreatorBackTraceIndexHigh;
    USHORT Identifier;
} RTL_CRITICAL_SECTION_DEBUG, *PRTL_CRITICAL_SECTION_DEBUG;

typedef struct _RTL_CRITICAL_SECTION {
    PRTL_CRITICAL_SECTION_DEBUG DebugInfo;
    LONG LockCount;
    LONG RecursionCount;
    HANDLE OwningThread;
    HANDLE LockSemaphore;
    ULONG_PTR SpinCount;
} RTL_CRITICAL_SECTION, *PRTL_CRITICAL_SECTION;
MIME_STATIC_ASSERT(sizeof(RTL_CRITICAL_SECTION) == (sizeof(void *) == 4 ? 24 : 40), "RTL_CRITICAL_SECTION");

/* a free critical section, usable as a static initializer. */
#define RTL_CRITICAL_SECTION_INIT {NULL, -1, 0, NULL, NULL, 0}

typedef struct _RTL_SRWLOCK {
    PVOID Ptr;
} RTL_SRWLOCK, *PRTL_SRWLOCK;

typedef struct _RTL_CONDITION_VARIABLE {
    PVOID Ptr;
} RTL_CONDITION_VARIABLE, *PRTL_CONDITION_VARIABLE;

typedef union _RTL_RUN_ONCE {
    PVOID Ptr;
} RTL_RUN_ONCE, *PRTL_RUN_ONCE;

typedef ULONG(NTAPI *PRTL_RUN_ONCE_INIT_FN)(PRTL_RUN_ONCE once, PVOID parameter, PVOID *context);

typedef struct _SLIST_ENTRY {
    struct _SLIST_ENTRY *Next;
} SLIST_ENTRY, *PSLIST_ENTRY;

#ifdef __i386__
typedef union DECLSPEC_ALIGN(8) _SLIST_HEADER {
    ULONGLONG Whole;
    struct {
        PSLIST_ENTRY Next;
        ULONG DepthSequence;
    } S;
} SLIST_HEADER, *PSLIST_HEADER;
#else
/* 16 byte header: next pointer and a depth and sequence word, updated with casp. */
typedef union DECLSPEC_ALIGN(16) _SLIST_HEADER {
    struct {
        ULONGLONG Alignment;
        ULONGLONG Region;
    };
    struct {
        PSLIST_ENTRY Next;
        ULONGLONG DepthSequence; /* depth in bits 0..15, sequence above */
    } S;
    unsigned __int128 Whole;
} SLIST_HEADER, *PSLIST_HEADER;

#endif

typedef struct _RTL_OSVERSIONINFOEXW {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
    USHORT wServicePackMajor;
    USHORT wServicePackMinor;
    USHORT wSuiteMask;
    UCHAR wProductType;
    UCHAR wReserved;
} RTL_OSVERSIONINFOEXW, *PRTL_OSVERSIONINFOEXW;

typedef struct _TIME_FIELDS {
    SHORT Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday;
} TIME_FIELDS, *PTIME_FIELDS;

typedef enum _RTL_PATH_TYPE {
    RtlPathTypeUnknown,
    RtlPathTypeUncAbsolute,     /* \\server\share */
    RtlPathTypeDriveAbsolute,   /* C:\x */
    RtlPathTypeDriveRelative,   /* C:x */
    RtlPathTypeRooted,          /* \x */
    RtlPathTypeRelative,        /* x */
    RtlPathTypeLocalDevice,     /* \\.\x or \\?\x */
    RtlPathTypeRootLocalDevice, /* \\. or \\? */
} RTL_PATH_TYPE;

typedef struct _LDR_RESOURCE_INFO {
    ULONG_PTR Type;
    ULONG_PTR Name;
    ULONG_PTR Language;
} LDR_RESOURCE_INFO, *PLDR_RESOURCE_INFO;

NTSYSAPI NTSTATUS NTAPI RtlQueryEnvironmentVariable(PWSTR env, PCWSTR name, SIZE_T name_chars, PWSTR value, SIZE_T value_chars, PSIZE_T return_chars);
NTSYSAPI NTSTATUS NTAPI RtlQueryEnvironmentVariable_U(PWSTR env, PCUNICODE_STRING name, PUNICODE_STRING value);
NTSYSAPI NTSTATUS NTAPI RtlCreateEnvironment(BOOLEAN clone, PWSTR *out);
NTSYSAPI NTSTATUS NTAPI RtlDestroyEnvironment(PWSTR env);
NTSYSAPI NTSTATUS NTAPI RtlSetEnvironmentVariable(PWSTR *env_ptr, PCUNICODE_STRING name, PCUNICODE_STRING value);
NTSYSAPI NTSTATUS NTAPI RtlSetCurrentEnvironment(PWSTR env, PWSTR *old);
NTSYSAPI NTSTATUS NTAPI RtlExpandEnvironmentStrings(PWSTR env, PCWSTR src, SIZE_T src_chars, PWSTR dst, SIZE_T dst_chars, PSIZE_T return_chars);
NTSYSAPI NTSTATUS NTAPI RtlExpandEnvironmentStrings_U(PWSTR env, PCUNICODE_STRING src, PUNICODE_STRING dst, PULONG return_bytes);
NTSYSAPI PVOID NTAPI RtlAddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER handler);
NTSYSAPI ULONG NTAPI RtlRemoveVectoredExceptionHandler(PVOID handle);
NTSYSAPI PVOID NTAPI RtlAddVectoredContinueHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER handler);
NTSYSAPI ULONG NTAPI RtlRemoveVectoredContinueHandler(PVOID handle);
NTSYSAPI void NTAPI RtlSetUnhandledExceptionFilter(PVOID filter);
NTSYSAPI BOOLEAN NTAPI RtlDispatchException(PEXCEPTION_RECORD record, PCONTEXT context);
NTSYSAPI void NTAPI RtlRaiseException(PEXCEPTION_RECORD record);
NTSYSAPI void NTAPI RtlCaptureContext(PCONTEXT context);
NTSYSAPI DECLSPEC_NORETURN void NTAPI RtlRaiseStatus(NTSTATUS status);
NTSYSAPI void NTAPI RtlUnwind(PVOID target_frame, PVOID target_ip, PEXCEPTION_RECORD record, PVOID value);
NTSYSAPI void NTAPI RtlUnwindEx(PVOID target_frame, PVOID target_ip, PEXCEPTION_RECORD record, PVOID value, PCONTEXT context, PVOID history);
NTSYSAPI PVOID NTAPI RtlLookupFunctionEntry(ULONG64 pc, PULONG64 image_base, PVOID history);
NTSYSAPI PVOID NTAPI RtlCreateHeap(ULONG flags, PVOID base, SIZE_T reserve, SIZE_T commit, PVOID lock, PVOID parameters);
NTSYSAPI PVOID NTAPI RtlDestroyHeap(PVOID handle);
NTSYSAPI PVOID NTAPI RtlAllocateHeap(PVOID handle, ULONG flags, SIZE_T size);
NTSYSAPI BOOLEAN NTAPI RtlFreeHeap(PVOID handle, ULONG flags, PVOID p);
NTSYSAPI SIZE_T NTAPI RtlSizeHeap(PVOID handle, ULONG flags, const void *p);
NTSYSAPI PVOID NTAPI RtlReAllocateHeap(PVOID handle, ULONG flags, PVOID p, SIZE_T size);
NTSYSAPI BOOLEAN NTAPI RtlLockHeap(PVOID handle);
NTSYSAPI BOOLEAN NTAPI RtlUnlockHeap(PVOID handle);
NTSYSAPI BOOLEAN NTAPI RtlValidateHeap(PVOID handle, ULONG flags, PVOID p);
NTSYSAPI SIZE_T NTAPI RtlCompactHeap(PVOID handle, ULONG flags);
NTSYSAPI ULONG NTAPI RtlGetProcessHeaps(ULONG count, PVOID *heaps);
NTSYSAPI NTSTATUS NTAPI RtlSetHeapInformation(PVOID heap, ULONG klass, PVOID info, SIZE_T length);
NTSYSAPI NTSTATUS NTAPI RtlQueryHeapInformation(PVOID heap, ULONG klass, PVOID info, SIZE_T length, PSIZE_T returned);
NTSYSAPI NTSTATUS NTAPI LdrFindEntryForAddress(PVOID address, PLDR_DATA_TABLE_ENTRY *entry);
NTSYSAPI PVOID NTAPI RtlPcToFileHeader(PVOID pc, PVOID *base);
NTSYSAPI void NTAPI LdrShutdownThread(void);
NTSYSAPI void NTAPI LdrShutdownProcess(void);
NTSYSAPI NTSTATUS NTAPI LdrLoadDll(PCWSTR search_path, PULONG flags, PCUNICODE_STRING name, PVOID *base);
NTSYSAPI NTSTATUS NTAPI LdrUnloadDll(PVOID base);
NTSYSAPI NTSTATUS NTAPI LdrGetDllHandleEx(ULONG flags, PCWSTR path, PULONG characteristics, PCUNICODE_STRING name, PVOID *base);
NTSYSAPI NTSTATUS NTAPI LdrGetDllHandle(PCWSTR path, PULONG characteristics, PCUNICODE_STRING name, PVOID *base);
NTSYSAPI NTSTATUS NTAPI LdrAddRefDll(ULONG flags, PVOID base);
NTSYSAPI NTSTATUS NTAPI LdrGetProcedureAddressEx(PVOID base, PCANSI_STRING name, ULONG ordinal, PVOID *address, ULONG flags);
NTSYSAPI NTSTATUS NTAPI LdrGetProcedureAddress(PVOID base, PCANSI_STRING name, ULONG ordinal, PVOID *address);
NTSYSAPI NTSTATUS NTAPI LdrDisableThreadCalloutsForDll(PVOID base);
NTSYSAPI NTSTATUS NTAPI LdrLockLoaderLock(ULONG flags, PULONG disposition, PULONG_PTR cookie);
NTSYSAPI NTSTATUS NTAPI LdrUnlockLoaderLock(ULONG flags, ULONG_PTR cookie);
NTSYSAPI NTSTATUS NTAPI LdrQueryImageFileExecutionOptions(PUNICODE_STRING key, PCWSTR value, ULONG type, PVOID buffer, ULONG length, PULONG returned);
NTSYSAPI NTSTATUS NTAPI LdrGetDllFullName(PVOID base, PUNICODE_STRING name);
NTSYSAPI DECLSPEC_NORETURN void NTAPI RtlUserThreadStart(PVOID entry, PVOID parameter);
NTSYSAPI DECLSPEC_NORETURN void NTAPI RtlExitUserThread(NTSTATUS status);
NTSYSAPI DECLSPEC_NORETURN void NTAPI RtlExitUserProcess(NTSTATUS status);
NTSYSAPI void NTAPI RtlAcquirePebLock(void);
NTSYSAPI void NTAPI RtlReleasePebLock(void);
NTSYSAPI PPEB NTAPI RtlGetCurrentPeb(void);
NTSYSAPI ULONG NTAPI RtlNtStatusToDosErrorNoTeb(NTSTATUS status);
NTSYSAPI ULONG NTAPI RtlNtStatusToDosError(NTSTATUS status);
NTSYSAPI NTSTATUS NTAPI RtlGetLastNtStatus(void);
NTSYSAPI ULONG NTAPI RtlGetLastWin32Error(void);
NTSYSAPI void NTAPI RtlSetLastWin32Error(ULONG error);
NTSYSAPI void NTAPI RtlRestoreLastWin32Error(ULONG error);
NTSYSAPI void NTAPI RtlSetLastWin32ErrorAndNtStatusFromNtStatus(NTSTATUS status);
NTSYSAPI ULONG vDbgPrintEx(ULONG component, ULONG level, PCSTR format, va_list ap);
NTSYSAPI ULONG DbgPrintEx(ULONG component, ULONG level, PCSTR format, ...);
NTSYSAPI ULONG DbgPrint(PCSTR format, ...);
NTSYSAPI void NTAPI DbgBreakPoint(void);
NTSYSAPI void NTAPI DbgUserBreakPoint(void);
NTSYSAPI NTSTATUS NTAPI RtlGetVersion(RTL_OSVERSIONINFOEXW *info);
NTSYSAPI NTSTATUS NTAPI RtlQuerySystemTime(PLARGE_INTEGER time);
NTSYSAPI ULONGLONG NTAPI VerSetConditionMask(ULONGLONG mask, ULONG type, UCHAR condition);
NTSYSAPI void NTAPI RtlGetNtVersionNumbers(PULONG major, PULONG minor, PULONG build);
NTSYSAPI BOOLEAN NTAPI RtlGetNtProductType(ULONG *type);
NTSYSAPI PCWSTR NTAPI RtlGetNtSystemRoot(void);
NTSYSAPI BOOLEAN NTAPI RtlIsProcessorFeaturePresent(ULONG feature);
NTSYSAPI BOOLEAN NTAPI RtlQueryPerformanceCounter(PLARGE_INTEGER counter);
NTSYSAPI BOOLEAN NTAPI RtlQueryPerformanceFrequency(PLARGE_INTEGER frequency);
NTSYSAPI LONGLONG NTAPI RtlGetSystemTimePrecise(void);
NTSYSAPI BOOLEAN NTAPI RtlTimeToSecondsSince1970(const LARGE_INTEGER *time, PULONG seconds);
NTSYSAPI void NTAPI RtlSecondsSince1970ToTime(ULONG seconds, PLARGE_INTEGER time);
NTSYSAPI BOOLEAN NTAPI RtlTimeToSecondsSince1980(const LARGE_INTEGER *time, PULONG seconds);
NTSYSAPI void NTAPI RtlSecondsSince1980ToTime(ULONG seconds, PLARGE_INTEGER time);
NTSYSAPI NTSTATUS NTAPI RtlSystemTimeToLocalTime(const LARGE_INTEGER *system, PLARGE_INTEGER local);
NTSYSAPI NTSTATUS NTAPI RtlLocalTimeToSystemTime(const LARGE_INTEGER *local, PLARGE_INTEGER system);
NTSYSAPI BOOLEAN NTAPI RtlTimeFieldsToTime(PTIME_FIELDS tf, PLARGE_INTEGER time);
NTSYSAPI PVOID NTAPI RtlEncodePointer(PVOID p);
NTSYSAPI PVOID NTAPI RtlDecodePointer(PVOID p);
NTSYSAPI PVOID NTAPI RtlEncodeSystemPointer(PVOID p);
NTSYSAPI PVOID NTAPI RtlDecodeSystemPointer(PVOID p);
NTSYSAPI SIZE_T NTAPI RtlCompareMemory(const void *a, const void *b, SIZE_T length);
NTSYSAPI SIZE_T NTAPI RtlCompareMemoryUlong(const void *source, SIZE_T length, ULONG pattern);
NTSYSAPI void NTAPI RtlFillMemory(void *dst, SIZE_T length, UCHAR fill);
NTSYSAPI void NTAPI RtlZeroMemory(void *dst, SIZE_T length);
NTSYSAPI void NTAPI RtlMoveMemory(void *dst, const void *src, SIZE_T length);
NTSYSAPI void NTAPI RtlCopyMemory(void *dst, const void *src, SIZE_T length);
NTSYSAPI ULONG NTAPI RtlComputeCrc32(ULONG initial, const UCHAR *data, ULONG length);
NTSYSAPI ULONG NTAPI RtlRandomEx(PULONG seed);
NTSYSAPI ULONG NTAPI RtlRandom(PULONG seed);
NTSYSAPI ULONG NTAPI RtlUniform(PULONG seed);
NTSYSAPI USHORT NTAPI RtlCaptureStackBackTrace(ULONG skip, ULONG count, PVOID *frames, PULONG hash);
NTSYSAPI RTL_PATH_TYPE NTAPI RtlDetermineDosPathNameType_U(PCWSTR path);
NTSYSAPI ULONG NTAPI RtlGetFullPathName_U(PCWSTR name, ULONG size, PWSTR buffer, PWSTR *file_part);
NTSYSAPI BOOLEAN NTAPI RtlDosPathNameToNtPathName_U(PCWSTR dos, PUNICODE_STRING nt, PCWSTR *file_part, PVOID relative);
NTSYSAPI NTSTATUS NTAPI RtlDosPathNameToNtPathName_U_WithStatus(PCWSTR dos, PUNICODE_STRING nt, PCWSTR *file_part, PVOID relative);
NTSYSAPI BOOLEAN NTAPI RtlDosPathNameToRelativeNtPathName_U(PCWSTR dos, PUNICODE_STRING nt, PCWSTR *file_part, PVOID relative);
NTSYSAPI void NTAPI RtlReleaseRelativeName(PVOID relative);
NTSYSAPI ULONG NTAPI RtlGetCurrentDirectory_U(ULONG size, PWSTR buffer);
NTSYSAPI NTSTATUS NTAPI RtlSetCurrentDirectory_U(PCUNICODE_STRING dir);
NTSYSAPI BOOLEAN NTAPI RtlDoesFileExists_U(PCWSTR name);
NTSYSAPI ULONG NTAPI RtlDosSearchPath_U(PCWSTR path, PCWSTR name, PCWSTR extension, ULONG size, PWSTR buffer, PWSTR *file_part);
NTSYSAPI ULONG NTAPI RtlIsDosDeviceName_U(PCWSTR name);
NTSYSAPI NTSTATUS NTAPI LdrFindResource_U(PVOID handle, PLDR_RESOURCE_INFO info, ULONG level, IMAGE_RESOURCE_DATA_ENTRY **entry);
NTSYSAPI NTSTATUS NTAPI LdrFindResourceDirectory_U(PVOID handle, PLDR_RESOURCE_INFO info, ULONG level, IMAGE_RESOURCE_DIRECTORY **dir);
NTSYSAPI NTSTATUS NTAPI LdrAccessResource(PVOID handle, const IMAGE_RESOURCE_DATA_ENTRY *entry, PVOID *data, PULONG size);
NTSYSAPI NTSTATUS NTAPI RtlInitializeCriticalSectionEx(PRTL_CRITICAL_SECTION cs, ULONG spin, ULONG flags);
NTSYSAPI NTSTATUS NTAPI RtlInitializeCriticalSectionAndSpinCount(PRTL_CRITICAL_SECTION cs, ULONG spin);
NTSYSAPI NTSTATUS NTAPI RtlInitializeCriticalSection(PRTL_CRITICAL_SECTION cs);
NTSYSAPI ULONG NTAPI RtlSetCriticalSectionSpinCount(PRTL_CRITICAL_SECTION cs, ULONG spin);
NTSYSAPI NTSTATUS NTAPI RtlDeleteCriticalSection(PRTL_CRITICAL_SECTION cs);
NTSYSAPI NTSTATUS NTAPI RtlEnterCriticalSection(PRTL_CRITICAL_SECTION cs);
NTSYSAPI BOOLEAN NTAPI RtlTryEnterCriticalSection(PRTL_CRITICAL_SECTION cs);
NTSYSAPI NTSTATUS NTAPI RtlLeaveCriticalSection(PRTL_CRITICAL_SECTION cs);
NTSYSAPI ULONG NTAPI RtlIsCriticalSectionLocked(PRTL_CRITICAL_SECTION cs);
NTSYSAPI ULONG NTAPI RtlIsCriticalSectionLockedByThread(PRTL_CRITICAL_SECTION cs);
NTSYSAPI void NTAPI RtlInitializeSRWLock(PRTL_SRWLOCK lock);
NTSYSAPI BOOLEAN NTAPI RtlTryAcquireSRWLockExclusive(PRTL_SRWLOCK lock);
NTSYSAPI void NTAPI RtlAcquireSRWLockExclusive(PRTL_SRWLOCK lock);
NTSYSAPI void NTAPI RtlReleaseSRWLockExclusive(PRTL_SRWLOCK lock);
NTSYSAPI BOOLEAN NTAPI RtlTryAcquireSRWLockShared(PRTL_SRWLOCK lock);
NTSYSAPI void NTAPI RtlAcquireSRWLockShared(PRTL_SRWLOCK lock);
NTSYSAPI void NTAPI RtlReleaseSRWLockShared(PRTL_SRWLOCK lock);
NTSYSAPI void NTAPI RtlInitializeConditionVariable(PRTL_CONDITION_VARIABLE cv);
NTSYSAPI void NTAPI RtlWakeConditionVariable(PRTL_CONDITION_VARIABLE cv);
NTSYSAPI void NTAPI RtlWakeAllConditionVariable(PRTL_CONDITION_VARIABLE cv);
NTSYSAPI NTSTATUS NTAPI RtlSleepConditionVariableCS(PRTL_CONDITION_VARIABLE cv, PRTL_CRITICAL_SECTION cs, const LARGE_INTEGER *timeout);
NTSYSAPI NTSTATUS NTAPI RtlSleepConditionVariableSRW(PRTL_CONDITION_VARIABLE cv, PRTL_SRWLOCK lock, const LARGE_INTEGER *timeout, ULONG flags);
NTSYSAPI void NTAPI RtlRunOnceInitialize(PRTL_RUN_ONCE once);
NTSYSAPI NTSTATUS NTAPI RtlRunOnceBeginInitialize(PRTL_RUN_ONCE once, ULONG flags, PVOID *context);
NTSYSAPI NTSTATUS NTAPI RtlRunOnceComplete(PRTL_RUN_ONCE once, ULONG flags, PVOID context);
NTSYSAPI NTSTATUS NTAPI RtlRunOnceExecuteOnce(PRTL_RUN_ONCE once, PRTL_RUN_ONCE_INIT_FN fn, PVOID parameter, PVOID *context);
NTSYSAPI NTSTATUS NTAPI RtlWaitOnAddress(const void *address, const void *compare, SIZE_T size, const LARGE_INTEGER *timeout);
NTSYSAPI void NTAPI RtlWakeAddressSingle(const void *address);
NTSYSAPI void NTAPI RtlWakeAddressAll(const void *address);
NTSYSAPI void NTAPI RtlInitializeSListHead(PSLIST_HEADER head);
NTSYSAPI PSLIST_ENTRY NTAPI RtlInterlockedPushEntrySList(PSLIST_HEADER head, PSLIST_ENTRY entry);
NTSYSAPI PSLIST_ENTRY NTAPI RtlInterlockedPopEntrySList(PSLIST_HEADER head);
NTSYSAPI PSLIST_ENTRY NTAPI RtlInterlockedFlushSList(PSLIST_HEADER head);
NTSYSAPI USHORT NTAPI RtlQueryDepthSList(PSLIST_HEADER head);
NTSYSAPI PSLIST_ENTRY NTAPI RtlFirstEntrySList(const SLIST_HEADER *head);

#endif
