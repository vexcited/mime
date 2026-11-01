#ifndef MIME_MSI_PRIVATE_H
#define MIME_MSI_PRIVATE_H

#define MSIAPI DECLSPEC_EXPORT
#include <mime/win32/msi.h>
#include <mime/win32/winuser.h>
#include <mime/win32/winnls.h>
#include <rtl.h>
#include "../lib/compound/compound.h"

#if defined(__x86_64__)
#define IMAGE_FILE_MACHINE_NATIVE 0x8664
#elif defined(__i386__)
#define IMAGE_FILE_MACHINE_NATIVE 0x14c
#else
#define IMAGE_FILE_MACHINE_NATIVE 0xAA64
#endif

enum { MIME_MSI_KIND_RECORD = 1, MIME_MSI_KIND_DATABASE, MIME_MSI_KIND_VIEW, MIME_MSI_KIND_PACKAGE, MIME_MSI_KIND_SUMMARY };
typedef struct MIME_MSI_OBJECT {
    ULONG Kind;
    ULONG References;
    void (*Destroy)(struct MIME_MSI_OBJECT *object);
} MIME_MSI_OBJECT;

extern SRWLOCK MimeMsiLock;
void *MimeMsiAllocate(size_t size);
void MimeMsiFree(void *memory);
MSIHANDLE MimeMsiAddHandle(MIME_MSI_OBJECT *object);
MIME_MSI_OBJECT *MimeMsiGetHandle(MSIHANDLE handle, ULONG kind);

void MimeMsiRelease(MIME_MSI_OBJECT *object);

enum { FIELD_NULL, FIELD_INTEGER, FIELD_STRING, FIELD_STREAM };

/* a binary field's bytes, shared by records fetched from the same row. */
typedef struct MIME_MSI_BLOB {
    ULONG References;
    SIZE_T Size;
    BYTE Data[];
} MIME_MSI_BLOB;

typedef struct MIME_MSI_FIELD {
    ULONG Kind;
    int Integer;
    PWSTR String;
    MIME_MSI_BLOB *Stream;
    SIZE_T Position;
} MIME_MSI_FIELD;

MIME_MSI_BLOB *MimeMsiBlobCreate(const void *data, SIZE_T size);
void MimeMsiBlobRelease(MIME_MSI_BLOB *blob);
UINT MimeMsiFieldStream(MIME_MSI_FIELD *field, MIME_MSI_BLOB *blob);

typedef struct MIME_MSI_RECORD {
    MIME_MSI_OBJECT Object;
    UINT Count;
    MIME_MSI_FIELD Fields[];
} MIME_MSI_RECORD;

MIME_MSI_RECORD *MimeMsiRecordCreate(UINT count);
UINT MimeMsiFieldString(MIME_MSI_FIELD *field, LPCWSTR value);
int MimeMsiFieldGetInteger(const MIME_MSI_FIELD *field);
void MimeMsiFieldInteger(MIME_MSI_FIELD *field, int value);

typedef struct MIME_MSI_COLUMN {
    PCWSTR Name;
    USHORT Type;
    ULONG Width;
    SIZE_T Offset;
} MIME_MSI_COLUMN;

typedef struct MIME_MSI_TABLE {
    struct MIME_MSI_TABLE *Next;
    WCHAR Name[64];
    BYTE *Data;
    ULONG Rows, Count;
    MIME_MSI_COLUMN Columns[64];
} MIME_MSI_TABLE;

/* a stream a transform adds, read before the streams of the file. */
typedef struct MIME_MSI_STREAM {
    struct MIME_MSI_STREAM *Next;
    WCHAR Name[32];
    BYTE *Data;
    SIZE_T Size;
} MIME_MSI_STREAM;

typedef struct MIME_MSI_DATABASE {
    MIME_MSI_OBJECT Object;
    MIME_CFB File;
    PVOID Data;
    PWSTR Path;
    PWSTR *Strings;
    ULONG StringCount, StringWidth, CodePage, Revision;
    MIME_MSI_TABLE *Tables;
    struct MIME_MSI_TRANSFORM *Transforms;
    BOOL OwnedData;
    /* a storage inside a patch opened as its own database: its directory entry and the file holding it. */
    ULONG Root;
    struct MIME_MSI_DATABASE *Parent;
    /* patches applied to this database, whose cabinets the patched Media rows name. */
    struct MIME_MSI_DATABASE **Patches;
    ULONG PatchCount, PatchTransforms;
    MIME_MSI_STREAM *Streams;
} MIME_MSI_DATABASE;

/* where a transform is kept: in the package, beside it in the source, in a secure copy, or wherever its file is. */
enum { TRANSFORM_FILE, TRANSFORM_EMBEDDED, TRANSFORM_SECURE, TRANSFORM_SOURCE };

typedef struct MIME_MSI_TRANSFORM {
    struct MIME_MSI_TRANSFORM *Next;
    MIME_MSI_DATABASE *Database;
    UINT Flags, Kind;
    /* the storage of an embedded transform, the name relative to the source of one kept there. */
    PWSTR Name;
} MIME_MSI_TRANSFORM;

UINT MimeMsiLoadStrings(MIME_MSI_DATABASE *database);
BOOL MimeMsiEncodeName(PCWSTR name, BOOL table, WCHAR out[32]);
BOOL MimeMsiDecodeTableName(const WCHAR encoded[32], WCHAR name[64]);
UINT MimeMsiOpenDatabaseFile(PCWSTR path, BOOL transform, MIME_MSI_DATABASE **out);
UINT MimeMsiOpenStorageFile(PCWSTR path, BYTE kind, MIME_MSI_DATABASE **out);
UINT MimeMsiOpenSubStorage(MIME_MSI_DATABASE *parent, PCWSTR name, MIME_MSI_DATABASE **out);
UINT MimeMsiApplyTransform(MSIHANDLE handle, PCWSTR path, int flags, BOOL named_errors);
UINT MimeMsiChildren(MIME_MSI_DATABASE *database, ULONG **list, ULONG *count);
UINT MimeMsiTableStream(MIME_MSI_DATABASE *database, PCWSTR name, BYTE **out, SIZE_T *size);
UINT MimeMsiApplyTransformStorage(MIME_MSI_DATABASE *database, MIME_MSI_DATABASE *transform, UINT flags);
UINT MimeMsiApplyPatches(MSIHANDLE database, PCWSTR product, PCWSTR list);

/* what an image of a database leaves out, changes and adds. a negative word count leaves the summary's alone. */
typedef struct MIME_MSI_IMAGE_EDIT {
    const WCHAR (*Remove)[32];
    ULONG RemoveCount;
    BOOL Regenerate, SaveTime;
    int WordCount;
    PCWSTR LastAuthor, Subject, Comments, Revision, AdminProperties;
} MIME_MSI_IMAGE_EDIT;
UINT MimeMsiBuildImage(MIME_MSI_DATABASE *database, const MIME_MSI_IMAGE_EDIT *edit, BYTE **out, SIZE_T *size);
UINT MimeMsiCreateSummary(MIME_MSI_DATABASE *database, MSIHANDLE *out);

typedef struct MIME_MSI_SUMMARY_VALUE {
    UINT Type;
    int Number;
    FILETIME Time;
    PWSTR Text;
} MIME_MSI_SUMMARY_VALUE;
UINT MimeMsiSummaryProperty(MIME_MSI_DATABASE *database, UINT id, MIME_MSI_SUMMARY_VALUE *value);
UINT MimeMsiInstanceTransforms(MSIHANDLE database, ULONG *count, BOOL *first, WCHAR code[39]);
UINT MimeMsiApplyTransformList(MSIHANDLE database, PCWSTR list, PCWSTR source, BOOL secure, BOOL at_source);
UINT MimeMsiLoadTable(MIME_MSI_DATABASE *database, PCWSTR name, MIME_MSI_TABLE **out);
UINT MimeMsiReadStream(MIME_MSI_DATABASE *database, PCWSTR name, BYTE **out, SIZE_T *size);
UINT MimeMsiReadBinary(MIME_MSI_DATABASE *database, PCWSTR name, BYTE **out, SIZE_T *size);
ULONG MimeMsiCell(const MIME_MSI_TABLE *table, ULONG row, ULONG column);

BOOL MimeMsiPackGuid(PCWSTR guid, WCHAR packed[33]);
BOOL MimeMsiUnpackGuid(PCWSTR packed, WCHAR guid[39]);

typedef struct MIME_MSI_PROPERTY {
    struct MIME_MSI_PROPERTY *Next;
    PWSTR Name, Value;
} MIME_MSI_PROPERTY;

typedef struct MIME_MSI_FEATURE {
    WCHAR Name[39], Parent[39];
    INSTALLSTATE Installed, Requested;
    /* the level a Condition row set for this costing. */
    BOOL LevelSet;
    int Level;
    /* the attributes a program set, as the Feature table holds them. */
    BOOL AttributesSet;
    int Attributes;
} MIME_MSI_FEATURE;

/* the state a program asked for a component, which holds against what the features say. */
typedef struct MIME_MSI_COMPONENT_REQUEST {
    PWSTR Name;
    INSTALLSTATE State;
} MIME_MSI_COMPONENT_REQUEST;

typedef struct MIME_MSI_PACKAGE {
    MIME_MSI_OBJECT Object;
    MIME_MSI_DATABASE *Database;
    MIME_MSI_PROPERTY *Properties;
    DWORD Options;
    PWSTR ActionFormat;
    BOOL SequenceRunning;
    BOOL TransactionActive, TransactionCommitted, FilesQueued, CatalogRegistered;
    ULONG CostStage, BackupCounter;
    struct MIME_MSI_FILE_PLAN *Files;
    struct MIME_MSI_CHANGE *Changes;
    MIME_MSI_FEATURE *Features;
    ULONG FeatureCount, FeatureRevision;
    BOOL FeatureSelectionReady;
    /* what is selected changed since the file plan was made. */
    BOOL PlanStale;
    BOOL Maintenance, MaintenanceReady, ConfigureDefaults, Recache;
    HKEY MaintenanceRoot;
    WCHAR MaintenanceProduct[39];
    BOOL Is64Bit;
    /* an administrative installation is running, or the package is the image of one. */
    BOOL Admin, AdminImage, Advertise;
    /* patches were applied to the installed product in this session, and are to be cached. */
    BOOL NewPatches;
    int SourceType;
    DWORD RegistryView;
    struct MIME_MSI_SCRIPT_ACTION *Deferred, **DeferredTail;
    struct MIME_MSI_SCRIPT_ACTION *Rollback, *Commit;
    BOOL InScript, RollbackSession, CommitSession;
    BOOL Removing, RebootAtEnd, RebootNow;
    BOOL NoRollback;
    BOOL FirstSequenceDone;
    UINT LastCustomActionError;
    PWSTR LaunchMessage;
    BYTE *ComponentInstalled;
    ULONG ComponentRows;
    MIME_MSI_COMPONENT_REQUEST *ComponentRequests;
    ULONG ComponentRequestCount;
    ULONG SessionKind;
    struct MIME_MSI_QUEUED_ACTION *Queued;
    struct MIME_MSI_CAHOST *Host;
} MIME_MSI_PACKAGE;

typedef struct MIME_MSI_CA_ROW {
    PWSTR Name, Source, Target;
    UINT Type;
} MIME_MSI_CA_ROW;

/* a deferred action, run with the script or on its rollback or commit. */
typedef struct MIME_MSI_SCRIPT_ACTION {
    struct MIME_MSI_SCRIPT_ACTION *Next;
    MIME_MSI_CA_ROW Row;
    PWSTR Data;
} MIME_MSI_SCRIPT_ACTION;

#define MIME_MSI_SCRIPT_PENDING 0xFFFF0001u
#define MIME_MSI_UNLOCKED_PENDING 0xFFFF0002u

void MimeMsiDestroyPackage(MIME_MSI_OBJECT *object);
UINT MimeMsiCustomActionLookup(MIME_MSI_PACKAGE *package, PCWSTR name, MIME_MSI_CA_ROW *row);
void MimeMsiCustomActionFree(MIME_MSI_CA_ROW *row);
UINT MimeMsiRunCustomAction(MSIHANDLE handle, MIME_MSI_PACKAGE *package, MIME_MSI_CA_ROW *row, PWSTR *message);
UINT MimeMsiRunScript(MSIHANDLE handle, MIME_MSI_PACKAGE *package);
void MimeMsiClearScript(MIME_MSI_PACKAGE *package);
UINT MimeMsiRunRollback(MIME_MSI_PACKAGE *package);
UINT MimeMsiRunCommit(MIME_MSI_PACKAGE *package);
enum {
    PROXY_GetPropertyW, PROXY_SetPropertyW, PROXY_RecordSetStringW, PROXY_CreateRecord, PROXY_ProcessMessage,
    PROXY_ViewExecute, PROXY_ViewFetch, PROXY_EnumRelatedProductsW, PROXY_GetActiveDatabase, PROXY_GetProductInfoW,
    PROXY_CloseHandle, PROXY_RecordGetStringW, PROXY_DatabaseOpenViewW, PROXY_SetMode, PROXY_EvaluateConditionW,
    PROXY_GetMode, PROXY_RecordIsNull, PROXY_GetSourcePathW, PROXY_GetTargetPathW, PROXY_DoActionW, PROXY_FormatRecordW,
    PROXY_ViewGetErrorW, PROXY_ViewModify, PROXY_ViewGetColumnInfo, PROXY_DatabaseGetPrimaryKeysW, PROXY_RecordSetInteger,
    PROXY_RecordGetInteger, PROXY_RecordReadStream, PROXY_GetComponentStateW,
    PROXY_GetFeatureStateW, PROXY_SetFeatureStateW, PROXY_SetComponentStateW, PROXY_SetFeatureAttributesW,
    PROXY_GetFeatureCostW, PROXY_EnumComponentCostsW, PROXY_GetFeatureValidStatesW, PROXY_SetInstallLevel,
    PROXY_SetTargetPathW, PROXY_GetLanguage, PROXY_VerifyDiskSpace, PROXY_SequenceW, PROXY_GetLastErrorRecord,
    PROXY_DatabaseIsTablePersistentW, PROXY_DatabaseCommit, PROXY_OpenDatabaseW, PROXY_GetSummaryInformationW,
    PROXY_SummaryInfoGetPropertyCount, PROXY_SummaryInfoGetPropertyW, PROXY_SummaryInfoSetPropertyW,
    PROXY_SummaryInfoPersist, PROXY_RecordGetFieldCount, PROXY_RecordDataSize, PROXY_RecordClearData,
    PROXY_RecordSetStreamW, PROXY_ViewClose, PROXY_CloseAllHandles, PROXY_COUNT
};
BOOL MimeMsiProxyActive(void);
UINT_PTR MimeMsiProxyCall(ULONG id, ULONG count, const UINT_PTR *arguments);
void MimeMsiShutDownHost(void);
#define MIME_ARG(x) ((UINT_PTR)(x))
/* in the custom action host, installer functions forward the call to the installer. the leading zero keeps the list valid
   for a call with no arguments. */
#define MIME_REMOTE(id, type, ...) \
    do { \
        if (MimeMsiProxyActive()) { \
            const UINT_PTR arguments_[] = {0, ##__VA_ARGS__}; \
            return (type)MimeMsiProxyCall((id), (ULONG)(sizeof(arguments_) / sizeof(arguments_[0]) - 1), arguments_ + 1); \
        } \
    } while (0)
UINT MimeMsiRemoteRun(MIME_MSI_PACKAGE *package, MSIHANDLE session, PCWSTR dll, PCSTR entry, WORD machine,
                      UINT *result);
UINT MimeMsiMachineOfImage(const BYTE *data, SIZE_T size, WORD *machine);
UINT MimeMsiRemoteRegister(MIME_MSI_PACKAGE *package, PCWSTR dll, PCSTR entry, WORD machine, UINT *result);
UINT MimeMsiExtraAction(MIME_MSI_PACKAGE *package, PCWSTR action);
UINT MimeMsiRunUnlocked(MSIHANDLE handle, MIME_MSI_PACKAGE *package, PCWSTR action);
UINT MimeMsiFormat(MIME_MSI_PACKAGE *package, PCWSTR template, PWSTR *out);
UINT MimeMsiComponentState(MIME_MSI_PACKAGE *package, PCWSTR name, INSTALLSTATE *installed, INSTALLSTATE *action);
UINT MimeMsiComponentChange(MIME_MSI_PACKAGE *package, PCWSTR name, BOOL *local, BOOL *registered);
BOOL MimeMsiLogEnabled(void);
UINT MimeMsiFileTargetPath(MIME_MSI_PACKAGE *package, PCWSTR key, PWSTR *out);
UINT MimeMsiComponentDirectory(MIME_MSI_PACKAGE *package, PCWSTR name, PWSTR *out);

typedef UINT (*MIME_MSI_PROPERTY_SINK)(void *context, PCWSTR name, PCWSTR value);
UINT MimeMsiParseProperties(PCWSTR command, MIME_MSI_PROPERTY_SINK sink, void *context);
PWSTR MimeMsiDuplicate(PCWSTR text);
UINT MimeMsiCopyString(PCWSTR text, LPWSTR out, LPDWORD size);
PCWSTR MimeMsiGetProperty(const MIME_MSI_PACKAGE *package, PCWSTR name);
UINT MimeMsiSetProperty(MIME_MSI_PACKAGE *package, PCWSTR name, PCWSTR value);

UINT MimeMsiFormatRecord(MIME_MSI_PACKAGE *package, const MIME_MSI_RECORD *record, PWSTR *out, DWORD *length);

void MimeMsiCloseLog(void);

MSICONDITION MimeMsiEvaluateCondition(MIME_MSI_PACKAGE *package, PCWSTR condition);

ULONG MimeMsiColumn(const MIME_MSI_TABLE *table, PCWSTR name, BOOL text);
PCWSTR MimeMsiText(const MIME_MSI_DATABASE *database, const MIME_MSI_TABLE *table, ULONG row, ULONG column);
int MimeMsiInteger(const MIME_MSI_TABLE *table, ULONG row, ULONG column);
PWSTR MimeMsiJoinPath(PCWSTR parent, PCWSTR name);
UINT MimeMsiPublishDirectories(MIME_MSI_PACKAGE *package);
UINT MimeMsiSnapshotComponents(MIME_MSI_PACKAGE *package);
UINT MimeMsiDirectoryPath(MIME_MSI_PACKAGE *package, PCWSTR name, BOOL source, PWSTR *out);
UINT MimeMsiSetTargetPath(MIME_MSI_PACKAGE *package, PCWSTR name, PCWSTR value);

UINT MimeMsiStandardAction(MIME_MSI_PACKAGE *package, PCWSTR action);
UINT MimeMsiInstallAdminPackage(MIME_MSI_PACKAGE *package);
UINT MimeMsiAdvertiseScriptFile(PCWSTR package, PCWSTR script, PCWSTR transforms, LANGID language, DWORD platform, DWORD options);
UINT MimeMsiFinishTransaction(MIME_MSI_PACKAGE *package, BOOL commit);

UINT MimeMsiCatalogPath(PCWSTR product, WCHAR path[96]);

UINT MimeMsiWriteRegistry(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path, PCWSTR name,
                         DWORD type, const void *data, DWORD bytes);
UINT MimeMsiMakeDirectory(MIME_MSI_PACKAGE *package, PCWSTR path);
UINT MimeMsiNoteNewFile(MIME_MSI_PACKAGE *package, PCWSTR path);
UINT MimeMsiBackupFile(MIME_MSI_PACKAGE *package, PCWSTR path, BOOL *existed);
UINT MimeMsiRemoveFile(MIME_MSI_PACKAGE *package, PCWSTR path);
UINT MimeMsiRemoveDirectory(MIME_MSI_PACKAGE *package, PCWSTR path);
UINT MimeMsiDeleteRegistryValue(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path, PCWSTR name);
UINT MimeMsiDeleteRegistryTree(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path, ULONG depth);
typedef UINT (*MIME_MSI_UNDO)(void *context, BOOL rollback);
typedef UINT (*MIME_MSI_QUEUED)(MIME_MSI_PACKAGE *package, UINT_PTR argument);
UINT MimeMsiNoteUndo(MIME_MSI_PACKAGE *package, MIME_MSI_UNDO undo, void *context);
UINT MimeMsiQueueAction(MIME_MSI_PACKAGE *package, MIME_MSI_QUEUED run, UINT_PTR argument);
UINT MimeMsiServiceAction(MIME_MSI_PACKAGE *package, PCWSTR action);
UINT MimeMsiAssemblyAction(MIME_MSI_PACKAGE *package, PCWSTR action);
UINT MimeMsiAssemblyFolder(MIME_MSI_PACKAGE *package, PCWSTR component, PWSTR *folder);
UINT MimeMsiAssemblyCleanup(MIME_MSI_PACKAGE *package, PCWSTR component);
UINT MimeMsiCopyInstalledFile(MIME_MSI_PACKAGE *package, PCWSTR source, PCWSTR target);
UINT MimeMsiFeatureSelected(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *features, PCWSTR name, ULONG depth, BOOL *selected);
UINT MimeMsiRegistrationAction(MIME_MSI_PACKAGE *package, PCWSTR action);
UINT MimeMsiProcessComponents(MIME_MSI_PACKAGE *package, HKEY root);
UINT MimeMsiPublishComponents(MIME_MSI_PACKAGE *package, HKEY root, BOOL publish, BOOL removing);
UINT MimeMsiDescriptor(PCWSTR product, PCWSTR feature, PCWSTR component, PWSTR *out);
UINT MimeMsiDescriptorEx(PCWSTR product, PCWSTR feature, PCWSTR component, ULONG flags, PWSTR *out);
BOOL MimeMsiDecompose(PCWSTR descriptor, BOOL validate, PWSTR product, PWSTR feature, PWSTR component, DWORD *used,
                      DWORD *remaining, BOOL *prefixed);
BOOL MimeMsiCompressGuid(PCWSTR guid, WCHAR out[21]);
BOOL MimeMsiExpandGuid(PCWSTR text, WCHAR out[39]);
UINT MimeMsiOpenProductKey(PCWSTR product, HKEY *key, BOOL *machine);
UINT MimeMsiProvideComponent(PCWSTR product, PCWSTR feature, PCWSTR component, DWORD mode, PWSTR path, LPDWORD size);
UINT MimeMsiProcessConditions(MIME_MSI_PACKAGE *package, BOOL *changed);
int MimeMsiFeatureLevel(MIME_MSI_PACKAGE *package, PCWSTR name, int level);
UINT MimeMsiInstallQueuedFiles(MIME_MSI_PACKAGE *package);
UINT MimeMsiCreateRegistryKey(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path);
UINT MimeMsiTableAction(MIME_MSI_PACKAGE *package, PCWSTR action);
UINT MimeMsiRequireTransaction(MIME_MSI_PACKAGE *package);
UINT MimeMsiRebootResult(MIME_MSI_PACKAGE *package);
UINT MimeMsiClassesAction(MIME_MSI_PACKAGE *package, PCWSTR action);
UINT MimeMsiFontsAction(MIME_MSI_PACKAGE *package, PCWSTR action);
UINT MimeMsiFileTablesAction(MIME_MSI_PACKAGE *package, PCWSTR action);
UINT MimeMsiRegistrationTablesAction(MIME_MSI_PACKAGE *package, PCWSTR action);
UINT MimeMsiIniAction(MIME_MSI_PACKAGE *package, PCWSTR action);
UINT MimeMsiComponentSelected(MIME_MSI_PACKAGE *package, PCWSTR name, PCWSTR *directory, BOOL *selected);
UINT MimeMsiRegistryText(HKEY key, PCWSTR name, PWSTR *out);
UINT MimeMsiCurrentSid(WCHAR sid[256]);
UINT MimeMsiRegisterSource(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR product_path);
UINT MimeMsiWriteInstalledFile(MIME_MSI_PACKAGE *package, const void *data, SIZE_T size, PCWSTR target);
UINT MimeMsiCacheTransforms(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR product_path, PCWSTR cache);
UINT MimeMsiRestoreTransforms(MSIHANDLE database, HKEY product, PCWSTR code);
UINT MimeMsiFindProductSourceFile(PCWSTR product, PCWSTR relative, PWSTR *full);
UINT MimeMsiCachePatches(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR product_path, PCWSTR cache);
UINT MimeMsiRestorePatches(MSIHANDLE database, HKEY product);
UINT MimeMsiSaveDirectories(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path);
UINT MimeMsiLoadMaintenance(MIME_MSI_PACKAGE *package, HKEY key, PCWSTR product, BOOL machine);
UINT MimeMsiValidateMaintenance(MIME_MSI_PACKAGE *package);
typedef struct MIME_MSI_FILE_HASH {
    DWORD Size, Words[4];
} MIME_MSI_FILE_HASH;
#define MIME_MSI_FILE_COMPANION 0x10000
BOOL MimeMsiParseVersion(PCWSTR text, DWORD *high, DWORD *low);
UINT MimeMsiFileOverwrite(PCWSTR path, PCWSTR version, PCWSTR language, const MIME_MSI_FILE_HASH *hash, DWORD flags,
                          BOOL *overwrite);
UINT MimeMsiCompanionOverwrite(PCWSTR parent_path, PCWSTR parent_version, PCWSTR parent_language, PCWSTR path,
                               const MIME_MSI_FILE_HASH *hash, DWORD flags, BOOL *overwrite);
UINT MimeMsiReinstallMode(MIME_MSI_PACKAGE *package, DWORD *mode);
BOOL MimeMsiModeAllows(MIME_MSI_PACKAGE *package, DWORD flag);
UINT MimeMsiComponentRequested(MIME_MSI_PACKAGE *package, PCWSTR name, BOOL *requested);
UINT MimeMsiOpenProductEx(PCWSTR product, PCWSTR package, MSIHANDLE *out);
UINT MimeMsiRecachePackage(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path);
UINT MimeMsiFindPackageSource(PCWSTR product, PWSTR path, DWORD size);
BOOL MimeMsiFindSourceFile(MIME_MSI_PACKAGE *package, PCWSTR relative, PWSTR *full);
UINT MimeMsiLoadFeatures(MIME_MSI_PACKAGE *package);
UINT MimeMsiRefreshFilePlan(MIME_MSI_PACKAGE *package);
void MimeMsiPlanChanged(MIME_MSI_PACKAGE *package);
void MimeMsiClearComponentRequests(MIME_MSI_PACKAGE *package);
UINT MimeMsiFeatureState(MIME_MSI_PACKAGE *package, PCWSTR name, INSTALLSTATE *installed, INSTALLSTATE *action);

UINT MimeMsiInitializeSystem(MIME_MSI_PACKAGE *package);
UINT MimeMsiSetScopeFolders(MIME_MSI_PACKAGE *package);
BOOL MimeMsiIs64BitPackage(MIME_MSI_PACKAGE *package);

WINBASEAPI BOOL WINAPI GetUserNameW(LPWSTR buffer, LPDWORD size);
WINBASEAPI LSTATUS WINAPI RegConnectRegistryW(LPCWSTR machine, HKEY key, HKEY *result);

#endif
