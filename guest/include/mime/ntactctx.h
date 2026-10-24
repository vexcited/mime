/* activation contexts as ntdll.dll exports them: the per thread activation stack, section lookups and queries. */
#ifndef MIME_NTACTCTX_H
#define MIME_NTACTCTX_H

#include <mime/ntrtl.h>

#define STATUS_SXS_SECTION_NOT_FOUND ((NTSTATUS)0xC0150001)
#define STATUS_SXS_CANT_GEN_ACTCTX ((NTSTATUS)0xC0150002)
#define STATUS_SXS_INVALID_ACTCTXDATA_FORMAT ((NTSTATUS)0xC0150003)
#define STATUS_SXS_ASSEMBLY_NOT_FOUND ((NTSTATUS)0xC0150004)
#define STATUS_SXS_MANIFEST_FORMAT_ERROR ((NTSTATUS)0xC0150005)
#define STATUS_SXS_MANIFEST_PARSE_ERROR ((NTSTATUS)0xC0150006)
#define STATUS_SXS_ACTIVATION_CONTEXT_DISABLED ((NTSTATUS)0xC0150007)
#define STATUS_SXS_KEY_NOT_FOUND ((NTSTATUS)0xC0150008)
#define STATUS_SXS_VERSION_CONFLICT ((NTSTATUS)0xC0150009)
#define STATUS_SXS_WRONG_SECTION_TYPE ((NTSTATUS)0xC015000A)
#define STATUS_SXS_THREAD_QUERIES_DISABLED ((NTSTATUS)0xC015000B)
#define STATUS_SXS_ASSEMBLY_MISSING ((NTSTATUS)0xC015000C)
#define STATUS_SXS_PROCESS_DEFAULT_ALREADY_SET ((NTSTATUS)0xC015000E)
#define STATUS_SXS_EARLY_DEACTIVATION ((NTSTATUS)0xC015000F)
#define STATUS_SXS_INVALID_DEACTIVATION ((NTSTATUS)0xC0150010)
#define STATUS_SXS_MULTIPLE_DEACTIVATION ((NTSTATUS)0xC0150011)
#define STATUS_SXS_SYSTEM_DEFAULT_ACTIVATION_CONTEXT_EMPTY ((NTSTATUS)0xC0150012)
#define STATUS_SXS_PROCESS_TERMINATION_REQUESTED ((NTSTATUS)0xC0150013)
#define STATUS_SXS_CORRUPT_ACTIVATION_STACK ((NTSTATUS)0xC0150014)
#define STATUS_SXS_CORRUPTION ((NTSTATUS)0xC0150015)
#ifndef STATUS_INVALID_PARAMETER_1
#define STATUS_INVALID_PARAMETER_1 ((NTSTATUS)0xC00000EF)
#endif
#ifndef STATUS_INVALID_PARAMETER_2
#define STATUS_INVALID_PARAMETER_2 ((NTSTATUS)0xC00000F0)
#endif
#ifndef STATUS_INVALID_PARAMETER_3
#define STATUS_INVALID_PARAMETER_3 ((NTSTATUS)0xC00000F1)
#endif
#ifndef STATUS_INVALID_PARAMETER_4
#define STATUS_INVALID_PARAMETER_4 ((NTSTATUS)0xC00000F2)
#endif
#ifndef STATUS_INVALID_PARAMETER_5
#define STATUS_INVALID_PARAMETER_5 ((NTSTATUS)0xC00000F3)
#endif
#ifndef STATUS_INVALID_PARAMETER_6
#define STATUS_INVALID_PARAMETER_6 ((NTSTATUS)0xC00000F4)
#endif

/* pseudo handles. NULL is the process default. */
#define ACTCTX_EMPTY ((HANDLE)(LONG_PTR)-3)
#define ACTCTX_SYSTEM_DEFAULT ((HANDLE)(LONG_PTR)-4)

#define ACTIVATION_CONTEXT_SECTION_ASSEMBLY_INFORMATION 1
#define ACTIVATION_CONTEXT_SECTION_DLL_REDIRECTION 2
#define ACTIVATION_CONTEXT_SECTION_WINDOW_CLASS_REDIRECTION 3
#define ACTIVATION_CONTEXT_SECTION_COM_SERVER_REDIRECTION 4
#define ACTIVATION_CONTEXT_SECTION_COM_INTERFACE_REDIRECTION 5
#define ACTIVATION_CONTEXT_SECTION_COM_TYPE_LIBRARY_REDIRECTION 6
#define ACTIVATION_CONTEXT_SECTION_COM_PROGID_REDIRECTION 7
#define ACTIVATION_CONTEXT_SECTION_GLOBAL_OBJECT_RENAME_TABLE 8
#define ACTIVATION_CONTEXT_SECTION_CLR_SURROGATES 9
#define ACTIVATION_CONTEXT_SECTION_APPLICATION_SETTINGS 10
#define ACTIVATION_CONTEXT_SECTION_COMPATIBILITY_INFO 11
#define ACTIVATION_CONTEXT_SECTION_WINRT_ACTIVATABLE_CLASSES 12

#define FIND_ACTIVATION_CONTEXT_SECTION_KEY_RETURN_ACTIVATION_CONTEXT 0x00000001
#define FIND_ACTIVATION_CONTEXT_SECTION_KEY_RETURN_FLAGS 0x00000002
#define FIND_ACTIVATION_CONTEXT_SECTION_KEY_RETURN_ASSEMBLY_METADATA 0x00000004

#define ACTIVATION_CONTEXT_SECTION_KEYED_DATA_FLAG_FOUND_IN_PROCESS_DEFAULT 0x00000001
#define ACTIVATION_CONTEXT_SECTION_KEYED_DATA_FLAG_FOUND_IN_SYSTEM_DEFAULT 0x00000002

#define RTL_ACTIVATE_ACTIVATION_CONTEXT_EX_FLAG_RELEASE_ON_STACK_DEALLOCATION 0x00000001
#define RTL_DEACTIVATE_ACTIVATION_CONTEXT_FLAG_FORCE_EARLY_DEACTIVATION 0x00000001

#define RTL_QUERY_INFORMATION_ACTIVATION_CONTEXT_FLAG_USE_ACTIVE_ACTIVATION_CONTEXT 0x00000001
#define RTL_QUERY_INFORMATION_ACTIVATION_CONTEXT_FLAG_ACTIVATION_CONTEXT_IS_MODULE 0x00000002
#define RTL_QUERY_INFORMATION_ACTIVATION_CONTEXT_FLAG_ACTIVATION_CONTEXT_IS_ADDRESS 0x00000004
#define RTL_QUERY_INFORMATION_ACTIVATION_CONTEXT_FLAG_NO_ADDREF 0x80000000

/* notification types. */
#define ACTIVATION_CONTEXT_NOTIFICATION_DESTROY 1
#define ACTIVATION_CONTEXT_NOTIFICATION_ZOMBIFY 2
#define ACTIVATION_CONTEXT_NOTIFICATION_USED 3

typedef struct _ACTIVATION_CONTEXT ACTIVATION_CONTEXT, *PACTIVATION_CONTEXT;

typedef void(NTAPI *PACTIVATION_CONTEXT_NOTIFY_ROUTINE)(ULONG type, PACTIVATION_CONTEXT context, const void *data,
                                                         PVOID notification_context, PVOID notification_data,
                                                         PBOOLEAN disable);

typedef struct _RTL_ACTIVATION_CONTEXT_STACK_FRAME {
    struct _RTL_ACTIVATION_CONTEXT_STACK_FRAME *Previous;
    PACTIVATION_CONTEXT ActivationContext;
    ULONG Flags;
} RTL_ACTIVATION_CONTEXT_STACK_FRAME, *PRTL_ACTIVATION_CONTEXT_STACK_FRAME;

/* the caller keeps this frame on its own stack for the unsafe fast activation. */
typedef struct _RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_EXTENDED {
    SIZE_T Size;
    ULONG Format;
    RTL_ACTIVATION_CONTEXT_STACK_FRAME Frame;
    PVOID Extra1;
    PVOID Extra2;
    PVOID Extra3;
    PVOID Extra4;
} RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_EXTENDED, *PRTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_EXTENDED;
#define RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_FORMAT_WHISTLER 1

typedef struct _ACTIVATION_CONTEXT_STACK {
    PRTL_ACTIVATION_CONTEXT_STACK_FRAME ActiveFrame;
    LIST_ENTRY FrameListCache;
    ULONG Flags;
    ULONG NextCookieSequenceNumber;
    ULONG StackId;
} ACTIVATION_CONTEXT_STACK, *PACTIVATION_CONTEXT_STACK;
MIME_STATIC_ASSERT(sizeof(ACTIVATION_CONTEXT_STACK) == (sizeof(void *) == 4 ? 0x18 : 0x28), "ACTIVATION_CONTEXT_STACK");

typedef struct _ACTIVATION_CONTEXT_SECTION_KEYED_DATA_ASSEMBLY_METADATA {
    PVOID Information;
    PVOID SectionBase;
    ULONG SectionLength;
    PVOID SectionGlobalDataBase;
    ULONG SectionGlobalDataLength;
} ACTIVATION_CONTEXT_SECTION_KEYED_DATA_ASSEMBLY_METADATA;

/* the same layout as ACTCTX_SECTION_KEYED_DATA. */
typedef struct _ACTIVATION_CONTEXT_SECTION_KEYED_DATA {
    ULONG Size;
    ULONG DataFormatVersion;
    PVOID Data;
    ULONG Length;
    PVOID SectionGlobalData;
    ULONG SectionGlobalDataLength;
    PVOID SectionBase;
    ULONG SectionTotalLength;
    PACTIVATION_CONTEXT ActivationContext;
    ULONG AssemblyRosterIndex;
    ULONG Flags;
    ACTIVATION_CONTEXT_SECTION_KEYED_DATA_ASSEMBLY_METADATA AssemblyMetadata;
} ACTIVATION_CONTEXT_SECTION_KEYED_DATA, *PACTIVATION_CONTEXT_SECTION_KEYED_DATA;
typedef const ACTIVATION_CONTEXT_SECTION_KEYED_DATA *PCACTIVATION_CONTEXT_SECTION_KEYED_DATA;

typedef enum _ACTIVATION_CONTEXT_INFO_CLASS {
    ActivationContextBasicInformation = 1,
    ActivationContextDetailedInformation = 2,
    AssemblyDetailedInformationInActivationContext = 3,
    FileInformationInAssemblyOfAssemblyInActivationContext = 4,
    RunlevelInformationInActivationContext = 5,
    CompatibilityInformationInActivationContext = 6,
    ActivationContextManifestResourceName = 7,
} ACTIVATION_CONTEXT_INFO_CLASS;

typedef struct _ACTIVATION_CONTEXT_BASIC_INFORMATION {
    HANDLE hActCtx;
    DWORD dwFlags;
} ACTIVATION_CONTEXT_BASIC_INFORMATION, *PACTIVATION_CONTEXT_BASIC_INFORMATION;

typedef struct _ACTIVATION_CONTEXT_QUERY_INDEX {
    DWORD ulAssemblyIndex;
    DWORD ulFileIndexInAssembly;
} ACTIVATION_CONTEXT_QUERY_INDEX, *PACTIVATION_CONTEXT_QUERY_INDEX;

typedef struct _ACTIVATION_CONTEXT_DETAILED_INFORMATION {
    DWORD dwFlags;
    DWORD ulFormatVersion;
    DWORD ulAssemblyCount;
    DWORD ulRootManifestPathType;
    DWORD ulRootManifestPathChars;
    DWORD ulRootConfigurationPathType;
    DWORD ulRootConfigurationPathChars;
    DWORD ulAppDirPathType;
    DWORD ulAppDirPathChars;
    PCWSTR lpRootManifestPath;
    PCWSTR lpRootConfigurationPath;
    PCWSTR lpAppDirPath;
} ACTIVATION_CONTEXT_DETAILED_INFORMATION, *PACTIVATION_CONTEXT_DETAILED_INFORMATION;

typedef struct _ACTIVATION_CONTEXT_ASSEMBLY_DETAILED_INFORMATION {
    DWORD ulFlags;
    DWORD ulEncodedAssemblyIdentityLength;
    DWORD ulManifestPathType;
    DWORD ulManifestPathLength;
    LARGE_INTEGER liManifestLastWriteTime;
    DWORD ulPolicyPathType;
    DWORD ulPolicyPathLength;
    LARGE_INTEGER liPolicyLastWriteTime;
    DWORD ulMetadataSatelliteRosterIndex;
    DWORD ulManifestVersionMajor;
    DWORD ulManifestVersionMinor;
    DWORD ulPolicyVersionMajor;
    DWORD ulPolicyVersionMinor;
    DWORD ulAssemblyDirectoryNameLength;
    PCWSTR lpAssemblyEncodedAssemblyIdentity;
    PCWSTR lpAssemblyManifestPath;
    PCWSTR lpAssemblyPolicyPath;
    PCWSTR lpAssemblyDirectoryName;
    DWORD ulFileCount;
} ACTIVATION_CONTEXT_ASSEMBLY_DETAILED_INFORMATION, *PACTIVATION_CONTEXT_ASSEMBLY_DETAILED_INFORMATION;

typedef struct _ASSEMBLY_FILE_DETAILED_INFORMATION {
    DWORD ulFlags;
    DWORD ulFilenameLength;
    DWORD ulPathLength;
    PCWSTR lpFileName;
    PCWSTR lpFilePath;
} ASSEMBLY_FILE_DETAILED_INFORMATION, *PASSEMBLY_FILE_DETAILED_INFORMATION;

typedef enum {
    ACTCTX_RUN_LEVEL_UNSPECIFIED = 0,
    ACTCTX_RUN_LEVEL_AS_INVOKER,
    ACTCTX_RUN_LEVEL_HIGHEST_AVAILABLE,
    ACTCTX_RUN_LEVEL_REQUIRE_ADMIN,
    ACTCTX_RUN_LEVEL_NUMBERS,
} ACTCTX_REQUESTED_RUN_LEVEL;

typedef struct _ACTIVATION_CONTEXT_RUN_LEVEL_INFORMATION {
    DWORD ulFlags;
    ACTCTX_REQUESTED_RUN_LEVEL RunLevel;
    DWORD UiAccess;
} ACTIVATION_CONTEXT_RUN_LEVEL_INFORMATION, *PACTIVATION_CONTEXT_RUN_LEVEL_INFORMATION;

typedef enum {
    ACTCTX_COMPATIBILITY_ELEMENT_TYPE_UNKNOWN = 0,
    ACTCTX_COMPATIBILITY_ELEMENT_TYPE_OS,
    ACTCTX_COMPATIBILITY_ELEMENT_TYPE_MITIGATION,
    ACTCTX_COMPATIBILITY_ELEMENT_TYPE_MAXVERSIONTESTED,
} ACTCTX_COMPATIBILITY_ELEMENT_TYPE;

typedef struct _COMPATIBILITY_CONTEXT_ELEMENT {
    GUID Id;
    ACTCTX_COMPATIBILITY_ELEMENT_TYPE Type;
    ULONGLONG MaxVersionTested;
} COMPATIBILITY_CONTEXT_ELEMENT, *PCOMPATIBILITY_CONTEXT_ELEMENT;

typedef struct _ACTIVATION_CONTEXT_COMPATIBILITY_INFORMATION {
    DWORD ElementCount;
    COMPATIBILITY_CONTEXT_ELEMENT Elements[];
} ACTIVATION_CONTEXT_COMPATIBILITY_INFORMATION, *PACTIVATION_CONTEXT_COMPATIBILITY_INFORMATION;

typedef NTSTATUS(NTAPI *PLDR_MANIFEST_PROBER_ROUTINE)(PVOID dll_base, PCWSTR full_dll_path, PACTIVATION_CONTEXT *context);
typedef NTSTATUS(NTAPI *PLDR_ACTCTX_LANGUAGE_ROURINE)(PVOID unknown1, USHORT unknown2, PVOID unknown3);
typedef void(NTAPI *PLDR_RELEASE_ACTCTX_ROUTINE)(PACTIVATION_CONTEXT context);

NTSYSAPI NTSTATUS NTAPI RtlCreateActivationContext(ULONG flags, const void *data, ULONG extra_bytes,
                                                   PACTIVATION_CONTEXT_NOTIFY_ROUTINE notification,
                                                   PVOID notification_context, PACTIVATION_CONTEXT *context);
NTSYSAPI void NTAPI RtlAddRefActivationContext(PACTIVATION_CONTEXT context);
NTSYSAPI void NTAPI RtlReleaseActivationContext(PACTIVATION_CONTEXT context);
NTSYSAPI NTSTATUS NTAPI RtlZombifyActivationContext(PACTIVATION_CONTEXT context);
NTSYSAPI NTSTATUS NTAPI RtlActivateActivationContext(ULONG flags, PACTIVATION_CONTEXT context, PULONG_PTR cookie);
NTSYSAPI NTSTATUS NTAPI RtlActivateActivationContextEx(ULONG flags, PTEB teb, PACTIVATION_CONTEXT context,
                                                       PULONG_PTR cookie);
NTSYSAPI void NTAPI RtlDeactivateActivationContext(ULONG flags, ULONG_PTR cookie);
NTSYSAPI NTSTATUS NTAPI RtlGetActiveActivationContext(PACTIVATION_CONTEXT *context);
NTSYSAPI BOOLEAN NTAPI RtlIsActivationContextActive(PACTIVATION_CONTEXT context);
NTSYSAPI NTSTATUS NTAPI RtlAllocateActivationContextStack(PACTIVATION_CONTEXT_STACK *stack);
NTSYSAPI void NTAPI RtlFreeActivationContextStack(PACTIVATION_CONTEXT_STACK stack);
NTSYSAPI void NTAPI RtlFreeThreadActivationContextStack(void);
NTSYSAPI NTSTATUS NTAPI RtlFindActivationContextSectionString(ULONG flags, const GUID *extension, ULONG section,
                                                              PCUNICODE_STRING key,
                                                              PACTIVATION_CONTEXT_SECTION_KEYED_DATA data);
NTSYSAPI NTSTATUS NTAPI RtlFindActivationContextSectionGuid(ULONG flags, const GUID *extension, ULONG section,
                                                            const GUID *key, PACTIVATION_CONTEXT_SECTION_KEYED_DATA data);
NTSYSAPI NTSTATUS NTAPI RtlQueryInformationActivationContext(ULONG flags, PACTIVATION_CONTEXT context,
                                                             PVOID subinstance, ULONG info_class, PVOID buffer,
                                                             SIZE_T length, PSIZE_T written);
NTSYSAPI NTSTATUS NTAPI RtlQueryInformationActiveActivationContext(ULONG info_class, PVOID buffer, SIZE_T length,
                                                                   PSIZE_T written);
NTSYSAPI NTSTATUS NTAPI RtlQueryActivationContextApplicationSettings(ULONG flags, PACTIVATION_CONTEXT context,
                                                                     PCWSTR ns, PCWSTR name, PWSTR buffer,
                                                                     SIZE_T chars, PSIZE_T needed);
NTSYSAPI NTSTATUS NTAPI RtlDosApplyFileIsolationRedirection_Ustr(ULONG flags, PCUNICODE_STRING name,
                                                                 PCUNICODE_STRING extension, PUNICODE_STRING static_buf,
                                                                 PUNICODE_STRING dynamic_buf, PUNICODE_STRING *new_name,
                                                                 PULONG new_flags, PSIZE_T file_name_offset,
                                                                 PSIZE_T required);
NTSYSAPI void NTAPI LdrSetDllManifestProber(PLDR_MANIFEST_PROBER_ROUTINE prober,
                                            PLDR_ACTCTX_LANGUAGE_ROURINE language,
                                            PLDR_RELEASE_ACTCTX_ROUTINE release);

#ifdef __i386__
NTSYSAPI void __fastcall RtlActivateActivationContextUnsafeFast(
    PRTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_EXTENDED frame, PACTIVATION_CONTEXT context);
NTSYSAPI void __fastcall RtlDeactivateActivationContextUnsafeFast(
    PRTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_EXTENDED frame);
#else
NTSYSAPI void NTAPI RtlActivateActivationContextUnsafeFast(
    PRTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_EXTENDED frame, PACTIVATION_CONTEXT context);
NTSYSAPI void NTAPI RtlDeactivateActivationContextUnsafeFast(
    PRTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_EXTENDED frame);
#endif

/*
 * mime's own: builds activation context data from a manifest for CreateActCtx, the job of sxs.dll inside csrss
 * on windows. the source is a manifest file, or an image when module or resource are set. the data comes from
 * NtAllocateVirtualMemory; free it with RtlFreeActivationContextData.
 */
typedef struct _MIME_SXS_GENERATE {
    ULONG Flags;               /* ACTCTX_FLAG_*, already checked by CreateActCtxW. */
    PCWSTR Source;             /* full path of the manifest or the image. */
    PCWSTR AssemblyDirectory;  /* where private assemblies are probed. */
    PCWSTR ResourceName;       /* RT_MANIFEST name or id, NULL for a file. */
    PVOID Module;              /* a loaded image to read the resource from, or NULL. */
    USHORT ProcessorArchitecture;
    LANGID LangId;
} MIME_SXS_GENERATE, *PMIME_SXS_GENERATE;

NTSYSAPI NTSTATUS NTAPI RtlGenerateActivationContext(const MIME_SXS_GENERATE *request, PVOID *data);
NTSYSAPI void NTAPI RtlFreeActivationContextData(PVOID data);
/* gives a context to a suspended thread of this process, activated when it starts, like CreateRemoteThreadEx. */
NTSYSAPI NTSTATUS NTAPI RtlInheritActivationContext(HANDLE thread_id, PACTIVATION_CONTEXT context);

#endif
