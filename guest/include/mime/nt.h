/*
 * core nt definitions shared by the kernel and user mode. layouts that user mode or drivers can see follow
 * 64-bit windows, the same on x64 and arm64, and their offsets are asserted.
 */
#ifndef MIME_NT_H
#define MIME_NT_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define MIME_STATIC_ASSERT(e, m) static_assert(e, m)
#define MIME_FIELD_AT(type, field, off) MIME_STATIC_ASSERT(offsetof(type, field) == (off), #type "." #field)

#ifdef __i386__
#define NTAPI __stdcall
#define WINAPI __stdcall
#define CALLBACK __stdcall
#define APIENTRY __stdcall
#define FASTCALL __fastcall
#else
#define NTAPI
#define WINAPI
#define CALLBACK
#define APIENTRY
#define FASTCALL
#endif
#define CDECL __cdecl
#define DECLSPEC_IMPORT __declspec(dllimport)
#ifdef __i386__
#define DECLSPEC_EXPORT
#else
#define DECLSPEC_EXPORT __declspec(dllexport)
#endif
#define DECLSPEC_NORETURN __declspec(noreturn)
#define DECLSPEC_ALIGN(x) __declspec(align(x))
#define FORCEINLINE [[gnu::always_inline]] static inline
#define UNALIGNED
#define CONST const
#define IN
#define OUT
#define OPTIONAL
#define UNREFERENCED_PARAMETER(p) ((void)(p))

#ifndef NULL
#define NULL ((void *)0)
#endif
#define TRUE 1
#define FALSE 0

typedef void VOID, *PVOID, *LPVOID;
typedef const void *PCVOID, *LPCVOID;
typedef char CHAR, *PCHAR, *PSTR, *LPSTR;
typedef const char *PCSTR, *LPCSTR, *PCCH;
typedef unsigned char UCHAR, *PUCHAR, BYTE, *PBYTE, *LPBYTE, BOOLEAN, *PBOOLEAN;
typedef signed char SCHAR, CCHAR;
typedef short SHORT, *PSHORT, CSHORT;
typedef unsigned short USHORT, *PUSHORT, WORD, *PWORD, *LPWORD, ATOM;
typedef wchar_t WCHAR, *PWCHAR, *PWSTR, *LPWSTR;
typedef const wchar_t *PCWSTR, *LPCWSTR, *PCWCH;
typedef wchar_t *PWCH;
typedef int INT, *PINT, *LPINT, BOOL, *PBOOL, *LPBOOL;
typedef unsigned int UINT, *PUINT, *LPUINT;
typedef long LONG, *PLONG, *LPLONG;
typedef unsigned long ULONG, *PULONG, DWORD, *PDWORD, *LPDWORD;
typedef long long LONGLONG, *PLONGLONG, LONG64, INT64;
typedef unsigned long long ULONGLONG, *PULONGLONG, ULONG64, *PULONG64, DWORD64, *PDWORD64, UINT64, DWORDLONG;
typedef intptr_t LONG_PTR, *PLONG_PTR, INT_PTR, SSIZE_T, *PSSIZE_T, LPARAM, LRESULT;
typedef uintptr_t ULONG_PTR, *PULONG_PTR, UINT_PTR, SIZE_T, *PSIZE_T, DWORD_PTR, WPARAM, KAFFINITY;
typedef float FLOAT;
typedef double DOUBLE;
typedef LONG NTSTATUS, *PNTSTATUS;
typedef LONG KPRIORITY;
typedef UCHAR KIRQL, *PKIRQL;
typedef CCHAR KPROCESSOR_MODE;
typedef ULONG ACCESS_MASK, *PACCESS_MASK;
typedef ULONG LOGICAL;
typedef void *HANDLE, **PHANDLE;
typedef HANDLE HMODULE, HINSTANCE;
typedef ULONG_PTR KSPIN_LOCK, *PKSPIN_LOCK;
typedef LONGLONG USN;
typedef ULONG LCID;
typedef USHORT LANGID;

typedef union _LARGE_INTEGER {
    struct {
        ULONG LowPart;
        LONG HighPart;
    };
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef struct _LUID {
    ULONG LowPart;
    LONG HighPart;
} LUID, *PLUID;

typedef union _ULARGE_INTEGER {
    struct {
        ULONG LowPart;
        ULONG HighPart;
    };
    ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

typedef struct _GUID {
    ULONG Data1;
    USHORT Data2;
    USHORT Data3;
    UCHAR Data4[8];
} GUID, *PGUID;

#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define NT_ERROR(s) ((ULONG)(s) >> 30 == 3)

#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#define STATUS_WAIT_0 ((NTSTATUS)0x00000000L)
#define STATUS_WAIT_1 ((NTSTATUS)0x00000001L)
#define STATUS_ABANDONED ((NTSTATUS)0x00000080L)
#define STATUS_USER_APC ((NTSTATUS)0x000000C0L)
#define STATUS_KERNEL_APC ((NTSTATUS)0x00000100L)
#define STATUS_ALERTED ((NTSTATUS)0x00000101L)
#define STATUS_TIMEOUT ((NTSTATUS)0x00000102L)
#define STATUS_NO_YIELD_PERFORMED ((NTSTATUS)0x40000024L)
#define STATUS_PENDING ((NTSTATUS)0x00000103L)
#define STATUS_REPARSE ((NTSTATUS)0x00000104L)
#define STATUS_MORE_ENTRIES ((NTSTATUS)0x00000105L)
#define STATUS_NOT_ALL_ASSIGNED ((NTSTATUS)0x00000106L)
#define STATUS_SOME_NOT_MAPPED ((NTSTATUS)0x00000107L)
#define STATUS_PROCESS_NOT_IN_JOB ((NTSTATUS)0x00000123L)
#define STATUS_PROCESS_IN_JOB ((NTSTATUS)0x00000124L)
#define STATUS_IMAGE_NOT_AT_BASE ((NTSTATUS)0x40000003L)
#define STATUS_OBJECT_NAME_EXISTS ((NTSTATUS)0x40000000L)
#define STATUS_BUFFER_OVERFLOW ((NTSTATUS)0x80000005L)
#define STATUS_NO_MORE_FILES ((NTSTATUS)0x80000006L)
#define STATUS_NO_MORE_ENTRIES ((NTSTATUS)0x8000001AL)
#define STATUS_GUARD_PAGE_VIOLATION ((NTSTATUS)0x80000001L)
#define STATUS_DATATYPE_MISALIGNMENT ((NTSTATUS)0x80000002L)
#define STATUS_BREAKPOINT ((NTSTATUS)0x80000003L)
#define STATUS_SINGLE_STEP ((NTSTATUS)0x80000004L)
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#define STATUS_NOT_IMPLEMENTED ((NTSTATUS)0xC0000002L)
#define STATUS_INVALID_INFO_CLASS ((NTSTATUS)0xC0000003L)
#define STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS)0xC0000004L)
#define STATUS_ACCESS_VIOLATION ((NTSTATUS)0xC0000005L)
#define STATUS_IN_PAGE_ERROR ((NTSTATUS)0xC0000006L)
#define STATUS_INVALID_HANDLE ((NTSTATUS)0xC0000008L)
#define STATUS_INVALID_CID ((NTSTATUS)0xC000000BL)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#define STATUS_NO_SUCH_DEVICE ((NTSTATUS)0xC000000EL)
#define STATUS_NO_SUCH_FILE ((NTSTATUS)0xC000000FL)
#define STATUS_INVALID_DEVICE_REQUEST ((NTSTATUS)0xC0000010L)
#define STATUS_END_OF_FILE ((NTSTATUS)0xC0000011L)
#define STATUS_NO_MEMORY ((NTSTATUS)0xC0000017L)
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xC00000A3L)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xC0000184L)
#define STATUS_IO_DEVICE_ERROR ((NTSTATUS)0xC0000185L)
#define STATUS_DEVICE_REMOVED ((NTSTATUS)0xC00002B6L)
#define STATUS_DEVICE_NOT_CONNECTED ((NTSTATUS)0xC000009DL)
#define STATUS_INVALID_BUFFER_SIZE ((NTSTATUS)0xC0000206L)
#define STATUS_INVALID_USER_BUFFER ((NTSTATUS)0xC00000E8L)
#define STATUS_CANCELLED ((NTSTATUS)0xC0000120L)
#define STATUS_DATA_ERROR ((NTSTATUS)0xC000003EL)
#define STATUS_INVALID_PARAMETER_MIX ((NTSTATUS)0xC0000030L)
#define STATUS_NO_DATA_DETECTED ((NTSTATUS)0x80000022L)
#define STATUS_CONFLICTING_ADDRESSES ((NTSTATUS)0xC0000018L)
#define STATUS_NOT_MAPPED_VIEW ((NTSTATUS)0xC0000019L)
#define STATUS_UNABLE_TO_FREE_VM ((NTSTATUS)0xC000001AL)
#define STATUS_ILLEGAL_INSTRUCTION ((NTSTATUS)0xC000001DL)
#define STATUS_INVALID_SYSTEM_SERVICE ((NTSTATUS)0xC000001CL)
#define STATUS_INVALID_VIEW_SIZE ((NTSTATUS)0xC000001FL)
#define STATUS_PORT_ALREADY_SET ((NTSTATUS)0xC0000048L)
#define STATUS_ALREADY_COMMITTED ((NTSTATUS)0xC0000021L)
#define STATUS_ACCESS_DENIED ((NTSTATUS)0xC0000022L)
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#define STATUS_OBJECT_TYPE_MISMATCH ((NTSTATUS)0xC0000024L)
#define STATUS_NONCONTINUABLE_EXCEPTION ((NTSTATUS)0xC0000025L)
#define STATUS_INVALID_DISPOSITION ((NTSTATUS)0xC0000026L)
#define STATUS_OBJECT_NAME_INVALID ((NTSTATUS)0xC0000033L)
#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xC0000034L)
#define STATUS_OBJECT_NAME_COLLISION ((NTSTATUS)0xC0000035L)
#define STATUS_PORT_DISCONNECTED ((NTSTATUS)0xC0000037L)
#define STATUS_OBJECT_PATH_INVALID ((NTSTATUS)0xC0000039L)
#define STATUS_OBJECT_PATH_NOT_FOUND ((NTSTATUS)0xC000003AL)
#define STATUS_OBJECT_PATH_SYNTAX_BAD ((NTSTATUS)0xC000003BL)
#define STATUS_SHARING_VIOLATION ((NTSTATUS)0xC0000043L)
#define STATUS_SECTION_NOT_IMAGE ((NTSTATUS)0xC0000049L)
#define STATUS_THREAD_IS_TERMINATING ((NTSTATUS)0xC000004BL)
#define STATUS_INVALID_PAGE_PROTECTION ((NTSTATUS)0xC0000045L)
#define STATUS_MUTANT_NOT_OWNED ((NTSTATUS)0xC0000046L)
#define STATUS_SEMAPHORE_LIMIT_EXCEEDED ((NTSTATUS)0xC0000047L)
#define STATUS_DELETE_PENDING ((NTSTATUS)0xC0000056L)
#define STATUS_PRIVILEGE_NOT_HELD ((NTSTATUS)0xC0000061L)
#define STATUS_INVALID_IMAGE_FORMAT ((NTSTATUS)0xC000007BL)
#define STATUS_PROCEDURE_NOT_FOUND ((NTSTATUS)0xC000007AL)
#define STATUS_MEMORY_NOT_ALLOCATED ((NTSTATUS)0xC00000A0L)
#define STATUS_FREE_VM_NOT_AT_BASE ((NTSTATUS)0xC000009FL)
#define STATUS_NOT_COMMITTED ((NTSTATUS)0xC000002DL)
#define STATUS_PARTIAL_COPY ((NTSTATUS)0x8000000DL)
#define STATUS_DISK_FULL ((NTSTATUS)0xC000007FL)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009AL)
#define STATUS_FILE_IS_A_DIRECTORY ((NTSTATUS)0xC00000BAL)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BBL)
#define STATUS_CHILD_PROCESS_BLOCKED ((NTSTATUS)0xC000049DL)
#define STATUS_QUOTA_EXCEEDED ((NTSTATUS)0xC0000044L)
#define STATUS_INTERNAL_ERROR ((NTSTATUS)0xC00000E5L)
#define STATUS_DIRECTORY_NOT_EMPTY ((NTSTATUS)0xC0000101L)
#define STATUS_NAME_TOO_LONG ((NTSTATUS)0xC0000106L)
#define STATUS_NOT_A_DIRECTORY ((NTSTATUS)0xC0000103L)
#define STATUS_NOT_SAME_DEVICE ((NTSTATUS)0xC00000D4L)
#define STATUS_TOO_MANY_LINKS ((NTSTATUS)0xC0000265L)
#define STATUS_PROCESS_IS_TERMINATING ((NTSTATUS)0xC000010AL)
#define STATUS_TIMER_RESOLUTION_NOT_SET ((NTSTATUS)0xC0000245L)
#define STATUS_INVALID_IMAGE_NOT_MZ ((NTSTATUS)0xC000012FL)
#define STATUS_DLL_NOT_FOUND ((NTSTATUS)0xC0000135L)
#define STATUS_ENTRYPOINT_NOT_FOUND ((NTSTATUS)0xC0000139L)
#define STATUS_ORDINAL_NOT_FOUND ((NTSTATUS)0xC0000138L)
#define STATUS_CONTROL_C_EXIT ((NTSTATUS)0xC000013AL)
#define STATUS_DLL_INIT_FAILED ((NTSTATUS)0xC0000142L)
#define STATUS_NO_TOKEN ((NTSTATUS)0xC000007CL)
#define STATUS_INVALID_OWNER ((NTSTATUS)0xC000005AL)
#define STATUS_INVALID_PRIMARY_GROUP ((NTSTATUS)0xC000005BL)
#define STATUS_NO_SUCH_LOGON_SESSION ((NTSTATUS)0xC000005FL)
#define STATUS_LOGON_FAILURE ((NTSTATUS)0xC000006DL)
#define STATUS_INVALID_SID ((NTSTATUS)0xC0000078L)
#define STATUS_ALLOTTED_SPACE_EXCEEDED ((NTSTATUS)0xC0000099L)
#define STATUS_BAD_IMPERSONATION_LEVEL ((NTSTATUS)0xC00000A5L)
#define STATUS_CANT_OPEN_ANONYMOUS ((NTSTATUS)0xC00000A6L)
#define STATUS_BAD_TOKEN_TYPE ((NTSTATUS)0xC00000A8L)
#define STATUS_CANNOT_IMPERSONATE ((NTSTATUS)0xC000010DL)
#define STATUS_TOKEN_ALREADY_IN_USE ((NTSTATUS)0xC000012BL)
#define STATUS_INVALID_LABEL ((NTSTATUS)0xC0000446L)
#define STATUS_SYSTEM_SHUTDOWN ((NTSTATUS)0xC00002EBL)
#define STATUS_CALLBACK_POP_STACK ((NTSTATUS)0xC0000423L)
#define STATUS_STACK_OVERFLOW ((NTSTATUS)0xC00000FDL)
#define STATUS_INTEGER_DIVIDE_BY_ZERO ((NTSTATUS)0xC0000094L)
#define STATUS_PRIVILEGED_INSTRUCTION ((NTSTATUS)0xC0000096L)
#define STATUS_NOT_FOUND ((NTSTATUS)0xC0000225L)
#ifndef STATUS_INSTANCE_NOT_AVAILABLE
#define STATUS_INSTANCE_NOT_AVAILABLE ((NTSTATUS)0xC00000ABL)
#endif
#define STATUS_STACK_BUFFER_OVERRUN ((NTSTATUS)0xC0000409L)
#define STATUS_ASSERTION_FAILURE ((NTSTATUS)0xC0000420L)
#define STATUS_INVALID_ADDRESS ((NTSTATUS)0xC0000141L)
#define STATUS_NO_CALLBACK_ACTIVE ((NTSTATUS)0xC0000258L)
#define STATUS_RESOURCE_DATA_NOT_FOUND ((NTSTATUS)0xC0000089L)
#define STATUS_RESOURCE_TYPE_NOT_FOUND ((NTSTATUS)0xC000008AL)
#define STATUS_RESOURCE_NAME_NOT_FOUND ((NTSTATUS)0xC000008BL)
#define STATUS_RESOURCE_LANG_NOT_FOUND ((NTSTATUS)0xC0000204L)
#define STATUS_VARIABLE_NOT_FOUND ((NTSTATUS)0xC0000100L)
#define STATUS_INVALID_PARAMETER_1 ((NTSTATUS)0xC00000EFL)
#define STATUS_INVALID_PARAMETER_2 ((NTSTATUS)0xC00000F0L)
#define STATUS_INVALID_PARAMETER_3 ((NTSTATUS)0xC00000F1L)
#define STATUS_INVALID_PARAMETER_4 ((NTSTATUS)0xC00000F2L)
#ifndef STATUS_INVALID_PARAMETER_5
#define STATUS_INVALID_PARAMETER_5 ((NTSTATUS)0xC00000F3L)
#endif
#ifndef STATUS_INVALID_PARAMETER_6
#define STATUS_INVALID_PARAMETER_6 ((NTSTATUS)0xC00000F4L)
#endif
#define STATUS_INVALID_PARAMETER_7 ((NTSTATUS)0xC00000F5L)
#define STATUS_COMMITMENT_LIMIT ((NTSTATUS)0xC000012DL)
#define STATUS_UNABLE_TO_DELETE_SECTION ((NTSTATUS)0xC000001BL)
#define STATUS_SECTION_PROTECTION ((NTSTATUS)0xC000004EL)
#define STATUS_INTEGER_OVERFLOW ((NTSTATUS)0xC0000095L)
#define STATUS_INVALID_FILE_FOR_SECTION ((NTSTATUS)0xC0000020L)

typedef struct _LIST_ENTRY {
    struct _LIST_ENTRY *Flink;
    struct _LIST_ENTRY *Blink;
} LIST_ENTRY, *PLIST_ENTRY;

typedef struct _SINGLE_LIST_ENTRY {
    struct _SINGLE_LIST_ENTRY *Next;
} SINGLE_LIST_ENTRY, *PSINGLE_LIST_ENTRY;

#define CONTAINING_RECORD(address, type, field) ((type *)((char *)(address) - offsetof(type, field)))
#define FIELD_OFFSET(type, field) ((LONG)offsetof(type, field))
#define RTL_NUMBER_OF(a) (sizeof(a) / sizeof((a)[0]))
#define ARRAYSIZE(a) RTL_NUMBER_OF(a)

FORCEINLINE void InitializeListHead(PLIST_ENTRY head)
{
    head->Flink = head->Blink = head;
}

FORCEINLINE BOOLEAN IsListEmpty(const LIST_ENTRY *head)
{
    return head->Flink == head;
}

FORCEINLINE BOOLEAN RemoveEntryList(PLIST_ENTRY entry)
{
    PLIST_ENTRY flink = entry->Flink, blink = entry->Blink;
    blink->Flink = flink;
    flink->Blink = blink;
    return flink == blink;
}

FORCEINLINE PLIST_ENTRY RemoveHeadList(PLIST_ENTRY head)
{
    PLIST_ENTRY entry = head->Flink;
    RemoveEntryList(entry);
    return entry;
}

FORCEINLINE PLIST_ENTRY RemoveTailList(PLIST_ENTRY head)
{
    PLIST_ENTRY entry = head->Blink;
    RemoveEntryList(entry);
    return entry;
}

FORCEINLINE void InsertTailList(PLIST_ENTRY head, PLIST_ENTRY entry)
{
    PLIST_ENTRY blink = head->Blink;
    entry->Flink = head;
    entry->Blink = blink;
    blink->Flink = entry;
    head->Blink = entry;
}

FORCEINLINE void InsertHeadList(PLIST_ENTRY head, PLIST_ENTRY entry)
{
    PLIST_ENTRY flink = head->Flink;
    entry->Flink = flink;
    entry->Blink = head;
    flink->Blink = entry;
    head->Flink = entry;
}

typedef struct _STRING {
    USHORT Length;
    USHORT MaximumLength;
    PCHAR Buffer;
} STRING, ANSI_STRING, OEM_STRING, *PSTRING, *PANSI_STRING, *POEM_STRING;
typedef const STRING *PCSTRING, *PCANSI_STRING;

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} UNICODE_STRING, *PUNICODE_STRING;
typedef const UNICODE_STRING *PCUNICODE_STRING;

#define RTL_CONSTANT_STRING(s) {sizeof(s) - sizeof((s)[0]), sizeof(s), (void *)(s)}

typedef struct _CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} CLIENT_ID, *PCLIENT_ID;

typedef struct _IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

typedef void(NTAPI *PIO_APC_ROUTINE)(PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, ULONG Reserved);

typedef struct _OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

#define OBJ_INHERIT 0x00000002L
#define OBJ_PERMANENT 0x00000010L
#define OBJ_EXCLUSIVE 0x00000020L
#define OBJ_CASE_INSENSITIVE 0x00000040L
#define OBJ_OPENIF 0x00000080L
#define OBJ_OPENLINK 0x00000100L
#define OBJ_KERNEL_HANDLE 0x00000200L
#define OBJ_FORCE_ACCESS_CHECK 0x00000400L

#define InitializeObjectAttributes(p, n, a, r, s)                                                                 \
    do {                                                                                                          \
        (p)->Length = sizeof(OBJECT_ATTRIBUTES);                                                                  \
        (p)->RootDirectory = (r);                                                                                 \
        (p)->Attributes = (a);                                                                                    \
        (p)->ObjectName = (n);                                                                                    \
        (p)->SecurityDescriptor = (s);                                                                            \
        (p)->SecurityQualityOfService = nullptr;                                                                     \
    } while (0)

#define MAX_PATH 260

#define NtCurrentProcess() ((HANDLE)(LONG_PTR)-1)
#define NtCurrentThread() ((HANDLE)(LONG_PTR)-2)

#ifndef MIME_KERNEL
FORCEINLINE struct _TEB *NtCurrentTeb(void)
{
    struct _TEB *teb;
#ifdef __i386__
    __asm__("movl %%fs:0x18, %0" : "=r"(teb));
#elifdef __x86_64__
    __asm__("movq %%gs:0x30, %0" : "=r"(teb));
#else
    /* arm64 user mode keeps the teb in x18. */
    __asm__("mov %0, x18" : "=r"(teb));
#endif
    return teb;
}
#endif
#define ZwCurrentProcess() NtCurrentProcess()
#define ZwCurrentThread() NtCurrentThread()

/* access rights. */
#define DELETE 0x00010000L
#define READ_CONTROL 0x00020000L
#define WRITE_DAC 0x00040000L
#define WRITE_OWNER 0x00080000L
#define SYNCHRONIZE 0x00100000L
#define STANDARD_RIGHTS_REQUIRED 0x000F0000L
#define STANDARD_RIGHTS_READ READ_CONTROL
#define STANDARD_RIGHTS_WRITE READ_CONTROL
#define STANDARD_RIGHTS_EXECUTE READ_CONTROL
#define STANDARD_RIGHTS_ALL 0x001F0000L
#define MAXIMUM_ALLOWED 0x02000000L
#define GENERIC_READ 0x80000000L
#define GENERIC_WRITE 0x40000000L
#define GENERIC_EXECUTE 0x20000000L
#define GENERIC_ALL 0x10000000L

#define FILE_READ_DATA 0x0001
#define FILE_LIST_DIRECTORY 0x0001
#define FILE_WRITE_DATA 0x0002
#define FILE_ADD_FILE 0x0002
#define FILE_APPEND_DATA 0x0004
#define FILE_READ_EA 0x0008
#define FILE_WRITE_EA 0x0010
#define FILE_EXECUTE 0x0020
#define FILE_TRAVERSE 0x0020
#define FILE_DELETE_CHILD 0x0040
#define FILE_READ_ATTRIBUTES 0x0080
#define FILE_WRITE_ATTRIBUTES 0x0100
#define FILE_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x1FF)
#define FILE_GENERIC_READ (STANDARD_RIGHTS_READ | FILE_READ_DATA | FILE_READ_ATTRIBUTES | FILE_READ_EA | SYNCHRONIZE)
#define FILE_GENERIC_WRITE                                                                                        \
    (STANDARD_RIGHTS_WRITE | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | FILE_APPEND_DATA | SYNCHRONIZE)
#define FILE_GENERIC_EXECUTE (STANDARD_RIGHTS_EXECUTE | FILE_READ_ATTRIBUTES | FILE_EXECUTE | SYNCHRONIZE)

#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define FILE_SHARE_DELETE 0x00000004

#define FILE_SUPERSEDE 0x00000000
#define FILE_OPEN 0x00000001
#define FILE_CREATE 0x00000002
#define FILE_OPEN_IF 0x00000003
#define FILE_OVERWRITE 0x00000004
#define FILE_OVERWRITE_IF 0x00000005

#define FILE_DIRECTORY_FILE 0x00000001
#define FILE_WRITE_THROUGH 0x00000002
#define FILE_SEQUENTIAL_ONLY 0x00000004
#define FILE_NO_INTERMEDIATE_BUFFERING 0x00000008
#define FILE_SYNCHRONOUS_IO_ALERT 0x00000010
#define FILE_SYNCHRONOUS_IO_NONALERT 0x00000020
#define FILE_NON_DIRECTORY_FILE 0x00000040
#define FILE_OPEN_BY_FILE_ID 0x00002000
#define FILE_DELETE_ON_CLOSE 0x00001000
#define FILE_OPEN_FOR_BACKUP_INTENT 0x00004000
#define FILE_OPEN_REPARSE_POINT 0x00200000

#define FILE_SUPERSEDED 0x00000000
#define FILE_OPENED 0x00000001
#define FILE_CREATED 0x00000002
#define FILE_OVERWRITTEN 0x00000003
#define FILE_EXISTS 0x00000004
#define FILE_DOES_NOT_EXIST 0x00000005

#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_ATTRIBUTE_REPARSE_POINT 0x00000400

typedef enum _FILE_INFORMATION_CLASS {
    FileDirectoryInformation = 1,
    FileFullDirectoryInformation = 2,
    FileBothDirectoryInformation = 3,
    FileBasicInformation = 4,
    FileStandardInformation = 5,
    FileInternalInformation = 6,
    FileEaInformation = 7,
    FileAccessInformation = 8,
    FileNameInformation = 9,
    FileRenameInformation = 10,
    FileLinkInformation = 11,
    FileNamesInformation = 12,
    FileDispositionInformation = 13,
    FilePositionInformation = 14,
    FileModeInformation = 16,
    FileAlignmentInformation = 17,
    FileAllInformation = 18,
    FileAllocationInformation = 19,
    FileEndOfFileInformation = 20,
    FilePipeInformation = 23,
    FilePipeLocalInformation = 24,
    FileCompletionInformation = 30,
    FileNetworkOpenInformation = 34,
    FileAttributeTagInformation = 35,
    FileIdBothDirectoryInformation = 37,
    FileIdFullDirectoryInformation = 38,
    FileIoCompletionNotificationInformation = 41,
    FileHardLinkInformation = 46,
    FileStandardLinkInformation = 54,
    FileIdInformation = 59,
    FileDispositionInformationEx = 64,
    FileRenameInformationEx = 65,
    FileStatInformation = 68,
    FileStatLxInformation = 70,
    FileLinkInformationEx = 72,
    FileMaximumInformation
} FILE_INFORMATION_CLASS;

typedef struct _FILE_IO_COMPLETION_NOTIFICATION_INFORMATION {
    ULONG Flags;
} FILE_IO_COMPLETION_NOTIFICATION_INFORMATION;

typedef struct _FILE_BASIC_INFORMATION {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    ULONG FileAttributes;
} FILE_BASIC_INFORMATION, *PFILE_BASIC_INFORMATION;

typedef struct _FILE_STANDARD_INFORMATION {
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG NumberOfLinks;
    BOOLEAN DeletePending;
    BOOLEAN Directory;
} FILE_STANDARD_INFORMATION, *PFILE_STANDARD_INFORMATION;

typedef struct _FILE_POSITION_INFORMATION {
    LARGE_INTEGER CurrentByteOffset;
} FILE_POSITION_INFORMATION, *PFILE_POSITION_INFORMATION;

typedef struct _FILE_END_OF_FILE_INFORMATION {
    LARGE_INTEGER EndOfFile;
} FILE_END_OF_FILE_INFORMATION, *PFILE_END_OF_FILE_INFORMATION;

typedef struct _FILE_DISPOSITION_INFORMATION {
    BOOLEAN DeleteFile;
} FILE_DISPOSITION_INFORMATION, *PFILE_DISPOSITION_INFORMATION;

typedef struct _FILE_NETWORK_OPEN_INFORMATION {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG FileAttributes;
} FILE_NETWORK_OPEN_INFORMATION, *PFILE_NETWORK_OPEN_INFORMATION;

typedef struct _FILE_INTERNAL_INFORMATION {
    LARGE_INTEGER IndexNumber;
} FILE_INTERNAL_INFORMATION, *PFILE_INTERNAL_INFORMATION;

typedef struct _FILE_MODE_INFORMATION {
    ULONG Mode;
} FILE_MODE_INFORMATION, *PFILE_MODE_INFORMATION;

typedef struct _FILE_NAME_INFORMATION {
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_NAME_INFORMATION, *PFILE_NAME_INFORMATION;

/* the names of a file: one entry after another, each NextEntryOffset on from the last, which is 0 for the last. */
typedef struct _FILE_LINK_ENTRY_INFORMATION {
    ULONG NextEntryOffset;
    DECLSPEC_ALIGN(8) LONGLONG ParentFileId;
    ULONG FileNameLength; /* in characters. */
    WCHAR FileName[1];
} FILE_LINK_ENTRY_INFORMATION, *PFILE_LINK_ENTRY_INFORMATION;

typedef struct _FILE_LINKS_INFORMATION {
    ULONG BytesNeeded;
    ULONG EntriesReturned;
    FILE_LINK_ENTRY_INFORMATION Entry;
} FILE_LINKS_INFORMATION, *PFILE_LINKS_INFORMATION;

typedef struct _FILE_STANDARD_LINK_INFORMATION {
    ULONG NumberOfAccessibleLinks;
    ULONG TotalNumberOfLinks;
    BOOLEAN DeletePending;
    BOOLEAN Directory;
} FILE_STANDARD_LINK_INFORMATION, *PFILE_STANDARD_LINK_INFORMATION;

typedef struct _FILE_ID_INFORMATION {
    ULONGLONG VolumeSerialNumber;
    UCHAR FileId[16];
} FILE_ID_INFORMATION, *PFILE_ID_INFORMATION;

typedef struct _FILE_STAT_INFORMATION {
    LARGE_INTEGER FileId;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG FileAttributes;
    ULONG ReparseTag;
    ULONG NumberOfLinks;
    ULONG EffectiveAccess;
} FILE_STAT_INFORMATION, *PFILE_STAT_INFORMATION;

typedef struct _FILE_STAT_LX_INFORMATION {
    LARGE_INTEGER FileId;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG FileAttributes;
    ULONG ReparseTag;
    ULONG NumberOfLinks;
    ULONG EffectiveAccess;
    ULONG LxFlags;
    ULONG LxUid;
    ULONG LxGid;
    ULONG LxMode;
    ULONG LxDeviceIdMajor;
    ULONG LxDeviceIdMinor;
} FILE_STAT_LX_INFORMATION, *PFILE_STAT_LX_INFORMATION;

/* the first bytes of FILE_LINK_INFORMATION and FILE_LINK_INFORMATION_EX, as FILE_RENAME_INFORMATION lays them out. */
#define FILE_LINK_REPLACE_IF_EXISTS 0x00000001
#define FILE_LINK_POSIX_SEMANTICS 0x00000002
#define FILE_LINK_SUPPRESS_STORAGE_RESERVE_INHERITANCE 0x00000008
#define FILE_LINK_NO_INCREASE_AVAILABLE_SPACE 0x00000010
#define FILE_LINK_NO_DECREASE_AVAILABLE_SPACE 0x00000020
#define FILE_LINK_PRESERVE_AVAILABLE_SPACE 0x00000030
#define FILE_LINK_IGNORE_READONLY_ATTRIBUTE 0x00000040
#define FILE_LINK_FORCE_RESIZE_TARGET_SR 0x00000080
#define FILE_LINK_FORCE_RESIZE_SOURCE_SR 0x00000100
#define FILE_LINK_FORCE_RESIZE_SR 0x00000180

typedef struct _FILE_ATTRIBUTE_TAG_INFORMATION {
    ULONG FileAttributes;
    ULONG ReparseTag;
} FILE_ATTRIBUTE_TAG_INFORMATION, *PFILE_ATTRIBUTE_TAG_INFORMATION;

typedef struct _FILE_DIRECTORY_INFORMATION {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_DIRECTORY_INFORMATION, *PFILE_DIRECTORY_INFORMATION;

typedef struct _FILE_FULL_DIR_INFORMATION {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    ULONG EaSize;
    WCHAR FileName[1];
} FILE_FULL_DIR_INFORMATION, *PFILE_FULL_DIR_INFORMATION;

typedef struct _FILE_BOTH_DIR_INFORMATION {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    ULONG EaSize;
    CCHAR ShortNameLength;
    WCHAR ShortName[12];
    WCHAR FileName[1];
} FILE_BOTH_DIR_INFORMATION, *PFILE_BOTH_DIR_INFORMATION;

typedef struct _FILE_ID_BOTH_DIR_INFORMATION {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    ULONG EaSize;
    CCHAR ShortNameLength;
    WCHAR ShortName[12];
    LARGE_INTEGER FileId;
    WCHAR FileName[1];
} FILE_ID_BOTH_DIR_INFORMATION, *PFILE_ID_BOTH_DIR_INFORMATION;

typedef struct _FILE_NAMES_INFORMATION {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_NAMES_INFORMATION, *PFILE_NAMES_INFORMATION;

typedef enum _FSINFOCLASS {
    FileFsVolumeInformation = 1,
    FileFsLabelInformation = 2,
    FileFsSizeInformation = 3,
    FileFsDeviceInformation = 4,
    FileFsAttributeInformation = 5,
    FileFsFullSizeInformation = 7,
} FS_INFORMATION_CLASS;

typedef struct _FILE_FS_LABEL_INFORMATION {
    ULONG VolumeLabelLength;
    WCHAR VolumeLabel[1];
} FILE_FS_LABEL_INFORMATION, *PFILE_FS_LABEL_INFORMATION;

typedef struct _FILE_FS_DEVICE_INFORMATION {
    ULONG DeviceType;
    ULONG Characteristics;
} FILE_FS_DEVICE_INFORMATION, *PFILE_FS_DEVICE_INFORMATION;

/* memory management. */
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOPY 0x08
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#define PAGE_GUARD 0x100
#define PAGE_NOCACHE 0x200
#define PAGE_WRITECOMBINE 0x400
#define PAGE_TARGETS_NO_UPDATE 0x40000000
#define PAGE_TARGETS_INVALID 0x40000000

#define MEM_COMMIT 0x00001000
#define MEM_RESERVE 0x00002000
#define MEM_DECOMMIT 0x00004000
#define MEM_RELEASE 0x00008000
#define MEM_FREE 0x00010000
#define MEM_PRIVATE 0x00020000
#define MEM_MAPPED 0x00040000
#define MEM_REPLACE_PLACEHOLDER 0x00004000 /* with MEM_RESERVE, or for MEM_MAPPED views. */
#define MEM_RESERVE_PLACEHOLDER 0x00040000
#define MEM_RESET 0x00080000
#define MEM_TOP_DOWN 0x00100000
#define MEM_WRITE_WATCH 0x00200000
#define MEM_PHYSICAL 0x00400000
#define MEM_ROTATE 0x00800000
#define MEM_RESET_UNDO 0x01000000
#define MEM_LARGE_PAGES 0x20000000
#define MEM_IMAGE 0x01000000
#define WRITE_WATCH_FLAG_RESET 0x01
#define MEM_COALESCE_PLACEHOLDERS 0x00000001 /* with MEM_RELEASE. */
#define MEM_PRESERVE_PLACEHOLDER 0x00000002  /* with MEM_RELEASE. */
#define UNMAP_WITH_TRANSIENT_BOOST 0x00000001
#define UNMAP_PRESERVE_PLACEHOLDER 0x00000002
#define UNMAP_NONE 0x00000000

typedef enum _MEM_EXTENDED_PARAMETER_TYPE {
    MemExtendedParameterInvalidType = 0,
    MemExtendedParameterAddressRequirements = 1,
    MemExtendedParameterNumaNode = 2,
    MemExtendedParameterPartitionHandle = 3,
    MemExtendedParameterUserPhysicalHandle = 4,
    MemExtendedParameterAttributeFlags = 5,
    MemExtendedParameterImageMachine = 6,
    MemExtendedParameterMax = 7
} MEM_EXTENDED_PARAMETER_TYPE;

#define MEM_EXTENDED_PARAMETER_GRAPHICS 0x1
#define MEM_EXTENDED_PARAMETER_NONPAGED 0x2
#define MEM_EXTENDED_PARAMETER_ZERO_PAGES_OPTIONAL 0x4
#define MEM_EXTENDED_PARAMETER_NONPAGED_LARGE 0x8
#define MEM_EXTENDED_PARAMETER_NONPAGED_HUGE 0x10
#define MEM_EXTENDED_PARAMETER_SOFT_FAULT_PAGES 0x20
#define MEM_EXTENDED_PARAMETER_EC_CODE 0x40

typedef struct DECLSPEC_ALIGN(8) _MEM_EXTENDED_PARAMETER {
    struct {
        ULONG64 Type : 8;
        ULONG64 Reserved : 56;
    };
    union {
        ULONG64 ULong64;
        PVOID Pointer;
        SIZE_T Size;
        HANDLE Handle;
        ULONG ULong;
    };
} MEM_EXTENDED_PARAMETER, *PMEM_EXTENDED_PARAMETER;

typedef struct _MEM_ADDRESS_REQUIREMENTS {
    PVOID LowestStartingAddress;
    PVOID HighestEndingAddress;
    SIZE_T Alignment;
} MEM_ADDRESS_REQUIREMENTS, *PMEM_ADDRESS_REQUIREMENTS;

typedef struct _MEMORY_RANGE_ENTRY {
    PVOID VirtualAddress;
    SIZE_T NumberOfBytes;
} MEMORY_RANGE_ENTRY, *PMEMORY_RANGE_ENTRY;

typedef enum _VIRTUAL_MEMORY_INFORMATION_CLASS {
    VmPrefetchInformation = 0,
    VmPagePriorityInformation = 1,
    VmCfgCallTargetInformation = 2,
    VmPageDirtyStateInformation = 3,
    VmImageHotPatchInformation = 4,
    VmPhysicalContiguityInformation = 5,
    VmVirtualMachinePrepopulateInformation = 6,
    VmRemoveFromWorkingSetInformation = 7,
    VmInformationClassMax
} VIRTUAL_MEMORY_INFORMATION_CLASS;

#define SEC_FILE 0x00800000
#define SEC_IMAGE 0x01000000
#define SEC_RESERVE 0x04000000
#define SEC_COMMIT 0x08000000
#define SEC_NOCACHE 0x10000000
#define SEC_LARGE_PAGES 0x80000000
#define SEC_IMAGE_NO_EXECUTE (SEC_IMAGE | SEC_NOCACHE)

#define SECTION_QUERY 0x0001
#define SECTION_MAP_WRITE 0x0002
#define SECTION_MAP_READ 0x0004
#define SECTION_MAP_EXECUTE 0x0008
#define SECTION_EXTEND_SIZE 0x0010
#define SECTION_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | 0x1F)

typedef enum _SECTION_INHERIT {
    ViewShare = 1,
    ViewUnmap = 2
} SECTION_INHERIT;

typedef enum _MEMORY_INFORMATION_CLASS {
    MemoryBasicInformation = 0,
    MemoryWorkingSetInformation = 1,
    MemoryMappedFilenameInformation = 2,
    MemoryRegionInformation = 3,
    MemoryWorkingSetExInformation = 4,
    MemoryImageInformation = 6,
} MEMORY_INFORMATION_CLASS;

typedef struct _MEMORY_BASIC_INFORMATION {
    PVOID BaseAddress;
    PVOID AllocationBase;
    DWORD AllocationProtect;
#ifdef _WIN64
    WORD PartitionId;
#endif
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
} MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;

#define MEMORY_IMAGE_PARTIAL_MAP 0x1      /* the view does not cover the whole image. */
#define MEMORY_IMAGE_NOT_EXECUTABLE 0x2   /* mapped as data, SEC_IMAGE_NO_EXECUTE. */

typedef struct _MEMORY_IMAGE_INFORMATION {
    PVOID ImageBase;
    SIZE_T SizeOfImage;
    ULONG ImageFlags; /* MEMORY_IMAGE_*, with the signing level in bits 2 to 5. */
    ULONG Reserved;
} MEMORY_IMAGE_INFORMATION, *PMEMORY_IMAGE_INFORMATION;

/* process, thread and synchronisation rights. */
#define PROCESS_TERMINATE 0x0001
#define PROCESS_CREATE_THREAD 0x0002
#define PROCESS_VM_OPERATION 0x0008
#define PROCESS_VM_READ 0x0010
#define PROCESS_VM_WRITE 0x0020
#define PROCESS_DUP_HANDLE 0x0040
#define PROCESS_CREATE_PROCESS 0x0080
#define PROCESS_SET_QUOTA 0x0100
#define PROCESS_SET_INFORMATION 0x0200
#define PROCESS_QUERY_INFORMATION 0x0400
#define PROCESS_SUSPEND_RESUME 0x0800
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#define PROCESS_SET_LIMITED_INFORMATION 0x2000
#define PROCESS_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0xFFFF)
#define THREAD_TERMINATE 0x0001
#define THREAD_SUSPEND_RESUME 0x0002
#define THREAD_GET_CONTEXT 0x0008
#define THREAD_SET_CONTEXT 0x0010
#define THREAD_QUERY_INFORMATION 0x0040
#define THREAD_SET_INFORMATION 0x0020
#define THREAD_SET_THREAD_TOKEN 0x0080
#define THREAD_IMPERSONATE 0x0100
#define THREAD_DIRECT_IMPERSONATION 0x0200
#define THREAD_SET_LIMITED_INFORMATION 0x0400
#define THREAD_QUERY_LIMITED_INFORMATION 0x0800
#define THREAD_RESUME 0x1000
#define THREAD_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0xFFFF)
#define EVENT_QUERY_STATE 0x0001
#define EVENT_MODIFY_STATE 0x0002
#define EVENT_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x3)
#define MUTANT_QUERY_STATE 0x0001
#define MUTANT_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | MUTANT_QUERY_STATE)
#define SEMAPHORE_MODIFY_STATE 0x0002
#define SEMAPHORE_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x3)
#define TIMER_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x3)
#define DIRECTORY_QUERY 0x0001
#define DIRECTORY_TRAVERSE 0x0002
#define DIRECTORY_CREATE_OBJECT 0x0004
#define DIRECTORY_CREATE_SUBDIRECTORY 0x0008
#define DIRECTORY_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | 0xF)
#define SYMBOLIC_LINK_QUERY 0x0001
#define SYMBOLIC_LINK_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | 0x1)

typedef enum _EVENT_TYPE {
    NotificationEvent,
    SynchronizationEvent
} EVENT_TYPE;

typedef enum _TIMER_TYPE {
    NotificationTimer,
    SynchronizationTimer
} TIMER_TYPE;

typedef enum _WAIT_TYPE {
    WaitAll,
    WaitAny,
    WaitNotification,
    WaitDequeue,
    WaitDpc,
} WAIT_TYPE;

typedef enum _MODE {
    KernelMode,
    UserMode,
    MaximumMode
} MODE;

/* pe images. */
#define IMAGE_DOS_SIGNATURE 0x5A4D
#define IMAGE_NT_SIGNATURE 0x00004550
#define IMAGE_FILE_MACHINE_ARM64 0xAA64
#define IMAGE_FILE_MACHINE_AMD64 0x8664
#define IMAGE_FILE_MACHINE_I386 0x014C
#define IMAGE_NT_OPTIONAL_HDR32_MAGIC 0x10B
#define IMAGE_NT_OPTIONAL_HDR64_MAGIC 0x20B
#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES 16
#define IMAGE_SIZEOF_SHORT_NAME 8

#define IMAGE_DIRECTORY_ENTRY_EXPORT 0
#define IMAGE_DIRECTORY_ENTRY_IMPORT 1
#define IMAGE_DIRECTORY_ENTRY_RESOURCE 2
#define IMAGE_DIRECTORY_ENTRY_EXCEPTION 3
#define IMAGE_DIRECTORY_ENTRY_SECURITY 4
#define IMAGE_DIRECTORY_ENTRY_BASERELOC 5
#define IMAGE_DIRECTORY_ENTRY_DEBUG 6
#define IMAGE_DIRECTORY_ENTRY_TLS 9
#define IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG 10
#define IMAGE_DIRECTORY_ENTRY_IAT 12
#define IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT 13

#define IMAGE_FILE_RELOCS_STRIPPED 0x0001
#define IMAGE_FILE_EXECUTABLE_IMAGE 0x0002
#define IMAGE_FILE_DLL 0x2000

#define IMAGE_SUBSYSTEM_NATIVE 1
#define IMAGE_SUBSYSTEM_WINDOWS_GUI 2
#define IMAGE_SUBSYSTEM_WINDOWS_CUI 3

#define IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE 0x0040
#define IMAGE_DLLCHARACTERISTICS_NX_COMPAT 0x0100

#define IMAGE_SCN_CNT_CODE 0x00000020
#define IMAGE_SCN_CNT_INITIALIZED_DATA 0x00000040
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x00000080
#define IMAGE_SCN_MEM_DISCARDABLE 0x02000000
#define IMAGE_SCN_MEM_SHARED 0x10000000
#define IMAGE_SCN_MEM_EXECUTE 0x20000000
#define IMAGE_SCN_MEM_READ 0x40000000
#define IMAGE_SCN_MEM_WRITE 0x80000000

#define IMAGE_REL_BASED_ABSOLUTE 0
#define IMAGE_REL_BASED_HIGHLOW 3
#define IMAGE_REL_BASED_DIR64 10

#define IMAGE_ORDINAL_FLAG64 0x8000000000000000ull
#define IMAGE_SNAP_BY_ORDINAL64(o) (((o) & IMAGE_ORDINAL_FLAG64) != 0)
#define IMAGE_ORDINAL64(o) ((o) & 0xFFFF)

typedef struct _IMAGE_DOS_HEADER {
    USHORT e_magic;
    USHORT e_cblp, e_cp, e_crlc, e_cparhdr, e_minalloc, e_maxalloc, e_ss, e_sp, e_csum, e_ip, e_cs, e_lfarlc, e_ovno;
    USHORT e_res[4];
    USHORT e_oemid, e_oeminfo;
    USHORT e_res2[10];
    LONG e_lfanew;
} IMAGE_DOS_HEADER, *PIMAGE_DOS_HEADER;

typedef struct _IMAGE_FILE_HEADER {
    USHORT Machine;
    USHORT NumberOfSections;
    ULONG TimeDateStamp;
    ULONG PointerToSymbolTable;
    ULONG NumberOfSymbols;
    USHORT SizeOfOptionalHeader;
    USHORT Characteristics;
} IMAGE_FILE_HEADER, *PIMAGE_FILE_HEADER;

typedef struct _IMAGE_DATA_DIRECTORY {
    ULONG VirtualAddress;
    ULONG Size;
} IMAGE_DATA_DIRECTORY, *PIMAGE_DATA_DIRECTORY;

typedef struct _IMAGE_OPTIONAL_HEADER32 {
    USHORT Magic;
    UCHAR MajorLinkerVersion;
    UCHAR MinorLinkerVersion;
    ULONG SizeOfCode;
    ULONG SizeOfInitializedData;
    ULONG SizeOfUninitializedData;
    ULONG AddressOfEntryPoint;
    ULONG BaseOfCode;
    ULONG BaseOfData;
    ULONG ImageBase;
    ULONG SectionAlignment;
    ULONG FileAlignment;
    USHORT MajorOperatingSystemVersion;
    USHORT MinorOperatingSystemVersion;
    USHORT MajorImageVersion;
    USHORT MinorImageVersion;
    USHORT MajorSubsystemVersion;
    USHORT MinorSubsystemVersion;
    ULONG Win32VersionValue;
    ULONG SizeOfImage;
    ULONG SizeOfHeaders;
    ULONG CheckSum;
    USHORT Subsystem;
    USHORT DllCharacteristics;
    ULONG SizeOfStackReserve;
    ULONG SizeOfStackCommit;
    ULONG SizeOfHeapReserve;
    ULONG SizeOfHeapCommit;
    ULONG LoaderFlags;
    ULONG NumberOfRvaAndSizes;
    IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
} IMAGE_OPTIONAL_HEADER32, *PIMAGE_OPTIONAL_HEADER32;

typedef struct _IMAGE_NT_HEADERS32 {
    ULONG Signature;
    IMAGE_FILE_HEADER FileHeader;
    IMAGE_OPTIONAL_HEADER32 OptionalHeader;
} IMAGE_NT_HEADERS32, *PIMAGE_NT_HEADERS32;

typedef struct _IMAGE_OPTIONAL_HEADER64 {
    USHORT Magic;
    UCHAR MajorLinkerVersion;
    UCHAR MinorLinkerVersion;
    ULONG SizeOfCode;
    ULONG SizeOfInitializedData;
    ULONG SizeOfUninitializedData;
    ULONG AddressOfEntryPoint;
    ULONG BaseOfCode;
    ULONGLONG ImageBase;
    ULONG SectionAlignment;
    ULONG FileAlignment;
    USHORT MajorOperatingSystemVersion;
    USHORT MinorOperatingSystemVersion;
    USHORT MajorImageVersion;
    USHORT MinorImageVersion;
    USHORT MajorSubsystemVersion;
    USHORT MinorSubsystemVersion;
    ULONG Win32VersionValue;
    ULONG SizeOfImage;
    ULONG SizeOfHeaders;
    ULONG CheckSum;
    USHORT Subsystem;
    USHORT DllCharacteristics;
    ULONGLONG SizeOfStackReserve;
    ULONGLONG SizeOfStackCommit;
    ULONGLONG SizeOfHeapReserve;
    ULONGLONG SizeOfHeapCommit;
    ULONG LoaderFlags;
    ULONG NumberOfRvaAndSizes;
    IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
} IMAGE_OPTIONAL_HEADER64, *PIMAGE_OPTIONAL_HEADER64;

typedef struct _IMAGE_NT_HEADERS64 {
    ULONG Signature;
    IMAGE_FILE_HEADER FileHeader;
    IMAGE_OPTIONAL_HEADER64 OptionalHeader;
} IMAGE_NT_HEADERS64, *PIMAGE_NT_HEADERS64;
#ifdef __i386__
typedef IMAGE_NT_HEADERS32 IMAGE_NT_HEADERS, *PIMAGE_NT_HEADERS;
#else
typedef IMAGE_NT_HEADERS64 IMAGE_NT_HEADERS, *PIMAGE_NT_HEADERS;
#endif

typedef struct _IMAGE_SECTION_HEADER {
    UCHAR Name[IMAGE_SIZEOF_SHORT_NAME];
    union {
        ULONG PhysicalAddress;
        ULONG VirtualSize;
    } Misc;
    ULONG VirtualAddress;
    ULONG SizeOfRawData;
    ULONG PointerToRawData;
    ULONG PointerToRelocations;
    ULONG PointerToLinenumbers;
    USHORT NumberOfRelocations;
    USHORT NumberOfLinenumbers;
    ULONG Characteristics;
} IMAGE_SECTION_HEADER, *PIMAGE_SECTION_HEADER;

#define IMAGE_FIRST_SECTION(nt)                                                                                   \
    ((PIMAGE_SECTION_HEADER)((ULONG_PTR)(nt) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +                     \
                             (nt)->FileHeader.SizeOfOptionalHeader))

typedef struct _IMAGE_EXPORT_DIRECTORY {
    ULONG Characteristics;
    ULONG TimeDateStamp;
    USHORT MajorVersion;
    USHORT MinorVersion;
    ULONG Name;
    ULONG Base;
    ULONG NumberOfFunctions;
    ULONG NumberOfNames;
    ULONG AddressOfFunctions;
    ULONG AddressOfNames;
    ULONG AddressOfNameOrdinals;
} IMAGE_EXPORT_DIRECTORY, *PIMAGE_EXPORT_DIRECTORY;

typedef struct _IMAGE_IMPORT_DESCRIPTOR {
    union {
        ULONG Characteristics;
        ULONG OriginalFirstThunk;
    };
    ULONG TimeDateStamp;
    ULONG ForwarderChain;
    ULONG Name;
    ULONG FirstThunk;
} IMAGE_IMPORT_DESCRIPTOR, *PIMAGE_IMPORT_DESCRIPTOR;

typedef struct _IMAGE_IMPORT_BY_NAME {
    USHORT Hint;
    CHAR Name[1];
} IMAGE_IMPORT_BY_NAME, *PIMAGE_IMPORT_BY_NAME;

typedef struct _IMAGE_THUNK_DATA32 {
    union {
        ULONG ForwarderString;
        ULONG Function;
        ULONG Ordinal;
        ULONG AddressOfData;
    } u1;
} IMAGE_THUNK_DATA32, *PIMAGE_THUNK_DATA32;

typedef struct _IMAGE_THUNK_DATA64 {
    union {
        ULONGLONG ForwarderString;
        ULONGLONG Function;
        ULONGLONG Ordinal;
        ULONGLONG AddressOfData;
    } u1;
} IMAGE_THUNK_DATA64, *PIMAGE_THUNK_DATA64;

typedef struct _IMAGE_DELAYLOAD_DESCRIPTOR {
    ULONG Attributes;
    ULONG DllNameRVA;
    ULONG ModuleHandleRVA;
    ULONG ImportAddressTableRVA;
    ULONG ImportNameTableRVA;
    ULONG BoundImportAddressTableRVA;
    ULONG UnloadInformationTableRVA;
    ULONG TimeDateStamp;
} IMAGE_DELAYLOAD_DESCRIPTOR, *PIMAGE_DELAYLOAD_DESCRIPTOR;

typedef struct _IMAGE_BASE_RELOCATION {
    ULONG VirtualAddress;
    ULONG SizeOfBlock;
} IMAGE_BASE_RELOCATION, *PIMAGE_BASE_RELOCATION;

typedef void(NTAPI *PIMAGE_TLS_CALLBACK)(PVOID DllHandle, ULONG Reason, PVOID Reserved);

typedef struct _IMAGE_TLS_DIRECTORY32 {
    ULONG StartAddressOfRawData;
    ULONG EndAddressOfRawData;
    ULONG AddressOfIndex;
    ULONG AddressOfCallBacks;
    ULONG SizeOfZeroFill;
    ULONG Characteristics;
} IMAGE_TLS_DIRECTORY32, *PIMAGE_TLS_DIRECTORY32;

typedef struct _IMAGE_TLS_DIRECTORY64 {
    ULONGLONG StartAddressOfRawData;
    ULONGLONG EndAddressOfRawData;
    ULONGLONG AddressOfIndex;
    ULONGLONG AddressOfCallBacks;
    ULONG SizeOfZeroFill;
    ULONG Characteristics;
} IMAGE_TLS_DIRECTORY64, *PIMAGE_TLS_DIRECTORY64;

#ifdef __i386__
typedef IMAGE_THUNK_DATA32 IMAGE_THUNK_DATA;
typedef IMAGE_TLS_DIRECTORY32 IMAGE_TLS_DIRECTORY;
#define IMAGE_SNAP_BY_ORDINAL(o) (((o) & 0x80000000u) != 0)
#else
typedef IMAGE_THUNK_DATA64 IMAGE_THUNK_DATA;
typedef IMAGE_TLS_DIRECTORY64 IMAGE_TLS_DIRECTORY;
#define IMAGE_SNAP_BY_ORDINAL(o) IMAGE_SNAP_BY_ORDINAL64(o)
#endif

typedef struct _IMAGE_RESOURCE_DIRECTORY {
    ULONG Characteristics;
    ULONG TimeDateStamp;
    USHORT MajorVersion;
    USHORT MinorVersion;
    USHORT NumberOfNamedEntries;
    USHORT NumberOfIdEntries;
} IMAGE_RESOURCE_DIRECTORY, *PIMAGE_RESOURCE_DIRECTORY;

typedef struct _IMAGE_RESOURCE_DIRECTORY_ENTRY {
    ULONG Name; /* high bit set: an offset to an IMAGE_RESOURCE_DIR_STRING_U. */
    ULONG OffsetToData; /* high bit set: points to a subdirectory. */
} IMAGE_RESOURCE_DIRECTORY_ENTRY, *PIMAGE_RESOURCE_DIRECTORY_ENTRY;

typedef struct _IMAGE_RESOURCE_DIR_STRING_U {
    USHORT Length;
    WCHAR NameString[1];
} IMAGE_RESOURCE_DIR_STRING_U, *PIMAGE_RESOURCE_DIR_STRING_U;

typedef struct _IMAGE_RESOURCE_DATA_ENTRY {
    ULONG OffsetToData;
    ULONG Size;
    ULONG CodePage;
    ULONG Reserved;
} IMAGE_RESOURCE_DATA_ENTRY, *PIMAGE_RESOURCE_DATA_ENTRY;

typedef struct _IMAGE_ARM64_RUNTIME_FUNCTION_ENTRY {
    ULONG BeginAddress;
    ULONG UnwindData;
} IMAGE_ARM64_RUNTIME_FUNCTION_ENTRY, *PIMAGE_ARM64_RUNTIME_FUNCTION_ENTRY;

#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1
#define DLL_THREAD_ATTACH 2
#define DLL_THREAD_DETACH 3

/* arm64 thread context. */
#define CONTEXT_ARM64 0x00400000L
#define CONTEXT_ARM64_CONTROL (CONTEXT_ARM64 | 0x1L)
#define CONTEXT_ARM64_INTEGER (CONTEXT_ARM64 | 0x2L)
#define CONTEXT_ARM64_FLOATING_POINT (CONTEXT_ARM64 | 0x4L)
#define CONTEXT_ARM64_DEBUG_REGISTERS (CONTEXT_ARM64 | 0x8L)
#define CONTEXT_ARM64_FULL (CONTEXT_ARM64_CONTROL | CONTEXT_ARM64_INTEGER | CONTEXT_ARM64_FLOATING_POINT)
/* set when unwinding leaves pc on a return address, so the instruction before it is the call. */
#define CONTEXT_ARM64_UNWOUND_TO_CALL 0x20000000L

typedef union _ARM64_NT_NEON128 {
    struct {
        ULONGLONG Low;
        LONGLONG High;
    };
    double D[2];
    float S[4];
    USHORT H[8];
    UCHAR B[16];
} ARM64_NT_NEON128, NEON128, *PNEON128;

typedef struct DECLSPEC_ALIGN(16) _ARM64_NT_CONTEXT {
    ULONG ContextFlags;
    ULONG Cpsr;
    union {
        struct {
            ULONG64 X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14, X15;
            ULONG64 X16, X17, X18, X19, X20, X21, X22, X23, X24, X25, X26, X27, X28;
            ULONG64 Fp;
            ULONG64 Lr;
        };
        ULONG64 X[31];
    };
    ULONG64 Sp;
    ULONG64 Pc;
    ARM64_NT_NEON128 V[32];
    ULONG Fpcr;
    ULONG Fpsr;
    ULONG Bcr[8];
    ULONG64 Bvr[8];
    ULONG Wcr[2];
    ULONG64 Wvr[2];
} ARM64_NT_CONTEXT, *PARM64_NT_CONTEXT;
MIME_FIELD_AT(ARM64_NT_CONTEXT, X, 0x8);
MIME_FIELD_AT(ARM64_NT_CONTEXT, Sp, 0x100);
MIME_FIELD_AT(ARM64_NT_CONTEXT, Pc, 0x108);
MIME_FIELD_AT(ARM64_NT_CONTEXT, V, 0x110);
MIME_FIELD_AT(ARM64_NT_CONTEXT, Fpcr, 0x310);
MIME_STATIC_ASSERT(sizeof(ARM64_NT_CONTEXT) == 0x390, "ARM64_NT_CONTEXT size");

/* x64 thread context, also used for x64 programs run by the translator. */
#define CONTEXT_AMD64 0x00100000L
#define CONTEXT_AMD64_CONTROL (CONTEXT_AMD64 | 0x1L)
#define CONTEXT_AMD64_INTEGER (CONTEXT_AMD64 | 0x2L)
#define CONTEXT_AMD64_SEGMENTS (CONTEXT_AMD64 | 0x4L)
#define CONTEXT_AMD64_FLOATING_POINT (CONTEXT_AMD64 | 0x8L)
#define CONTEXT_AMD64_DEBUG_REGISTERS (CONTEXT_AMD64 | 0x10L)
#define CONTEXT_AMD64_FULL (CONTEXT_AMD64_CONTROL | CONTEXT_AMD64_INTEGER | CONTEXT_AMD64_FLOATING_POINT)

typedef struct DECLSPEC_ALIGN(16) _M128A {
    ULONGLONG Low;
    LONGLONG High;
} M128A, *PM128A;

typedef struct DECLSPEC_ALIGN(16) _XSAVE_FORMAT {
    USHORT ControlWord;
    USHORT StatusWord;
    UCHAR TagWord;
    UCHAR Reserved1;
    USHORT ErrorOpcode;
    ULONG ErrorOffset;
    USHORT ErrorSelector;
    USHORT Reserved2;
    ULONG DataOffset;
    USHORT DataSelector;
    USHORT Reserved3;
    ULONG MxCsr;
    ULONG MxCsr_Mask;
    M128A FloatRegisters[8];
    M128A XmmRegisters[16];
    UCHAR Reserved4[96];
} XSAVE_FORMAT, *PXSAVE_FORMAT, XMM_SAVE_AREA32, *PXMM_SAVE_AREA32;
MIME_STATIC_ASSERT(sizeof(XSAVE_FORMAT) == 512, "XSAVE_FORMAT size");

typedef struct DECLSPEC_ALIGN(16) _AMD64_CONTEXT {
    ULONG64 P1Home, P2Home, P3Home, P4Home, P5Home, P6Home;
    ULONG ContextFlags;
    ULONG MxCsr;
    USHORT SegCs, SegDs, SegEs, SegFs, SegGs, SegSs;
    ULONG EFlags;
    ULONG64 Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
    ULONG64 Rax, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi;
    ULONG64 R8, R9, R10, R11, R12, R13, R14, R15;
    ULONG64 Rip;
    union {
        XMM_SAVE_AREA32 FltSave;
        struct {
            M128A Header[2];
            M128A Legacy[8];
            M128A Xmm0, Xmm1, Xmm2, Xmm3, Xmm4, Xmm5, Xmm6, Xmm7;
            M128A Xmm8, Xmm9, Xmm10, Xmm11, Xmm12, Xmm13, Xmm14, Xmm15;
        };
    };
    M128A VectorRegister[26];
    ULONG64 VectorControl;
    ULONG64 DebugControl;
    ULONG64 LastBranchToRip;
    ULONG64 LastBranchFromRip;
    ULONG64 LastExceptionToRip;
    ULONG64 LastExceptionFromRip;
} AMD64_CONTEXT, *PAMD64_CONTEXT;
MIME_FIELD_AT(AMD64_CONTEXT, ContextFlags, 0x30);
MIME_FIELD_AT(AMD64_CONTEXT, EFlags, 0x44);
MIME_FIELD_AT(AMD64_CONTEXT, Rax, 0x78);
MIME_FIELD_AT(AMD64_CONTEXT, Rip, 0xF8);
MIME_FIELD_AT(AMD64_CONTEXT, FltSave, 0x100);
MIME_FIELD_AT(AMD64_CONTEXT, Xmm0, 0x1A0);
MIME_FIELD_AT(AMD64_CONTEXT, VectorRegister, 0x300);
MIME_STATIC_ASSERT(sizeof(AMD64_CONTEXT) == 0x4D0, "AMD64_CONTEXT size");

/* x86 thread context. */
typedef struct _I386_CONTEXT {
    ULONG ContextFlags;
    ULONG Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
    struct {
        ULONG ControlWord, StatusWord, TagWord, ErrorOffset, ErrorSelector, DataOffset, DataSelector;
        UCHAR RegisterArea[80];
        ULONG Cr0NpxState;
    } FloatSave;
    ULONG SegGs, SegFs, SegEs, SegDs;
    ULONG Edi, Esi, Ebx, Edx, Ecx, Eax;
    ULONG Ebp, Eip, SegCs, EFlags, Esp, SegSs;
    UCHAR ExtendedRegisters[512];
} I386_CONTEXT;
MIME_FIELD_AT(I386_CONTEXT, Eip, 0xB8);
MIME_STATIC_ASSERT(sizeof(I386_CONTEXT) == 716, "I386_CONTEXT size");

/*
 * wow64: tls slot 1 of the 64-bit teb points at the cpu area at the top of the 64-bit stack, which holds the
 * 32-bit context while the thread runs 64-bit code. the reset flag makes the next entry into 32-bit code restore
 * every register, not only what a system call keeps.
 */
#define WOW64_TLS_CPURESERVED 1
#define WOW64_TLS_WOW64INFO 10
#define WOW64_CPURESERVED_FLAG_RESET_STATE 0x1

typedef struct _WOW64_CPURESERVED {
    USHORT Flags;
    USHORT Machine;
    I386_CONTEXT Context;
} WOW64_CPURESERVED, *PWOW64_CPURESERVED;
MIME_STATIC_ASSERT(offsetof(WOW64_CPURESERVED, Context) == 4, "wow64 cpu area layout");
#define CONTEXT_I386 0x00010000
#define CONTEXT_I386_CONTROL (CONTEXT_I386 | 1)
#define CONTEXT_I386_INTEGER (CONTEXT_I386 | 2)
#define CONTEXT_I386_SEGMENTS (CONTEXT_I386 | 4)
#define CONTEXT_I386_FLOATING_POINT (CONTEXT_I386 | 8)
#define CONTEXT_I386_EXTENDED_REGISTERS (CONTEXT_I386 | 0x20)
#define CONTEXT_I386_FULL (CONTEXT_I386 | 7)

#ifdef __i386__
typedef I386_CONTEXT CONTEXT, *PCONTEXT;
#define CONTEXT_CONTROL CONTEXT_I386_CONTROL
#define CONTEXT_INTEGER CONTEXT_I386_INTEGER
#define CONTEXT_FLOATING_POINT CONTEXT_I386_FLOATING_POINT
#define CONTEXT_FULL CONTEXT_I386_FULL
#define CONTEXT_ALL (CONTEXT_I386 | 0x3F)
#elifdef __x86_64__
typedef AMD64_CONTEXT CONTEXT, *PCONTEXT;
#define CONTEXT_CONTROL CONTEXT_AMD64_CONTROL
#define CONTEXT_INTEGER CONTEXT_AMD64_INTEGER
#define CONTEXT_SEGMENTS CONTEXT_AMD64_SEGMENTS
#define CONTEXT_FLOATING_POINT CONTEXT_AMD64_FLOATING_POINT
#define CONTEXT_DEBUG_REGISTERS CONTEXT_AMD64_DEBUG_REGISTERS
#define CONTEXT_FULL CONTEXT_AMD64_FULL
#define CONTEXT_ALL (CONTEXT_FULL | CONTEXT_SEGMENTS | CONTEXT_DEBUG_REGISTERS)
#else
typedef ARM64_NT_CONTEXT CONTEXT, *PCONTEXT;
#define CONTEXT_CONTROL CONTEXT_ARM64_CONTROL
#define CONTEXT_INTEGER CONTEXT_ARM64_INTEGER
#define CONTEXT_FLOATING_POINT CONTEXT_ARM64_FLOATING_POINT
#define CONTEXT_DEBUG_REGISTERS CONTEXT_ARM64_DEBUG_REGISTERS
#define CONTEXT_FULL CONTEXT_ARM64_FULL
#define CONTEXT_UNWOUND_TO_CALL CONTEXT_ARM64_UNWOUND_TO_CALL
#define CONTEXT_ALL (CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS)
#endif

#define EXCEPTION_MAXIMUM_PARAMETERS 15
#define EXCEPTION_NONCONTINUABLE 0x1
#define EXCEPTION_UNWINDING 0x2
#define EXCEPTION_EXIT_UNWIND 0x4
#define EXCEPTION_TARGET_UNWIND 0x20
#define EXCEPTION_COLLIDED_UNWIND 0x40
#define EXCEPTION_UNWIND (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND | EXCEPTION_TARGET_UNWIND | EXCEPTION_COLLIDED_UNWIND)

typedef struct _EXCEPTION_RECORD {
    NTSTATUS ExceptionCode;
    ULONG ExceptionFlags;
    struct _EXCEPTION_RECORD *ExceptionRecord;
    PVOID ExceptionAddress;
    ULONG NumberParameters;
    ULONG_PTR ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;

typedef struct _EXCEPTION_POINTERS {
    PEXCEPTION_RECORD ExceptionRecord;
    PCONTEXT ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS;

/* user mode process structures. */
typedef struct _NT_TIB {
    PVOID ExceptionList;
    PVOID StackBase;
    PVOID StackLimit;
    PVOID SubSystemTib;
    PVOID FiberData;
    PVOID ArbitraryUserPointer;
    struct _NT_TIB *Self;
} NT_TIB, *PNT_TIB;

typedef struct _CURDIR {
    UNICODE_STRING DosPath;
    HANDLE Handle;
} CURDIR, *PCURDIR;

typedef struct _RTL_DRIVE_LETTER_CURDIR {
    USHORT Flags;
    USHORT Length;
    ULONG TimeStamp;
    STRING DosPath;
} RTL_DRIVE_LETTER_CURDIR, *PRTL_DRIVE_LETTER_CURDIR;

#define RTL_USER_PROC_PARAMS_NORMALIZED 0x00000001

typedef struct _RTL_USER_PROCESS_PARAMETERS {
    ULONG MaximumLength;
    ULONG Length;
    ULONG Flags;
    ULONG DebugFlags;
    HANDLE ConsoleHandle;
    ULONG ConsoleFlags;
    HANDLE StandardInput;
    HANDLE StandardOutput;
    HANDLE StandardError;
    CURDIR CurrentDirectory;
    UNICODE_STRING DllPath;
    UNICODE_STRING ImagePathName;
    UNICODE_STRING CommandLine;
    PVOID Environment;
    ULONG StartingX;
    ULONG StartingY;
    ULONG CountX;
    ULONG CountY;
    ULONG CountCharsX;
    ULONG CountCharsY;
    ULONG FillAttribute;
    ULONG WindowFlags;
    ULONG ShowWindowFlags;
    UNICODE_STRING WindowTitle;
    UNICODE_STRING DesktopInfo;
    UNICODE_STRING ShellInfo;
    UNICODE_STRING RuntimeData;
    RTL_DRIVE_LETTER_CURDIR CurrentDirectores[32];
    ULONG_PTR EnvironmentSize;
    ULONG_PTR EnvironmentVersion;
    PVOID PackageDependencyData;
    ULONG ProcessGroupId;
    ULONG LoaderThreads;
} RTL_USER_PROCESS_PARAMETERS, *PRTL_USER_PROCESS_PARAMETERS;
#ifdef __i386__
MIME_FIELD_AT(RTL_USER_PROCESS_PARAMETERS, CurrentDirectory, 0x24);
MIME_FIELD_AT(RTL_USER_PROCESS_PARAMETERS, ImagePathName, 0x38);
MIME_FIELD_AT(RTL_USER_PROCESS_PARAMETERS, CommandLine, 0x40);
MIME_FIELD_AT(RTL_USER_PROCESS_PARAMETERS, Environment, 0x48);
MIME_FIELD_AT(RTL_USER_PROCESS_PARAMETERS, WindowTitle, 0x70);
MIME_FIELD_AT(RTL_USER_PROCESS_PARAMETERS, EnvironmentSize, 0x290);
#else
MIME_FIELD_AT(RTL_USER_PROCESS_PARAMETERS, CurrentDirectory, 0x38);
MIME_FIELD_AT(RTL_USER_PROCESS_PARAMETERS, ImagePathName, 0x60);
MIME_FIELD_AT(RTL_USER_PROCESS_PARAMETERS, CommandLine, 0x70);
MIME_FIELD_AT(RTL_USER_PROCESS_PARAMETERS, Environment, 0x80);
MIME_FIELD_AT(RTL_USER_PROCESS_PARAMETERS, WindowTitle, 0xB0);
MIME_FIELD_AT(RTL_USER_PROCESS_PARAMETERS, EnvironmentSize, 0x3F0);
#endif

typedef struct _PEB_LDR_DATA {
    ULONG Length;
    BOOLEAN Initialized;
    PVOID SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
    PVOID EntryInProgress;
    BOOLEAN ShutdownInProgress;
    HANDLE ShutdownThreadId;
} PEB_LDR_DATA, *PPEB_LDR_DATA;
#ifdef __i386__
MIME_FIELD_AT(PEB_LDR_DATA, InLoadOrderModuleList, 0xc);
MIME_FIELD_AT(PEB_LDR_DATA, InInitializationOrderModuleList, 0x1c);
#else
MIME_FIELD_AT(PEB_LDR_DATA, InLoadOrderModuleList, 0x10);
MIME_FIELD_AT(PEB_LDR_DATA, InInitializationOrderModuleList, 0x30);
#endif

typedef struct _RTL_BALANCED_NODE {
    struct _RTL_BALANCED_NODE *Children[2];
    ULONG_PTR ParentValue;
} RTL_BALANCED_NODE, *PRTL_BALANCED_NODE;

#define LDRP_IMAGE_DLL 0x00000004
#define LDRP_ENTRY_PROCESSED 0x00004000
#define LDRP_PROCESS_ATTACH_CALLED 0x00080000

typedef struct _LDR_DATA_TABLE_ENTRY {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
    ULONG Flags;
    USHORT ObsoleteLoadCount;
    USHORT TlsIndex;
    LIST_ENTRY HashLinks;
    ULONG TimeDateStamp;
    PVOID EntryPointActivationContext;
    PVOID Lock;
    PVOID DdagNode;
    LIST_ENTRY NodeModuleLink;
    PVOID LoadContext;
    PVOID ParentDllBase;
    PVOID SwitchBackContext;
    RTL_BALANCED_NODE BaseAddressIndexNode;
    RTL_BALANCED_NODE MappingInfoIndexNode;
    ULONG_PTR OriginalBase;
    LARGE_INTEGER LoadTime;
    ULONG BaseNameHashValue;
    ULONG LoadReason;
    ULONG ImplicitPathOptions;
    ULONG ReferenceCount;
    ULONG DependentLoadFlags;
    UCHAR SigningLevel;
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;
#ifdef __i386__
MIME_FIELD_AT(LDR_DATA_TABLE_ENTRY, DllBase, 0x18);
MIME_FIELD_AT(LDR_DATA_TABLE_ENTRY, FullDllName, 0x24);
MIME_FIELD_AT(LDR_DATA_TABLE_ENTRY, BaseDllName, 0x2c);
MIME_FIELD_AT(LDR_DATA_TABLE_ENTRY, HashLinks, 0x3c);
MIME_FIELD_AT(LDR_DATA_TABLE_ENTRY, OriginalBase, 0x80);
#else
MIME_FIELD_AT(LDR_DATA_TABLE_ENTRY, DllBase, 0x30);
MIME_FIELD_AT(LDR_DATA_TABLE_ENTRY, FullDllName, 0x48);
MIME_FIELD_AT(LDR_DATA_TABLE_ENTRY, BaseDllName, 0x58);
MIME_FIELD_AT(LDR_DATA_TABLE_ENTRY, HashLinks, 0x70);
MIME_FIELD_AT(LDR_DATA_TABLE_ENTRY, OriginalBase, 0xF8);
#endif

typedef struct _PEB {
    BOOLEAN InheritedAddressSpace;
    BOOLEAN ReadImageFileExecOptions;
    BOOLEAN BeingDebugged;
    UCHAR BitField;
#ifndef __i386__
    UCHAR Padding0[4];
#endif
    HANDLE Mutant;
    PVOID ImageBaseAddress;
    PPEB_LDR_DATA Ldr;
    PRTL_USER_PROCESS_PARAMETERS ProcessParameters;
    PVOID SubSystemData;
    PVOID ProcessHeap;
    PVOID FastPebLock;
    PVOID AtlThunkSListPtr;
    PVOID IFEOKey;
    ULONG CrossProcessFlags;
#ifndef __i386__
    UCHAR Padding1[4];
#endif
    PVOID KernelCallbackTable;
    ULONG SystemReserved;
    ULONG AtlThunkSListPtr32;
    PVOID ApiSetMap;
    ULONG TlsExpansionCounter;
#ifndef __i386__
    UCHAR Padding2[4];
#endif
    PVOID TlsBitmap;
    ULONG TlsBitmapBits[2];
    PVOID ReadOnlySharedMemoryBase;
    PVOID SharedData;
    PVOID *ReadOnlyStaticServerData;
    PVOID AnsiCodePageData;
    PVOID OemCodePageData;
    PVOID UnicodeCaseTableData;
    ULONG NumberOfProcessors;
    ULONG NtGlobalFlag;
    LARGE_INTEGER CriticalSectionTimeout;
    ULONG_PTR HeapSegmentReserve;
    ULONG_PTR HeapSegmentCommit;
    ULONG_PTR HeapDeCommitTotalFreeThreshold;
    ULONG_PTR HeapDeCommitFreeBlockThreshold;
    ULONG NumberOfHeaps;
    ULONG MaximumNumberOfHeaps;
    PVOID *ProcessHeaps;
    PVOID GdiSharedHandleTable;
    PVOID ProcessStarterHelper;
    ULONG GdiDCAttributeList;
#ifndef __i386__
    UCHAR Padding3[4];
#endif
    PVOID LoaderLock;
    ULONG OSMajorVersion;
    ULONG OSMinorVersion;
    USHORT OSBuildNumber;
    USHORT OSCSDVersion;
    ULONG OSPlatformId;
    ULONG ImageSubsystem;
    ULONG ImageSubsystemMajorVersion;
    ULONG ImageSubsystemMinorVersion;
#ifndef __i386__
    UCHAR Padding4[4];
#endif
    ULONG_PTR ActiveProcessAffinityMask;
#ifdef __i386__
    ULONG GdiHandleBuffer[34];
#else
    ULONG GdiHandleBuffer[60];
#endif
    PVOID PostProcessInitRoutine;
    PVOID TlsExpansionBitmap;
    ULONG TlsExpansionBitmapBits[32];
    ULONG SessionId;
#ifndef __i386__
    UCHAR Padding5[4];
#endif
    ULARGE_INTEGER AppCompatFlags;
    ULARGE_INTEGER AppCompatFlagsUser;
    PVOID pShimData;
    PVOID AppCompatInfo;
    UNICODE_STRING CSDVersion;
    PVOID ActivationContextData;
    PVOID ProcessAssemblyStorageMap;
    PVOID SystemDefaultActivationContextData;
    PVOID SystemAssemblyStorageMap;
    ULONG_PTR MinimumStackCommit;
    PVOID SparePointers[2];
    PVOID PatchLoaderData;
    PVOID ChpeV2ProcessInfo;
    ULONG AppModelFeatureState;
    ULONG SpareUlongs[2];
    USHORT ActiveCodePage;
    USHORT OemCodePage;
    USHORT UseCaseMapping;
    USHORT UnusedNlsField;
    PVOID WerRegistrationData;
    PVOID WerShipAssertPtr;
    PVOID EcCodeBitMap;
    PVOID pImageHeaderHash;
    ULONG TracingFlags;
#ifndef __i386__
    UCHAR Padding6[4];
#endif
    ULONGLONG CsrServerReadOnlySharedMemoryBase;
    ULONG_PTR TppWorkerpListLock;
    LIST_ENTRY TppWorkerpList;
    PVOID WaitOnAddressHashTable[128];
} PEB, *PPEB;
#ifdef __i386__
MIME_FIELD_AT(PEB, ImageBaseAddress, 0x8);
MIME_FIELD_AT(PEB, Ldr, 0xc);
MIME_FIELD_AT(PEB, ProcessParameters, 0x10);
MIME_FIELD_AT(PEB, ProcessHeap, 0x18);
MIME_FIELD_AT(PEB, KernelCallbackTable, 0x2c);
MIME_FIELD_AT(PEB, ApiSetMap, 0x38);
MIME_FIELD_AT(PEB, NumberOfProcessors, 0x64);
MIME_FIELD_AT(PEB, ProcessHeaps, 0x90);
MIME_FIELD_AT(PEB, LoaderLock, 0xa0);
MIME_FIELD_AT(PEB, OSMajorVersion, 0xa4);
MIME_FIELD_AT(PEB, ImageSubsystem, 0xb4);
MIME_FIELD_AT(PEB, GdiHandleBuffer, 0xc4);
MIME_FIELD_AT(PEB, SessionId, 0x1d4);
#else
MIME_FIELD_AT(PEB, ImageBaseAddress, 0x10);
MIME_FIELD_AT(PEB, Ldr, 0x18);
MIME_FIELD_AT(PEB, ProcessParameters, 0x20);
MIME_FIELD_AT(PEB, ProcessHeap, 0x30);
MIME_FIELD_AT(PEB, KernelCallbackTable, 0x58);
MIME_FIELD_AT(PEB, ApiSetMap, 0x68);
MIME_FIELD_AT(PEB, NumberOfProcessors, 0xB8);
MIME_FIELD_AT(PEB, ProcessHeaps, 0xF0);
MIME_FIELD_AT(PEB, LoaderLock, 0x110);
MIME_FIELD_AT(PEB, OSMajorVersion, 0x118);
MIME_FIELD_AT(PEB, ImageSubsystem, 0x128);
MIME_FIELD_AT(PEB, GdiHandleBuffer, 0x140);
MIME_FIELD_AT(PEB, SessionId, 0x2C0);
#endif

typedef struct _GDI_TEB_BATCH {
    ULONG Offset;
    ULONG_PTR HDC;
    ULONG Buffer[310];
} GDI_TEB_BATCH;

#ifdef __i386__
typedef struct _TEB {
    NT_TIB NtTib;
    PVOID EnvironmentPointer;
    CLIENT_ID ClientId;
    PVOID ActiveRpcHandle;
    PVOID ThreadLocalStoragePointer;
    PPEB ProcessEnvironmentBlock;
    ULONG LastErrorValue;
    ULONG CountOfOwnedCriticalSections;
    PVOID CsrClientThread;
    PVOID Win32ThreadInfo;
    ULONG User32Reserved[26];
    ULONG UserReserved[5];
    PVOID WOW32Reserved;
    LCID CurrentLocale;
    UCHAR Reserved0[0x184 - 0xC8];
    UCHAR ActivationStack[0x18];
    UCHAR WorkingOnBehalfTicket[8];
    NTSTATUS ExceptionCode;
    PVOID ActivationContextStackPointer;
    UCHAR Reserved1[0x1D4 - 0x1AC];
    GDI_TEB_BATCH GdiTebBatch;
    CLIENT_ID RealClientId;
    HANDLE GdiCachedProcessHandle;
    ULONG GdiClientPID;
    ULONG GdiClientTID;
    PVOID GdiThreadLocalInfo;
    ULONG_PTR Win32ClientInfo[62];
    PVOID glDispatchTable[233];
    ULONG_PTR glReserved1[29];
    PVOID glReserved2;
    PVOID glSectionInfo;
    PVOID glSection;
    PVOID glTable;
    PVOID glCurrentRC;
    PVOID glContext;
    NTSTATUS LastStatusValue;
    UNICODE_STRING StaticUnicodeString;
    WCHAR StaticUnicodeBuffer[261];
    UCHAR Padding0[2];
    PVOID DeallocationStack;
    PVOID TlsSlots[64];
    LIST_ENTRY TlsLinks;
    UCHAR Reserved2[0xF28 - 0xF18];
    ULONG HardErrorMode;
    UCHAR Reserved3[0xF50 - 0xF2C];
    GUID ActivityId;
    UCHAR Reserved3b[0xF70 - 0xF60];
    ULONG GdiBatchCount;
    ULONG CurrentIdealProcessor;
    ULONG GuaranteedStackBytes;
    PVOID ReservedForPerf;
    PVOID ReservedForOle;
    UCHAR Reserved4[0xF94 - 0xF84];
    PVOID *TlsExpansionSlots;
    UCHAR Reserved5[0xFAC - 0xF98];
    PVOID CurrentTransactionHandle;
    PVOID ActiveFrame;
    PVOID FlsData;
    UCHAR Reserved6[0xFCA - 0xFB8];
    USHORT SameTebFlags;
    UCHAR Reserved7[0xFE8 - 0xFCC];
    ULONGLONG ReservedForCrt;
} TEB, *PTEB;
MIME_FIELD_AT(TEB, ClientId, 0x20);
MIME_FIELD_AT(TEB, ThreadLocalStoragePointer, 0x2C);
MIME_FIELD_AT(TEB, ProcessEnvironmentBlock, 0x30);
MIME_FIELD_AT(TEB, LastErrorValue, 0x34);
MIME_FIELD_AT(TEB, CurrentLocale, 0xC4);
MIME_FIELD_AT(TEB, ActivationStack, 0x184);
MIME_FIELD_AT(TEB, ExceptionCode, 0x1A4);
MIME_FIELD_AT(TEB, ActivationContextStackPointer, 0x1A8);
MIME_FIELD_AT(TEB, GdiTebBatch, 0x1D4);
MIME_FIELD_AT(TEB, Win32ClientInfo, 0x6CC);
MIME_FIELD_AT(TEB, LastStatusValue, 0xBF4);
MIME_FIELD_AT(TEB, ActivityId, 0xF50);
MIME_FIELD_AT(TEB, TlsSlots, 0xE10);
MIME_FIELD_AT(TEB, CurrentTransactionHandle, 0xFAC);
MIME_FIELD_AT(TEB, ReservedForOle, 0xF80);
MIME_FIELD_AT(TEB, FlsData, 0xFB4);
MIME_FIELD_AT(TEB, SameTebFlags, 0xFCA);
#else
typedef struct _TEB {
    NT_TIB NtTib;
    PVOID EnvironmentPointer;
    CLIENT_ID ClientId;
    PVOID ActiveRpcHandle;
    PVOID ThreadLocalStoragePointer;
    PPEB ProcessEnvironmentBlock;
    ULONG LastErrorValue;
    ULONG CountOfOwnedCriticalSections;
    PVOID CsrClientThread;
    PVOID Win32ThreadInfo;
    ULONG User32Reserved[26];
    ULONG UserReserved[5];
    PVOID WOW32Reserved;
    LCID CurrentLocale;
    ULONG FpSoftwareStatusRegister;
    PVOID ReservedForDebuggerInstrumentation[16];
    PVOID SystemReserved1[30];
    CHAR PlaceholderCompatibilityMode;
    BOOLEAN PlaceholderHydrationAlwaysExplicit;
    CHAR PlaceholderReserved[10];
    ULONG ProxiedProcessId;
    UCHAR ActivationStack[0x28];
    UCHAR WorkingOnBehalfTicket[8];
    NTSTATUS ExceptionCode;
    UCHAR Padding0[4];
    PVOID ActivationContextStackPointer;
    ULONG_PTR InstrumentationCallbackSp;
    ULONG_PTR InstrumentationCallbackPreviousPc;
    ULONG_PTR InstrumentationCallbackPreviousSp;
    ULONG TxFsContext;
    BOOLEAN InstrumentationCallbackDisabled;
    BOOLEAN UnalignedLoadStoreExceptions;
    UCHAR Padding1[2];
    GDI_TEB_BATCH GdiTebBatch;
    CLIENT_ID RealClientId;
    HANDLE GdiCachedProcessHandle;
    ULONG GdiClientPID;
    ULONG GdiClientTID;
    PVOID GdiThreadLocalInfo;
    ULONG_PTR Win32ClientInfo[62];
    PVOID glDispatchTable[233];
    ULONG_PTR glReserved1[29];
    PVOID glReserved2;
    PVOID glSectionInfo;
    PVOID glSection;
    PVOID glTable;
    PVOID glCurrentRC;
    PVOID glContext;
    NTSTATUS LastStatusValue;
    UCHAR Padding2[4];
    UNICODE_STRING StaticUnicodeString;
    WCHAR StaticUnicodeBuffer[261];
    UCHAR Padding3[6];
    PVOID DeallocationStack;
    PVOID TlsSlots[64];
    LIST_ENTRY TlsLinks;
    PVOID Vdm;
    PVOID ReservedForNtRpc;
    PVOID DbgSsReserved[2];
    ULONG HardErrorMode;
    UCHAR Padding4[4];
    PVOID Instrumentation[11];
    GUID ActivityId;
    PVOID SubProcessTag;
    PVOID PerflibData;
    PVOID EtwTraceData;
    PVOID WinSockData;
    ULONG GdiBatchCount;
    ULONG CurrentIdealProcessor;
    ULONG GuaranteedStackBytes;
    UCHAR Padding5[4];
    PVOID ReservedForPerf;
    PVOID ReservedForOle;
    ULONG WaitingOnLoaderLock;
    UCHAR Padding6[4];
    PVOID SavedPriorityState;
    ULONG_PTR ReservedForCodeCoverage;
    PVOID ThreadPoolData;
    PVOID *TlsExpansionSlots;
    PVOID ChpeV2CpuAreaInfo;
    PVOID Unused;
    ULONG MuiGeneration;
    ULONG IsImpersonating;
    PVOID NlsCache;
    PVOID pShimData;
    ULONG HeapData;
    UCHAR Padding7[4];
    HANDLE CurrentTransactionHandle;
    PVOID ActiveFrame;
    PVOID FlsData;
    PVOID PreferredLanguages;
    PVOID UserPrefLanguages;
    PVOID MergedPrefLanguages;
    ULONG MuiImpersonation;
    USHORT CrossTebFlags;
    USHORT SameTebFlags;
    PVOID TxnScopeEnterCallback;
    PVOID TxnScopeExitCallback;
    PVOID TxnScopeContext;
    ULONG LockCount;
    LONG WowTebOffset;
    PVOID ResourceRetValue;
    PVOID ReservedForWdf;
    ULONGLONG ReservedForCrt;
    GUID EffectiveContainerId;
    ULONGLONG LastSleepCounter;
    ULONG SpinCallCount;
    UCHAR Padding8[4];
    ULONGLONG ExtendedFeatureDisableMask;
    PVOID SchedulerSharedDataSlot;
    PVOID HeapWalkContext;
    UCHAR PrimaryGroupAffinity[16];
    ULONG Rcu[2];
} TEB, *PTEB;
MIME_FIELD_AT(TEB, ClientId, 0x40);
MIME_FIELD_AT(TEB, ThreadLocalStoragePointer, 0x58);
MIME_FIELD_AT(TEB, ProcessEnvironmentBlock, 0x60);
MIME_FIELD_AT(TEB, LastErrorValue, 0x68);
MIME_FIELD_AT(TEB, Win32ThreadInfo, 0x78);
MIME_FIELD_AT(TEB, CurrentLocale, 0x108);
MIME_FIELD_AT(TEB, ActivationStack, 0x290);
MIME_FIELD_AT(TEB, ExceptionCode, 0x2C0);
MIME_FIELD_AT(TEB, ActivationContextStackPointer, 0x2C8);
MIME_FIELD_AT(TEB, GdiTebBatch, 0x2F0);
MIME_FIELD_AT(TEB, RealClientId, 0x7D8);
MIME_FIELD_AT(TEB, Win32ClientInfo, 0x800);
MIME_FIELD_AT(TEB, LastStatusValue, 0x1250);
MIME_FIELD_AT(TEB, StaticUnicodeString, 0x1258);
MIME_FIELD_AT(TEB, DeallocationStack, 0x1478);
MIME_FIELD_AT(TEB, TlsSlots, 0x1480);
MIME_FIELD_AT(TEB, TlsLinks, 0x1680);
MIME_FIELD_AT(TEB, HardErrorMode, 0x16B0);
MIME_FIELD_AT(TEB, GdiBatchCount, 0x1740);
MIME_FIELD_AT(TEB, TlsExpansionSlots, 0x1780);
MIME_FIELD_AT(TEB, CurrentTransactionHandle, 0x17B8);
MIME_FIELD_AT(TEB, FlsData, 0x17C8);
MIME_FIELD_AT(TEB, SameTebFlags, 0x17EE);
MIME_FIELD_AT(TEB, WowTebOffset, 0x180C);
MIME_FIELD_AT(TEB, ReservedForCrt, 0x1820);
MIME_FIELD_AT(TEB, Rcu, 0x1870);

#endif

#define MIME_TEB_SIZE 0x2000
#define MIME_TEB32_SIZE 0x1000 /* follows the 64-bit teb of a wow64 thread. */

typedef struct _KSYSTEM_TIME {
    ULONG LowPart;
    LONG High1Time;
    LONG High2Time;
} KSYSTEM_TIME, *PKSYSTEM_TIME;

typedef enum _NT_PRODUCT_TYPE {
    NtProductWinNt = 1,
    NtProductLanManNt,
    NtProductServer
} NT_PRODUCT_TYPE;

#define PROCESSOR_ARCHITECTURE_AMD64 9
#define PROCESSOR_ARCHITECTURE_ARM64 12

typedef struct _KUSER_SHARED_DATA {
    ULONG TickCountLowDeprecated;
    ULONG TickCountMultiplier;
    volatile KSYSTEM_TIME InterruptTime;
    volatile KSYSTEM_TIME SystemTime;
    volatile KSYSTEM_TIME TimeZoneBias;
    USHORT ImageNumberLow;
    USHORT ImageNumberHigh;
    WCHAR NtSystemRoot[260];
    ULONG MaxStackTraceDepth;
    ULONG CryptoExponent;
    ULONG TimeZoneId;
    ULONG LargePageMinimum;
    ULONG AitSamplingValue;
    ULONG AppCompatFlag;
    ULONGLONG RNGSeedVersion;
    ULONG GlobalValidationRunlevel;
    LONG TimeZoneBiasStamp;
    ULONG NtBuildNumber;
    ULONG NtProductType;
    BOOLEAN ProductTypeIsValid;
    UCHAR Reserved0[1];
    USHORT NativeProcessorArchitecture;
    ULONG NtMajorVersion;
    ULONG NtMinorVersion;
    BOOLEAN ProcessorFeatures[64];
    ULONG Reserved1;
    ULONG Reserved3;
    volatile ULONG TimeSlip;
    ULONG AlternativeArchitecture;
    ULONG BootId;
    LARGE_INTEGER SystemExpirationDate;
    ULONG SuiteMask;
    BOOLEAN KdDebuggerEnabled;
    UCHAR MitigationPolicies;
    USHORT CyclesPerYield;
    volatile ULONG ActiveConsoleId;
    volatile ULONG DismountCount;
    ULONG ComPlusPackage;
    ULONG LastSystemRITEventTickCount;
    ULONG NumberOfPhysicalPages;
    BOOLEAN SafeBootMode;
    UCHAR VirtualizationFlags;
    UCHAR Reserved12[2];
    ULONG SharedDataFlags;
    ULONG DataFlagsPad[1];
    ULONGLONG TestRetInstruction;
    LONGLONG QpcFrequency;
    ULONG SystemCall;
    ULONG Reserved2;
    ULONGLONG FullNumberOfPhysicalPages;
    ULONGLONG SystemCallPad[1];
    union {
        volatile KSYSTEM_TIME TickCount;
        volatile ULONG64 TickCountQuad;
        ULONG ReservedTickCountOverlay[4];
    };
    ULONG Cookie;
    ULONG CookiePad[1];
    LONGLONG ConsoleSessionForegroundProcessId;
    ULONGLONG TimeUpdateLock;
    ULONGLONG BaselineSystemTimeQpc;
    ULONGLONG BaselineInterruptTimeQpc;
    ULONGLONG QpcSystemTimeIncrement;
    ULONGLONG QpcInterruptTimeIncrement;
    UCHAR QpcSystemTimeIncrementShift;
    UCHAR QpcInterruptTimeIncrementShift;
    USHORT UnparkedProcessorCount;
    ULONG EnclaveFeatureMask[4];
    ULONG TelemetryCoverageRound;
    USHORT UserModeGlobalLogger[16];
    ULONG ImageFileExecutionOptions;
    ULONG LangGenerationCount;
    volatile ULONGLONG MimeReadyThreads; /* Reserved4 on windows. nonzero while threads wait for a processor. */
    volatile ULONGLONG InterruptTimeBias;
    volatile ULONGLONG QpcBias;
    ULONG ActiveProcessorCount;
    volatile UCHAR ActiveGroupCount;
    UCHAR Reserved9;
    union {
        USHORT QpcData;
        struct {
            volatile UCHAR QpcBypassEnabled;
            UCHAR QpcShift;
        };
    };
    LARGE_INTEGER TimeZoneBiasEffectiveStart;
    LARGE_INTEGER TimeZoneBiasEffectiveEnd;
} KUSER_SHARED_DATA, *PKUSER_SHARED_DATA;
#define USER_SHARED_DATA ((volatile KUSER_SHARED_DATA *const)0x7FFE0000)

/* user mode reads the performance counter itself: the processor's counter with the kernel's bias and shift. */
#ifndef NTSYSCALLAPI
#ifdef MIME_KERNEL
#define NTSYSCALLAPI
#else
#define NTSYSCALLAPI DECLSPEC_IMPORT
#endif
#endif
NTSYSCALLAPI NTSTATUS NTAPI NtQueryPerformanceCounter(PLARGE_INTEGER counter, PLARGE_INTEGER frequency);

FORCEINLINE ULONG64 MimeReadQpc(void)
{
    ULONG64 v;
#ifdef __aarch64__
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v));
#else
    /* translated x86 code on arm64 has its own tsc, which the translator answers through this call. */
    if (USER_SHARED_DATA->NativeProcessorArchitecture != 9 /* PROCESSOR_ARCHITECTURE_AMD64 */) {
        LARGE_INTEGER counter;
        NtQueryPerformanceCounter(&counter, nullptr);
        return (ULONG64)counter.QuadPart;
    }
    ULONG lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi));
    v = (ULONG64)hi << 32 | lo;
#endif
    return (v + USER_SHARED_DATA->QpcBias) >> USER_SHARED_DATA->QpcShift;
}

FORCEINLINE ULONG64 MimeMulHigh(ULONG64 a, ULONG64 b)
{
    ULONG64 al = (ULONG)a, ah = a >> 32, bl = (ULONG)b, bh = b >> 32;
    ULONG64 low = al * bl, mid1 = ah * bl, mid2 = al * bh;
    ULONG64 carry = ((low >> 32) + (ULONG)mid1 + (ULONG)mid2) >> 32;
    return ah * bh + (mid1 >> 32) + (mid2 >> 32) + carry;
}

/*
 * system or interrupt time at counter precision: the last clock tick plus the counts since, retried while
 * TimeUpdateLock says the tick is changing.
 */
FORCEINLINE ULONG64 MimePreciseTime(BOOLEAN system)
{
    volatile KUSER_SHARED_DATA *k = USER_SHARED_DATA;
    for (;;) {
        ULONG64 sequence = __atomic_load_n(&k->TimeUpdateLock, __ATOMIC_ACQUIRE);
        if (sequence & 1)
            continue;
        volatile KSYSTEM_TIME *t = system ? &k->SystemTime : &k->InterruptTime;
        ULONG64 base = (ULONG64)(ULONG)t->High1Time << 32 | t->LowPart;
        ULONG64 baseline = system ? k->BaselineSystemTimeQpc : k->BaselineInterruptTimeQpc;
        ULONG64 increment = system ? k->QpcSystemTimeIncrement : k->QpcInterruptTimeIncrement;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&k->TimeUpdateLock, __ATOMIC_RELAXED) != sequence)
            continue;
        ULONG64 qpc = MimeReadQpc();
        return base + (qpc > baseline ? MimeMulHigh(qpc - baseline, increment) : 0);
    }
}
MIME_FIELD_AT(KUSER_SHARED_DATA, InterruptTime, 0x8);
MIME_FIELD_AT(KUSER_SHARED_DATA, SystemTime, 0x14);
MIME_FIELD_AT(KUSER_SHARED_DATA, NtSystemRoot, 0x30);
MIME_FIELD_AT(KUSER_SHARED_DATA, NtBuildNumber, 0x260);
MIME_FIELD_AT(KUSER_SHARED_DATA, NtProductType, 0x264);
MIME_FIELD_AT(KUSER_SHARED_DATA, NativeProcessorArchitecture, 0x26A);
MIME_FIELD_AT(KUSER_SHARED_DATA, NtMajorVersion, 0x26C);
MIME_FIELD_AT(KUSER_SHARED_DATA, ProcessorFeatures, 0x274);
MIME_FIELD_AT(KUSER_SHARED_DATA, NumberOfPhysicalPages, 0x2E8);
MIME_FIELD_AT(KUSER_SHARED_DATA, QpcFrequency, 0x300);
MIME_FIELD_AT(KUSER_SHARED_DATA, TickCount, 0x320);
MIME_FIELD_AT(KUSER_SHARED_DATA, Cookie, 0x330);
MIME_FIELD_AT(KUSER_SHARED_DATA, QpcBias, 0x3B8);
MIME_FIELD_AT(KUSER_SHARED_DATA, ActiveProcessorCount, 0x3C0);
MIME_FIELD_AT(KUSER_SHARED_DATA, QpcData, 0x3C6);

#define MIME_NT_BUILD_NUMBER 26300
#define MIME_NT_MAJOR_VERSION 10
#define MIME_NT_MINOR_VERSION 0

/* the system information classes mime implements. */
typedef enum _SYSTEM_INFORMATION_CLASS {
    SystemBasicInformation = 0,
    SystemProcessorInformation = 1,
    SystemPerformanceInformation = 2,
    SystemTimeOfDayInformation = 3,
    SystemProcessInformation = 5,
    SystemProcessorPerformanceInformation = 8,
    SystemModuleInformation = 11,
    SystemPageFileInformation = 18,
    SystemHandleInformation = 16,
    SystemFileCacheInformation = 21,
    SystemKernelDebuggerInformation = 35,
    SystemRangeStartInformation = 50,
    SystemNumaHighestNodeInformation = 55,
    SystemExtendedHandleInformation = 64,
    SystemEmulationBasicInformation = 62,
    SystemEmulationProcessorInformation = 63,
    SystemLogicalProcessorInformation = 73,
    SystemFirmwareTableInformation = 76,
    SystemBootEnvironmentInformation = 90,
    SystemSecureBootInformation = 145,
    SystemProcessorFeaturesInformation = 154,
    SystemHypervisorDetailInformation = 185,
} SYSTEM_INFORMATION_CLASS;

/* SystemFirmwareTableInformation, as in wdm.h. */
#define SystemFirmwareTable_Enumerate 0
#define SystemFirmwareTable_Get 1

typedef struct _SYSTEM_FIRMWARE_TABLE_INFORMATION {
    ULONG ProviderSignature;
    ULONG Action;
    ULONG TableId;
    ULONG TableBufferLength;
    UCHAR TableBuffer[1];
} SYSTEM_FIRMWARE_TABLE_INFORMATION, *PSYSTEM_FIRMWARE_TABLE_INFORMATION;

/* what GetSystemFirmwareTable returns for the RSMB provider. */
typedef struct _RAW_SMBIOS_DATA {
    UCHAR Used20CallingMethod;
    UCHAR SMBIOSMajorVersion;
    UCHAR SMBIOSMinorVersion;
    UCHAR DmiRevision;
    ULONG Length;
    UCHAR SMBIOSTableData[1];
} RAW_SMBIOS_DATA, *PRAW_SMBIOS_DATA;

typedef struct _SYSTEM_BASIC_INFORMATION {
    ULONG Reserved;
    ULONG TimerResolution;
    ULONG PageSize;
    ULONG NumberOfPhysicalPages;
    ULONG LowestPhysicalPageNumber;
    ULONG HighestPhysicalPageNumber;
    ULONG AllocationGranularity;
    ULONG_PTR MinimumUserModeAddress;
    ULONG_PTR MaximumUserModeAddress;
    ULONG_PTR ActiveProcessorsAffinityMask;
    CCHAR NumberOfProcessors;
} SYSTEM_BASIC_INFORMATION, *PSYSTEM_BASIC_INFORMATION;

/* SystemProcessInformation: one record per process, followed by its threads and image name. */
typedef struct _SYSTEM_THREAD_INFORMATION {
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER CreateTime;
    ULONG WaitTime;
    PVOID StartAddress;
    CLIENT_ID ClientId;
    LONG Priority;
    LONG BasePriority;
    ULONG ContextSwitches;
    ULONG ThreadState;
    ULONG WaitReason;
} SYSTEM_THREAD_INFORMATION, *PSYSTEM_THREAD_INFORMATION;

typedef struct _SYSTEM_PROCESS_INFORMATION {
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;
    LARGE_INTEGER WorkingSetPrivateSize;
    ULONG HardFaultCount;
    ULONG NumberOfThreadsHighWatermark;
    ULONGLONG CycleTime;
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER KernelTime;
    UNICODE_STRING ImageName;
    LONG BasePriority;
    HANDLE UniqueProcessId;
    HANDLE InheritedFromUniqueProcessId;
    ULONG HandleCount;
    ULONG SessionId;
    ULONG_PTR UniqueProcessKey;
    SIZE_T PeakVirtualSize;
    SIZE_T VirtualSize;
    ULONG PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivatePageCount;
    LARGE_INTEGER ReadOperationCount;
    LARGE_INTEGER WriteOperationCount;
    LARGE_INTEGER OtherOperationCount;
    LARGE_INTEGER ReadTransferCount;
    LARGE_INTEGER WriteTransferCount;
    LARGE_INTEGER OtherTransferCount;
} SYSTEM_PROCESS_INFORMATION, *PSYSTEM_PROCESS_INFORMATION;

typedef struct _SYSTEM_PROCESSOR_INFORMATION {
    USHORT ProcessorArchitecture;
    USHORT ProcessorLevel;
    USHORT ProcessorRevision;
    USHORT MaximumProcessors;
    ULONG ProcessorFeatureBits;
} SYSTEM_PROCESSOR_INFORMATION, *PSYSTEM_PROCESSOR_INFORMATION;

typedef enum _PROCESSINFOCLASS {
    ProcessBasicInformation = 0,
    ProcessQuotaLimits = 1,
    ProcessIoCounters = 2,
    ProcessVmCounters = 3,
    ProcessTimes = 4,
    ProcessBasePriority = 5,
    ProcessDebugPort = 7,
    ProcessAccessToken = 9,
    ProcessDefaultHardErrorMode = 12,
    ProcessPriorityClass = 18,
    ProcessHandleCount = 20,
    ProcessWorkingSetWatch = 15,
    ProcessAffinityMask = 21,
    ProcessPriorityBoost = 22,
    ProcessSessionInformation = 24,
    ProcessWow64Information = 26,
    ProcessImageFileName = 27,
    ProcessDebugFlags = 31,
    ProcessExecuteFlags = 34,
    ProcessCookie = 36,
    ProcessWorkingSetWatchEx = 42,
    ProcessImageInformation = 37,
    ProcessImageFileNameWin32 = 43,
    ProcessMitigationPolicy = 52,
    ProcessHandleTable = 58,
    ProcessProtectionInformation = 61,
} PROCESSINFOCLASS;

/* like ProcessPriorityClass, with a flag word saying whether to set the class, the foreground flag or both. */
#define MX_PROCESS_PRIORITY_CLASS_EX 108
#define PROCESS_PRIORITY_CLASS_EX_FOREGROUND 1
#define PROCESS_PRIORITY_CLASS_EX_CLASS 2
enum {
    PROCESS_PRIORITY_CLASS_UNKNOWN = 0,
    PROCESS_PRIORITY_CLASS_IDLE = 1,
    PROCESS_PRIORITY_CLASS_NORMAL = 2,
    PROCESS_PRIORITY_CLASS_HIGH = 3,
    PROCESS_PRIORITY_CLASS_REALTIME = 4,
    PROCESS_PRIORITY_CLASS_BELOW_NORMAL = 5,
    PROCESS_PRIORITY_CLASS_ABOVE_NORMAL = 6,
};
typedef struct _PROCESS_PRIORITY_CLASS {
    BOOLEAN Foreground;
    UCHAR PriorityClass;
} PROCESS_PRIORITY_CLASS, *PPROCESS_PRIORITY_CLASS;

/*
 * the values of SetProcessShutdownParameters. windows keeps them in csrss, so this class is mime's own.
 * processes with a higher level shut down first.
 */
#define MX_PROCESS_SHUTDOWN_PARAMETERS 109
#define MX_SHUTDOWN_LEVEL_DEFAULT 0x280
#define MX_SHUTDOWN_NORETRY 1
/* services.exe, then the processes it starts, shut down after the programs. */
#define MX_SHUTDOWN_LEVEL_SERVICES 0x1FF
#define MX_SHUTDOWN_LEVEL_SERVICE_PROCESS 0x100
/* extra time services.exe gets on top of WaitToKillServiceTimeout for preshutdown controls. a guess. */
#define MX_SHUTDOWN_PRESHUTDOWN_MS 30000
/* csrss fails flags other than SHUTDOWN_NORETRY with this, which maps to error 641. */
#define MX_STATUS_BAD_SHUTDOWN_FLAGS ((NTSTATUS)0xC00002EBL)
typedef struct _MX_SHUTDOWN_PARAMETERS {
    ULONG Level;
    ULONG Flags;
} MX_SHUTDOWN_PARAMETERS;

typedef enum _SHUTDOWN_ACTION {
    ShutdownNoReboot = 0,
    ShutdownReboot = 1,
    ShutdownPowerOff = 2,
} SHUTDOWN_ACTION;

/* ExitWindowsEx flags, for the kernel's own shutdown. */
#ifndef EWX_LOGOFF
#define EWX_LOGOFF 0x00000000
#define EWX_SHUTDOWN 0x00000001
#define EWX_REBOOT 0x00000002
#define EWX_FORCE 0x00000004
#define EWX_POWEROFF 0x00000008
#define EWX_FORCEIFHUNG 0x00000010
#define EWX_QUICKRESOLVE 0x00000020
#define EWX_RESTARTAPPS 0x00000040
#define EWX_HYBRID_SHUTDOWN 0x00400000
#define EWX_BOOTOPTIONS 0x01000000
#endif
/* flags that shut the machine down rather than log off, and every flag a request may carry. */
#define MX_EWX_MACHINE (EWX_SHUTDOWN | EWX_REBOOT | EWX_POWEROFF | EWX_HYBRID_SHUTDOWN)
#define MX_EWX_VALID (MX_EWX_MACHINE | EWX_FORCE | EWX_FORCEIFHUNG | EWX_QUICKRESOLVE | EWX_RESTARTAPPS | EWX_BOOTOPTIONS)

#ifndef STATUS_CANT_WAIT
#define STATUS_CANT_WAIT ((NTSTATUS)0xC00000D8L)
#endif
#ifndef STATUS_DEVICE_BUSY
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011L)
#endif
#ifndef STATUS_SHUTDOWN_IN_PROGRESS
#define STATUS_SHUTDOWN_IN_PROGRESS ((NTSTATUS)0xC00002FEL)
#endif
#ifndef STATUS_RETRY
#define STATUS_RETRY ((NTSTATUS)0xC000022DL)
#endif

/*
 * the kernel runs shutdowns, the job of csrss and winlogon on windows. this NtSetSystemInformation class stands
 * for the call InitiateSystemShutdownEx makes to winlogon, and 0 for the one that stops it.
 */
#define MX_SYSTEM_SHUTDOWN_REQUEST 0x7F000001u
#define MX_SHUTDOWN_ABORT 1u      /* AbortSystemShutdown */
#define MX_SHUTDOWN_IMMEDIATE 2u  /* no notification, every process ends at once. */
#define MX_SHUTDOWN_RETRY_FORCED 4u /* a refusing window does not stop it: programs are asked again, forced. */
#define MX_SHUTDOWN_REASON_HOST 0x80000000u /* planned, other: what `mime down` gives. */
typedef struct _MX_SHUTDOWN_REQUEST {
    ULONG Flags;      /* EWX_* */
    ULONG Reason;     /* SHTDN_REASON_* */
    ULONG Delay;      /* seconds before it starts, during which AbortSystemShutdown can stop it. */
    ULONG Control;    /* MX_SHUTDOWN_* */
} MX_SHUTDOWN_REQUEST;

typedef struct _PROCESS_BASIC_INFORMATION {
    NTSTATUS ExitStatus;
    PPEB PebBaseAddress;
    ULONG_PTR AffinityMask;
    KPRIORITY BasePriority;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR InheritedFromUniqueProcessId;
} PROCESS_BASIC_INFORMATION, *PPROCESS_BASIC_INFORMATION;

/* NtCreateUserProcess attributes; mime only knows the primary token. */
#define PS_ATTRIBUTE_TOKEN 0x00060002u
typedef struct _PS_ATTRIBUTE {
    ULONG_PTR Attribute;
    SIZE_T Size;
    union {
        ULONG_PTR Value;
        PVOID ValuePtr;
    };
    PSIZE_T ReturnLength;
} PS_ATTRIBUTE, *PPS_ATTRIBUTE;

typedef struct _PS_ATTRIBUTE_LIST {
    SIZE_T TotalLength;
    PS_ATTRIBUTE Attributes[1];
} PS_ATTRIBUTE_LIST, *PPS_ATTRIBUTE_LIST;

typedef enum _THREADINFOCLASS {
    ThreadBasicInformation = 0,
    ThreadTimes = 1,
    ThreadPriority = 2,
    ThreadBasePriority = 3,
    ThreadAffinityMask = 4,
    ThreadImpersonationToken = 5,
    ThreadEnableAlignmentFaultFixup = 7,
    ThreadQuerySetWin32StartAddress = 9,
    ThreadZeroTlsCell = 10,
    ThreadAmILastThread = 12,
    ThreadIdealProcessor = 13,
    ThreadPriorityBoost = 14,
    ThreadIsTerminated = 20,
    ThreadHideFromDebugger = 17,
    ThreadIdealProcessorEx = 33,
    ThreadDescription = 38,
    ThreadDynamicCodePolicyInfo = 42,
} THREADINFOCLASS;

typedef struct _THREAD_BASIC_INFORMATION {
    NTSTATUS ExitStatus;
    PTEB TebBaseAddress;
    CLIENT_ID ClientId;
    KAFFINITY AffinityMask;
    KPRIORITY Priority;
    KPRIORITY BasePriority;
} THREAD_BASIC_INFORMATION, *PTHREAD_BASIC_INFORMATION;

typedef struct _INITIAL_TEB {
    PVOID OldStackBase;
    PVOID OldStackLimit;
    PVOID StackBase;
    PVOID StackLimit;
    PVOID StackAllocationBase;
} INITIAL_TEB, *PINITIAL_TEB;

typedef enum _OBJECT_INFORMATION_CLASS {
    ObjectBasicInformation = 0,
    ObjectNameInformation = 1,
    ObjectTypeInformation = 2,
} OBJECT_INFORMATION_CLASS;

typedef struct _OBJECT_NAME_INFORMATION {
    UNICODE_STRING Name;
} OBJECT_NAME_INFORMATION, *POBJECT_NAME_INFORMATION;

/* win32k callbacks dispatched through PEB.KernelCallbackTable. */
typedef NTSTATUS(NTAPI *PKERNEL_CALLBACK_PROC)(PVOID Arguments, ULONG ArgumentLength);

/* nt string helpers common to kernel and user mode. */
#define UNICODE_NULL ((WCHAR)0)

#ifndef MIME_KERNEL
FORCEINLINE PPEB NtCurrentPeb(void)
{
    return NtCurrentTeb()->ProcessEnvironmentBlock;
}
#endif

#endif
