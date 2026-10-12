/*
 * core nt definitions shared by the mime kernel and user mode.
 * layouts follow 64 bit windows (identical on x64 and arm64) wherever
 * user mode code or drivers can observe them; offsets are asserted.
 */
#ifndef MIME_NT_H
#define MIME_NT_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define MIME_STATIC_ASSERT(e, m) _Static_assert(e, m)
#define MIME_FIELD_AT(type, field, off) MIME_STATIC_ASSERT(offsetof(type, field) == (off), #type "." #field)

#define NTAPI
#define WINAPI
#define CALLBACK
#define APIENTRY
#define CDECL
#define FASTCALL
#define DECLSPEC_IMPORT __declspec(dllimport)
#define DECLSPEC_EXPORT __declspec(dllexport)
#define DECLSPEC_NORETURN __declspec(noreturn)
#define DECLSPEC_ALIGN(x) __declspec(align(x))
#define FORCEINLINE static inline __attribute__((always_inline))
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
typedef unsigned int UINT, *PUINT;
typedef long LONG, *PLONG, *LPLONG;
typedef unsigned long ULONG, *PULONG, DWORD, *PDWORD, *LPDWORD;
typedef long long LONGLONG, *PLONGLONG, LONG64, INT64;
typedef unsigned long long ULONGLONG, *PULONGLONG, ULONG64, *PULONG64, DWORD64, *PDWORD64, UINT64, DWORDLONG;
typedef int64_t LONG_PTR, *PLONG_PTR, INT_PTR, SSIZE_T, *PSSIZE_T, LPARAM, LRESULT;
typedef uint64_t ULONG_PTR, *PULONG_PTR, UINT_PTR, SIZE_T, *PSIZE_T, DWORD_PTR, WPARAM, KAFFINITY;
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
#define STATUS_PENDING ((NTSTATUS)0x00000103L)
#define STATUS_REPARSE ((NTSTATUS)0x00000104L)
#define STATUS_MORE_ENTRIES ((NTSTATUS)0x00000105L)
#define STATUS_NOT_ALL_ASSIGNED ((NTSTATUS)0x00000106L)
#define STATUS_SOME_NOT_MAPPED ((NTSTATUS)0x00000107L)
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
#define STATUS_CONFLICTING_ADDRESSES ((NTSTATUS)0xC0000018L)
#define STATUS_NOT_MAPPED_VIEW ((NTSTATUS)0xC0000019L)
#define STATUS_UNABLE_TO_FREE_VM ((NTSTATUS)0xC000001AL)
#define STATUS_ILLEGAL_INSTRUCTION ((NTSTATUS)0xC000001DL)
#define STATUS_INVALID_SYSTEM_SERVICE ((NTSTATUS)0xC000001CL)
#define STATUS_INVALID_VIEW_SIZE ((NTSTATUS)0xC000001FL)
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
#define STATUS_INTERNAL_ERROR ((NTSTATUS)0xC00000E5L)
#define STATUS_DIRECTORY_NOT_EMPTY ((NTSTATUS)0xC0000101L)
#define STATUS_NAME_TOO_LONG ((NTSTATUS)0xC0000106L)
#define STATUS_NOT_A_DIRECTORY ((NTSTATUS)0xC0000103L)
#define STATUS_PROCESS_IS_TERMINATING ((NTSTATUS)0xC000010AL)
#define STATUS_INVALID_IMAGE_NOT_MZ ((NTSTATUS)0xC000012FL)
#define STATUS_DLL_NOT_FOUND ((NTSTATUS)0xC0000135L)
#define STATUS_ENTRYPOINT_NOT_FOUND ((NTSTATUS)0xC0000139L)
#define STATUS_ORDINAL_NOT_FOUND ((NTSTATUS)0xC0000138L)
#define STATUS_CONTROL_C_EXIT ((NTSTATUS)0xC000013AL)
#define STATUS_DLL_INIT_FAILED ((NTSTATUS)0xC0000142L)
#define STATUS_NO_TOKEN ((NTSTATUS)0xC000007CL)
#define STATUS_SYSTEM_SHUTDOWN ((NTSTATUS)0xC00002EBL)
#define STATUS_CALLBACK_POP_STACK ((NTSTATUS)0xC0000423L)
#define STATUS_STACK_OVERFLOW ((NTSTATUS)0xC00000FDL)
#define STATUS_INTEGER_DIVIDE_BY_ZERO ((NTSTATUS)0xC0000094L)
#define STATUS_PRIVILEGED_INSTRUCTION ((NTSTATUS)0xC0000096L)
#define STATUS_NOT_FOUND ((NTSTATUS)0xC0000225L)
#define STATUS_STACK_BUFFER_OVERRUN ((NTSTATUS)0xC0000409L)
#define STATUS_ASSERTION_FAILURE ((NTSTATUS)0xC0000420L)
#define STATUS_INVALID_ADDRESS ((NTSTATUS)0xC0000141L)
#define STATUS_RESOURCE_DATA_NOT_FOUND ((NTSTATUS)0xC0000089L)
#define STATUS_RESOURCE_TYPE_NOT_FOUND ((NTSTATUS)0xC000008AL)
#define STATUS_RESOURCE_NAME_NOT_FOUND ((NTSTATUS)0xC000008BL)
#define STATUS_RESOURCE_LANG_NOT_FOUND ((NTSTATUS)0xC0000204L)
#define STATUS_VARIABLE_NOT_FOUND ((NTSTATUS)0xC0000100L)
#define STATUS_INVALID_PARAMETER_1 ((NTSTATUS)0xC00000EFL)
#define STATUS_INVALID_PARAMETER_2 ((NTSTATUS)0xC00000F0L)
#define STATUS_INVALID_PARAMETER_3 ((NTSTATUS)0xC00000F1L)
#define STATUS_INVALID_PARAMETER_4 ((NTSTATUS)0xC00000F2L)
#define STATUS_INVALID_FILE_FOR_SECTION ((NTSTATUS)0xC0000020L)

/* doubly linked lists. */
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
        (p)->SecurityQualityOfService = NULL;                                                                     \
    } while (0)

#define MAX_PATH 260

#define NtCurrentProcess() ((HANDLE)(LONG_PTR)-1)
#define NtCurrentThread() ((HANDLE)(LONG_PTR)-2)

#ifndef MIME_KERNEL
FORCEINLINE struct _TEB *NtCurrentTeb(void)
{
    struct _TEB *teb;
#if defined(__x86_64__)
    /* x64 code finds its teb through gs. */
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
#define FILE_SYNCHRONOUS_IO_ALERT 0x00000010
#define FILE_SYNCHRONOUS_IO_NONALERT 0x00000020
#define FILE_NON_DIRECTORY_FILE 0x00000040
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
    FileDispositionInformationEx = 64,
    FileMaximumInformation
} FILE_INFORMATION_CLASS;

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

typedef struct _FILE_NAME_INFORMATION {
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_NAME_INFORMATION, *PFILE_NAME_INFORMATION;

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
    FileFsSizeInformation = 3,
    FileFsDeviceInformation = 4,
    FileFsAttributeInformation = 5,
    FileFsFullSizeInformation = 7,
} FS_INFORMATION_CLASS;

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

#define MEM_COMMIT 0x00001000
#define MEM_RESERVE 0x00002000
#define MEM_DECOMMIT 0x00004000
#define MEM_RELEASE 0x00008000
#define MEM_FREE 0x00010000
#define MEM_PRIVATE 0x00020000
#define MEM_MAPPED 0x00040000
#define MEM_RESET 0x00080000
#define MEM_TOP_DOWN 0x00100000
#define MEM_WRITE_WATCH 0x00200000
#define MEM_PHYSICAL 0x00400000
#define MEM_RESET_UNDO 0x01000000
#define MEM_LARGE_PAGES 0x20000000
#define MEM_IMAGE 0x01000000

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
    MemoryImageInformation = 6,
} MEMORY_INFORMATION_CLASS;

typedef struct _MEMORY_BASIC_INFORMATION {
    PVOID BaseAddress;
    PVOID AllocationBase;
    DWORD AllocationProtect;
    WORD PartitionId;
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
} MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;

/* process, thread and synchronisation rights. */
#define PROCESS_TERMINATE 0x0001
#define PROCESS_CREATE_THREAD 0x0002
#define PROCESS_VM_OPERATION 0x0008
#define PROCESS_VM_READ 0x0010
#define PROCESS_VM_WRITE 0x0020
#define PROCESS_DUP_HANDLE 0x0040
#define PROCESS_CREATE_PROCESS 0x0080
#define PROCESS_SET_INFORMATION 0x0200
#define PROCESS_QUERY_INFORMATION 0x0400
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#define PROCESS_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0xFFFF)
#define THREAD_TERMINATE 0x0001
#define THREAD_SUSPEND_RESUME 0x0002
#define THREAD_GET_CONTEXT 0x0008
#define THREAD_SET_CONTEXT 0x0010
#define THREAD_QUERY_INFORMATION 0x0040
#define THREAD_SET_INFORMATION 0x0020
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
} IMAGE_NT_HEADERS64, *PIMAGE_NT_HEADERS64, IMAGE_NT_HEADERS, *PIMAGE_NT_HEADERS;

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

typedef struct _IMAGE_TLS_DIRECTORY64 {
    ULONGLONG StartAddressOfRawData;
    ULONGLONG EndAddressOfRawData;
    ULONGLONG AddressOfIndex;
    ULONGLONG AddressOfCallBacks;
    ULONG SizeOfZeroFill;
    ULONG Characteristics;
} IMAGE_TLS_DIRECTORY64, *PIMAGE_TLS_DIRECTORY64;

typedef struct _IMAGE_RESOURCE_DIRECTORY {
    ULONG Characteristics;
    ULONG TimeDateStamp;
    USHORT MajorVersion;
    USHORT MinorVersion;
    USHORT NumberOfNamedEntries;
    USHORT NumberOfIdEntries;
} IMAGE_RESOURCE_DIRECTORY, *PIMAGE_RESOURCE_DIRECTORY;

typedef struct _IMAGE_RESOURCE_DIRECTORY_ENTRY {
    ULONG Name; /* high bit: name is an offset to IMAGE_RESOURCE_DIR_STRING_U */
    ULONG OffsetToData; /* high bit: points to a subdirectory */
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

/* x64 thread context, the context of x64 programs run by the translator. */
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

/* the context of the code being compiled. */
#if defined(__x86_64__)
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
MIME_FIELD_AT(RTL_USER_PROCESS_PARAMETERS, CurrentDirectory, 0x38);
MIME_FIELD_AT(RTL_USER_PROCESS_PARAMETERS, ImagePathName, 0x60);
MIME_FIELD_AT(RTL_USER_PROCESS_PARAMETERS, CommandLine, 0x70);
MIME_FIELD_AT(RTL_USER_PROCESS_PARAMETERS, Environment, 0x80);
MIME_FIELD_AT(RTL_USER_PROCESS_PARAMETERS, WindowTitle, 0xB0);
MIME_FIELD_AT(RTL_USER_PROCESS_PARAMETERS, EnvironmentSize, 0x3F0);

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
MIME_FIELD_AT(PEB_LDR_DATA, InLoadOrderModuleList, 0x10);
MIME_FIELD_AT(PEB_LDR_DATA, InInitializationOrderModuleList, 0x30);

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
MIME_FIELD_AT(LDR_DATA_TABLE_ENTRY, DllBase, 0x30);
MIME_FIELD_AT(LDR_DATA_TABLE_ENTRY, FullDllName, 0x48);
MIME_FIELD_AT(LDR_DATA_TABLE_ENTRY, BaseDllName, 0x58);
MIME_FIELD_AT(LDR_DATA_TABLE_ENTRY, HashLinks, 0x70);
MIME_FIELD_AT(LDR_DATA_TABLE_ENTRY, OriginalBase, 0xF8);

typedef struct _PEB {
    BOOLEAN InheritedAddressSpace;
    BOOLEAN ReadImageFileExecOptions;
    BOOLEAN BeingDebugged;
    UCHAR BitField;
    UCHAR Padding0[4];
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
    UCHAR Padding1[4];
    PVOID KernelCallbackTable;
    ULONG SystemReserved;
    ULONG AtlThunkSListPtr32;
    PVOID ApiSetMap;
    ULONG TlsExpansionCounter;
    UCHAR Padding2[4];
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
    UCHAR Padding3[4];
    PVOID LoaderLock;
    ULONG OSMajorVersion;
    ULONG OSMinorVersion;
    USHORT OSBuildNumber;
    USHORT OSCSDVersion;
    ULONG OSPlatformId;
    ULONG ImageSubsystem;
    ULONG ImageSubsystemMajorVersion;
    ULONG ImageSubsystemMinorVersion;
    UCHAR Padding4[4];
    ULONG_PTR ActiveProcessAffinityMask;
    ULONG GdiHandleBuffer[60];
    PVOID PostProcessInitRoutine;
    PVOID TlsExpansionBitmap;
    ULONG TlsExpansionBitmapBits[32];
    ULONG SessionId;
    UCHAR Padding5[4];
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
    UCHAR Padding6[4];
    ULONGLONG CsrServerReadOnlySharedMemoryBase;
    ULONG_PTR TppWorkerpListLock;
    LIST_ENTRY TppWorkerpList;
    PVOID WaitOnAddressHashTable[128];
} PEB, *PPEB;
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

typedef struct _GDI_TEB_BATCH {
    ULONG Offset;
    ULONG_PTR HDC;
    ULONG Buffer[310];
} GDI_TEB_BATCH;

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
MIME_FIELD_AT(TEB, ExceptionCode, 0x2C0);
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
MIME_FIELD_AT(TEB, FlsData, 0x17C8);
MIME_FIELD_AT(TEB, SameTebFlags, 0x17EE);
MIME_FIELD_AT(TEB, ReservedForCrt, 0x1820);
MIME_FIELD_AT(TEB, Rcu, 0x1870);

#define MIME_TEB_SIZE 0x2000

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
    ULONGLONG Reserved4;
    volatile ULONGLONG InterruptTimeBias;
    volatile ULONGLONG QpcBias;
    ULONG ActiveProcessorCount;
    volatile UCHAR ActiveGroupCount;
    UCHAR Reserved9;
    union {
        USHORT QpcData;
        struct {
            volatile UCHAR QpcBypassEnabled;
            UCHAR QpcReserved;
        };
    };
    LARGE_INTEGER TimeZoneBiasEffectiveStart;
    LARGE_INTEGER TimeZoneBiasEffectiveEnd;
} KUSER_SHARED_DATA, *PKUSER_SHARED_DATA;
#define USER_SHARED_DATA ((volatile KUSER_SHARED_DATA *const)0x7FFE0000)
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

/* system information classes we implement. */
typedef enum _SYSTEM_INFORMATION_CLASS {
    SystemBasicInformation = 0,
    SystemProcessorInformation = 1,
    SystemPerformanceInformation = 2,
    SystemTimeOfDayInformation = 3,
    SystemProcessInformation = 5,
    SystemModuleInformation = 11,
    SystemKernelDebuggerInformation = 35,
    SystemRangeStartInformation = 50,
    SystemNumaHighestNodeInformation = 55,
    SystemEmulationBasicInformation = 62,
    SystemEmulationProcessorInformation = 63,
    SystemLogicalProcessorInformation = 73,
    SystemFirmwareTableInformation = 76,
    SystemProcessorFeaturesInformation = 154,
    SystemHypervisorDetailInformation = 185,
} SYSTEM_INFORMATION_CLASS;

/* the request and answer of SystemFirmwareTableInformation, as in wdm.h. */
#define SystemFirmwareTable_Enumerate 0
#define SystemFirmwareTable_Get 1

typedef struct _SYSTEM_FIRMWARE_TABLE_INFORMATION {
    ULONG ProviderSignature;
    ULONG Action;
    ULONG TableId;
    ULONG TableBufferLength;
    UCHAR TableBuffer[1];
} SYSTEM_FIRMWARE_TABLE_INFORMATION, *PSYSTEM_FIRMWARE_TABLE_INFORMATION;

/*
 * the raw smbios data GetSystemFirmwareTable returns for the RSMB provider.
 */
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
    ProcessTimes = 4,
    ProcessDebugPort = 7,
    ProcessDefaultHardErrorMode = 12,
    ProcessSessionInformation = 24,
    ProcessWow64Information = 26,
    ProcessImageFileName = 27,
    ProcessExecuteFlags = 34,
    ProcessCookie = 36,
    ProcessImageInformation = 37,
    ProcessImageFileNameWin32 = 43,
    ProcessDebugFlags = 31,
} PROCESSINFOCLASS;

typedef struct _PROCESS_BASIC_INFORMATION {
    NTSTATUS ExitStatus;
    PPEB PebBaseAddress;
    ULONG_PTR AffinityMask;
    KPRIORITY BasePriority;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR InheritedFromUniqueProcessId;
} PROCESS_BASIC_INFORMATION, *PPROCESS_BASIC_INFORMATION;

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
    ThreadIsTerminated = 20,
    ThreadHideFromDebugger = 17,
    ThreadDescription = 38,
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

/* nt string helpers common to kernel and user mode (rtl). */
#define UNICODE_NULL ((WCHAR)0)

#ifndef MIME_KERNEL
FORCEINLINE PPEB NtCurrentPeb(void)
{
    return NtCurrentTeb()->ProcessEnvironmentBlock;
}
#endif

#endif
