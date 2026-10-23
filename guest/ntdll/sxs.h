/*
 * side by side internals shared by ntdll's sources: the binary layout of
 * activation context data, the manifest model the parser fills and the
 * hooks the loader calls.
 */
#ifndef MIME_NTDLL_SXS_H
#define MIME_NTDLL_SXS_H

#include "ntdll_private.h"
#include <mime/ntactctx.h>

/* ------------------------------------------------------------------ activation context data */

#define ACTCTX_DATA_MAGIC 0x78746341u          /* 'Actx' */
#define ACTCTX_STRING_SECTION_MAGIC 0x64487353u /* 'SsHd' */
#define ACTCTX_GUID_SECTION_MAGIC 0x64487347u   /* 'GsHd' */
#define ACTCTX_HASH_X65599 1u

#define ACTCTX_DATA_FLAG_NO_INHERIT 0x00000001

#define ACTCTX_TOC_DENSE 0x00000001
#define ACTCTX_TOC_INORDER 0x00000002

#define ACTCTX_ROSTER_ENTRY_INVALID 0x00000001
#define ACTCTX_ROSTER_ENTRY_ROOT 0x00000002

#define ACTCTX_STRING_SECTION_CASE_INSENSITIVE 0x00000001
#define ACTCTX_STRING_SECTION_INPSEUDOKEYORDER 0x00000002
#define ACTCTX_GUID_SECTION_INORDER 0x00000001

#define ACTCTX_ASSEMBLY_PRIVATE 0x00000010
#define ACTCTX_ASSEMBLY_ROOT 0x00000001 /* guessed name of the root assembly's own flag. */

#define ACTCTX_PATH_TYPE_NONE 1
#define ACTCTX_PATH_TYPE_WIN32_FILE 2

#define ACTCTX_DLL_PATH_INCLUDES_BASE_NAME 0x00000001
#define ACTCTX_DLL_OMITS_ASSEMBLY_ROOT 0x00000002
#define ACTCTX_DLL_EXPAND 0x00000004
#define ACTCTX_DLL_SYSTEM32 0x00000008

typedef struct ACTCTX_DATA {
    ULONG Magic;
    ULONG HeaderSize;
    ULONG FormatVersion;
    ULONG TotalSize;
    ULONG DefaultTocOffset;
    ULONG ExtendedTocOffset;
    ULONG AssemblyRosterOffset;
    ULONG Flags;
} ACTCTX_DATA;
static_assert(sizeof(ACTCTX_DATA) == 0x20);

typedef struct ACTCTX_TOC_HEADER {
    ULONG HeaderSize;
    ULONG EntryCount;
    ULONG FirstEntryOffset;
    ULONG Flags;
} ACTCTX_TOC_HEADER;

typedef struct ACTCTX_TOC_ENTRY {
    ULONG Id;
    ULONG Offset;
    ULONG Length;
    ULONG Format;
} ACTCTX_TOC_ENTRY;

typedef struct ACTCTX_EXTENDED_TOC_HEADER {
    ULONG HeaderSize;
    ULONG EntryCount;
    ULONG FirstEntryOffset;
    ULONG Flags;
} ACTCTX_EXTENDED_TOC_HEADER;

typedef struct ACTCTX_EXTENDED_TOC_ENTRY {
    GUID ExtensionGuid;
    ULONG TocOffset;
    ULONG Length;
} ACTCTX_EXTENDED_TOC_ENTRY;

typedef struct ACTCTX_ROSTER_HEADER {
    ULONG HeaderSize;
    ULONG HashAlgorithm;
    ULONG EntryCount;
    ULONG FirstEntryOffset;
    ULONG AssemblyInformationSectionOffset;
} ACTCTX_ROSTER_HEADER;

typedef struct ACTCTX_ROSTER_ENTRY {
    ULONG Flags;
    ULONG PseudoKey;
    ULONG AssemblyNameOffset;
    ULONG AssemblyNameLength;
    ULONG AssemblyInformationOffset;
    ULONG AssemblyInformationLength;
} ACTCTX_ROSTER_ENTRY;

typedef struct ACTCTX_STRING_SECTION_HEADER {
    ULONG Magic;
    ULONG HeaderSize;
    ULONG FormatVersion;
    ULONG DataFormatVersion;
    ULONG Flags;
    ULONG ElementCount;
    ULONG ElementListOffset;
    ULONG HashAlgorithm;
    ULONG SearchStructureOffset;
    ULONG UserDataOffset;
    ULONG UserDataSize;
} ACTCTX_STRING_SECTION_HEADER;
static_assert(sizeof(ACTCTX_STRING_SECTION_HEADER) == 0x2C);

typedef struct ACTCTX_STRING_SECTION_ENTRY {
    ULONG PseudoKey;
    ULONG KeyOffset;
    ULONG KeyLength;
    ULONG Offset;
    ULONG Length;
    ULONG AssemblyRosterIndex;
} ACTCTX_STRING_SECTION_ENTRY;

typedef struct ACTCTX_HASH_TABLE {
    ULONG BucketTableEntryCount;
    ULONG BucketTableOffset;
} ACTCTX_HASH_TABLE;

typedef struct ACTCTX_HASH_BUCKET {
    ULONG ChainCount;
    ULONG ChainOffset;
} ACTCTX_HASH_BUCKET;

typedef struct ACTCTX_GUID_SECTION_HEADER {
    ULONG Magic;
    ULONG HeaderSize;
    ULONG FormatVersion;
    ULONG DataFormatVersion;
    ULONG Flags;
    ULONG ElementCount;
    ULONG ElementListOffset;
    ULONG SearchStructureOffset;
    ULONG UserDataOffset;
    ULONG UserDataSize;
} ACTCTX_GUID_SECTION_HEADER;
static_assert(sizeof(ACTCTX_GUID_SECTION_HEADER) == 0x28);

typedef struct ACTCTX_GUID_SECTION_ENTRY {
    GUID Guid;
    ULONG Offset;
    ULONG Length;
    ULONG AssemblyRosterIndex;
} ACTCTX_GUID_SECTION_ENTRY;
static_assert(sizeof(ACTCTX_GUID_SECTION_ENTRY) == 28);

#pragma pack(push, 4)
typedef struct ACTCTX_ASSEMBLY_INFORMATION {
    ULONG Size;
    ULONG Flags;
    ULONG EncodedAssemblyIdentityLength;
    ULONG EncodedAssemblyIdentityOffset;
    ULONG ManifestPathType;
    ULONG ManifestPathLength;
    ULONG ManifestPathOffset;
    LARGE_INTEGER ManifestLastWriteTime;
    ULONG PolicyPathType;
    ULONG PolicyPathLength;
    ULONG PolicyPathOffset;
    LARGE_INTEGER PolicyLastWriteTime;
    ULONG MetadataSatelliteRosterIndex;
    ULONG Unused2;
    ULONG ManifestVersionMajor;
    ULONG ManifestVersionMinor;
    ULONG PolicyVersionMajor;
    ULONG PolicyVersionMinor;
    ULONG AssemblyDirectoryNameLength;
    ULONG AssemblyDirectoryNameOffset;
    ULONG NumOfFilesInAssembly;
    ULONG LanguageLength;
    ULONG LanguageOffset;
    ULONG RunLevel;
    ULONG UiAccess;
} ACTCTX_ASSEMBLY_INFORMATION;

typedef struct ACTCTX_ASSEMBLY_GLOBAL_INFORMATION {
    ULONG Size;
    ULONG Flags;
    GUID PolicyCoherencyGuid;
    GUID PolicyOverrideGuid;
    ULONG ApplicationDirectoryPathType;
    ULONG ApplicationDirectoryLength;
    ULONG ApplicationDirectoryOffset;
    ULONG ResourceName;
} ACTCTX_ASSEMBLY_GLOBAL_INFORMATION;
#pragma pack(pop)
static_assert(sizeof(ACTCTX_ASSEMBLY_INFORMATION) == 0x6C);
static_assert(offsetof(ACTCTX_ASSEMBLY_INFORMATION, AssemblyDirectoryNameLength) == 0x50);
static_assert(sizeof(ACTCTX_ASSEMBLY_GLOBAL_INFORMATION) == 0x38);

typedef struct ACTCTX_DLL_REDIRECTION {
    ULONG Size;
    ULONG Flags;
    ULONG TotalPathLength;
    ULONG PathSegmentCount;
    ULONG PathSegmentOffset;
} ACTCTX_DLL_REDIRECTION;

typedef struct ACTCTX_DLL_PATH_SEGMENT {
    ULONG Length;
    ULONG Offset;
} ACTCTX_DLL_PATH_SEGMENT;

typedef struct ACTCTX_WINDOW_CLASS_REDIRECTION {
    ULONG Size;
    ULONG Flags;
    ULONG VersionSpecificClassNameLength;
    ULONG VersionSpecificClassNameOffset;
    ULONG DllNameLength;
    ULONG DllNameOffset;
} ACTCTX_WINDOW_CLASS_REDIRECTION;

typedef struct ACTCTX_COM_SERVER_REDIRECTION {
    ULONG Size;
    ULONG Flags;
    ULONG ThreadingModel;
    GUID ReferenceClsid;
    GUID ConfiguredClsid;
    GUID ImplementedClsid;
    GUID TypeLibraryId;
    ULONG ModuleLength;
    ULONG ModuleOffset;
    ULONG ProgIdLength;
    ULONG ProgIdOffset;
    ULONG ShimDataLength;
    ULONG ShimDataOffset;
    ULONG MiscStatusDefault;
    ULONG MiscStatusContent;
    ULONG MiscStatusThumbnail;
    ULONG MiscStatusIcon;
    ULONG MiscStatusDocPrint;
} ACTCTX_COM_SERVER_REDIRECTION;
static_assert(sizeof(ACTCTX_COM_SERVER_REDIRECTION) == 120);

typedef struct ACTCTX_COM_PROGID_REDIRECTION {
    ULONG Size;
    ULONG Flags;
    ULONG ConfiguredClsidOffset;
} ACTCTX_COM_PROGID_REDIRECTION;

typedef struct ACTCTX_COM_TYPELIB_REDIRECTION {
    ULONG Size;
    ULONG Flags;
    ULONG NameLength; /* module name in the section's user data, terminator included. */
    ULONG NameOffset;
    USHORT MajorVersion;
    USHORT MinorVersion;
    ULONG HelpDirLength;
    ULONG HelpDirOffset; /* from the start of this entry. */
    ULONG LibraryFlags;
} ACTCTX_COM_TYPELIB_REDIRECTION;
static_assert(sizeof(ACTCTX_COM_TYPELIB_REDIRECTION) == 32);

#pragma pack(push, 4)
typedef struct ACTCTX_COM_INTERFACE_REDIRECTION {
    ULONG Size;
    ULONG Flags;
    GUID ProxyStubClsid32;
    ULONG NumMethods;
    GUID TypeLibraryId;
    GUID BaseInterface;
    ULONG NameLength;
    ULONG NameOffset;
} ACTCTX_COM_INTERFACE_REDIRECTION;
#pragma pack(pop)
static_assert(sizeof(ACTCTX_COM_INTERFACE_REDIRECTION) == 68);

typedef struct ACTCTX_APPLICATION_SETTING {
    ULONG Size;
    ULONG Flags;
    ULONG SettingNamespaceLength;
    ULONG SettingNamespaceOffset;
    ULONG SettingNameLength;
    ULONG SettingNameOffset;
    ULONG SettingValueLength;
    ULONG SettingValueOffset;
} ACTCTX_APPLICATION_SETTING;

/* ------------------------------------------------------------------ activation context object */

#define ACTCTX_FLAG_ZOMBIE 0x00000001
#define ACTCTX_FLAG_STATIC 0x00000002
#define ACTCTX_FLAG_USED_NOTIFIED 0x00000008

/* the handle points at RefCount, the fields keep windows' x64 offsets. */
struct _ACTIVATION_CONTEXT {
    LONG RefCount;
    ULONG Flags;
    PVOID Reserved1;
    PVOID Reserved2;
    const ACTCTX_DATA *Data;
    PACTIVATION_CONTEXT_NOTIFY_ROUTINE Notification;
    PVOID NotificationContext;
    ULONG SentNotifications;
    ULONG DisabledNotifications;
};

/* ------------------------------------------------------------------ manifest model */

typedef struct SXS_IDENTITY {
    PWSTR Name;
    PWSTR Type;
    PWSTR Arch;
    PWSTR Token;
    PWSTR Language;
    USHORT Version[4];
    BOOLEAN HasVersion;
} SXS_IDENTITY;

typedef struct SXS_WINDOW_CLASS {
    PWSTR Name;
    BOOLEAN Versioned;
} SXS_WINDOW_CLASS;

typedef struct SXS_COM_CLASS {
    GUID Clsid;
    GUID TypeLibrary;
    PWSTR ProgId;
    PWSTR *ProgIds; /* extra progid elements. */
    ULONG ProgIdCount;
    ULONG ThreadingModel;
    ULONG MiscStatus[5];
} SXS_COM_CLASS;

typedef struct SXS_TYPELIB {
    GUID Id;
    USHORT Major, Minor;
    ULONG Flags;
    PWSTR HelpDir;
} SXS_TYPELIB;

typedef struct SXS_INTERFACE {
    GUID Iid;
    GUID ProxyStubClsid;
    GUID TypeLibrary;
    GUID Base;
    PWSTR Name;
    ULONG NumMethods;
    BOOLEAN HasNumMethods, HasBase;
} SXS_INTERFACE;

typedef struct SXS_FILE {
    PWSTR Name;
    SXS_WINDOW_CLASS *Classes;
    ULONG ClassCount;
    SXS_COM_CLASS *ComClasses;
    ULONG ComClassCount;
    SXS_TYPELIB *TypeLibs;
    ULONG TypeLibCount;
    SXS_INTERFACE *Interfaces; /* comInterfaceProxyStub. */
    ULONG InterfaceCount;
} SXS_FILE;

typedef struct SXS_DEPENDENCY {
    SXS_IDENTITY Identity;
    BOOLEAN Optional;
} SXS_DEPENDENCY;

typedef struct SXS_SETTING {
    PWSTR Namespace;
    PWSTR Name;
    PWSTR Value;
} SXS_SETTING;

typedef struct SXS_COMPATIBILITY {
    GUID Id;
    ULONG Type;
    ULONGLONG MaxVersionTested;
} SXS_COMPATIBILITY;

typedef struct SXS_MANIFEST {
    SXS_IDENTITY Identity;
    BOOLEAN HasIdentity;
    BOOLEAN NoInherit;
    ULONG RunLevel;
    ULONG UiAccess;
    SXS_DEPENDENCY *Dependencies;
    ULONG DependencyCount;
    SXS_FILE *Files;
    ULONG FileCount;
    SXS_INTERFACE *ExternalProxies; /* comInterfaceExternalProxyStub. */
    ULONG ExternalProxyCount;
    SXS_SETTING *Settings;
    ULONG SettingCount;
    SXS_COMPATIBILITY *Compatibility;
    ULONG CompatibilityCount;
    PVOID Pool; /* owns every string and array of the manifest. */
} SXS_MANIFEST;

/* utf-8 or utf-16, with or without a bom. */
NTSTATUS SxspParseManifest(const void *bytes, SIZE_T size, SXS_MANIFEST *manifest);
void SxspFreeManifest(SXS_MANIFEST *manifest);
/* "a.b.c.d", FALSE when malformed. */
BOOLEAN SxspParseVersion(PCWSTR text, USHORT version[4]);

/* ------------------------------------------------------------------ hooks for the loader and kernelbase */

/* builds activation context data from an in-memory manifest as if read from path. */
NTSTATUS SxspGenerateFromText(const void *text, SIZE_T size, PCWSTR path, PVOID *data);

/* x65599 hash of the upcased characters, the key of section strings. */
ULONG SxspHashString(PCWCH s, SIZE_T chars);
/* this processor's system dll directory, with a trailing backslash. */
PCWSTR SxspSystemDirectory(void);

/* the active frame's context, or the process default for NULL. */
PACTIVATION_CONTEXT LdrpSxsActiveContext(void);
/* sets up the thread's activation stack inside the teb when it has none. */
void LdrpSxsInitializeThread(void);
/* builds the process default context from the program's manifest, then applies its settings. */
NTSTATUS LdrpSxsInitializeProcess(PVOID image, PCWSTR image_path);
/* reads a dll's resource 2 manifest, NULL context when it has none. */
NTSTATUS LdrpSxsProbeDll(PVOID base, PCWSTR full_path, PACTIVATION_CONTEXT *context);
/* STATUS_SXS_KEY_NOT_FOUND when the active context does not redirect the name. */
NTSTATUS LdrpSxsRedirectDll(PCWSTR name, PWSTR out, SIZE_T out_chars);

/* activates a module's context around loader work, NULL means the process default. */
typedef RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_EXTENDED LDRP_SXS_FRAME;
void LdrpSxsActivate(LDRP_SXS_FRAME *frame, PACTIVATION_CONTEXT context);
void LdrpSxsDeactivate(LDRP_SXS_FRAME *frame);

#endif
