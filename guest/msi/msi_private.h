#ifndef MIME_MSI_PRIVATE_H
#define MIME_MSI_PRIVATE_H

#define MSIAPI DECLSPEC_EXPORT
#include <mime/win32/msi.h>
#include <mime/win32/winuser.h>
#include <mime/win32/winnls.h>
#include <rtl.h>
#include "../lib/compound/compound.h"

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
} MIME_MSI_DATABASE;

typedef struct MIME_MSI_TRANSFORM {
    struct MIME_MSI_TRANSFORM *Next;
    MIME_MSI_DATABASE *Database;
    UINT Flags;
} MIME_MSI_TRANSFORM;

UINT MimeMsiLoadStrings(MIME_MSI_DATABASE *database);
UINT MimeMsiOpenDatabaseFile(PCWSTR path, BOOL transform, MIME_MSI_DATABASE **out);
UINT MimeMsiOpenStorageFile(PCWSTR path, BYTE kind, MIME_MSI_DATABASE **out);
UINT MimeMsiOpenSubStorage(MIME_MSI_DATABASE *parent, PCWSTR name, MIME_MSI_DATABASE **out);
UINT MimeMsiChildren(MIME_MSI_DATABASE *database, ULONG **list, ULONG *count);
UINT MimeMsiTableStream(MIME_MSI_DATABASE *database, PCWSTR name, BYTE **out, SIZE_T *size);
UINT MimeMsiApplyTransformStorage(MIME_MSI_DATABASE *database, MIME_MSI_DATABASE *transform, UINT flags);
UINT MimeMsiApplyPatches(MSIHANDLE database, PCWSTR product, PCWSTR list);
UINT MimeMsiCreateSummary(MIME_MSI_DATABASE *database, MSIHANDLE *out);

typedef struct MIME_MSI_SUMMARY_VALUE {
    UINT Type;
    int Number;
    FILETIME Time;
    PWSTR Text;
} MIME_MSI_SUMMARY_VALUE;
UINT MimeMsiSummaryProperty(MIME_MSI_DATABASE *database, UINT id, MIME_MSI_SUMMARY_VALUE *value);
UINT MimeMsiApplyTransformList(MSIHANDLE database, PCWSTR list);
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
} MIME_MSI_FEATURE;

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
    BOOL Maintenance, MaintenanceReady, ConfigureDefaults;
    HKEY MaintenanceRoot;
    WCHAR MaintenanceProduct[39];
    BOOL Is64Bit;
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
    ULONG SessionKind;
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
    PROXY_RecordGetInteger, PROXY_RecordReadStream, PROXY_GetComponentStateW, PROXY_COUNT
};
BOOL MimeMsiProxyActive(void);
UINT_PTR MimeMsiProxyCall(ULONG id, ULONG count, const UINT_PTR *arguments);
void MimeMsiShutDownHost(void);
#define MIME_ARG(x) ((UINT_PTR)(x))
/* in the custom action host, installer functions forward the call to the installer. */
#define MIME_REMOTE(id, type, ...) \
    do { \
        if (MimeMsiProxyActive()) { \
            const UINT_PTR arguments_[] = {__VA_ARGS__}; \
            return (type)MimeMsiProxyCall((id), (ULONG)(sizeof(arguments_) / sizeof(arguments_[0])), arguments_); \
        } \
    } while (0)
UINT MimeMsiRemoteRun(MIME_MSI_PACKAGE *package, MSIHANDLE session, PCWSTR dll, PCSTR entry, UINT *result);
UINT MimeMsiExtraAction(MIME_MSI_PACKAGE *package, PCWSTR action);
UINT MimeMsiRunUnlocked(MSIHANDLE handle, MIME_MSI_PACKAGE *package, PCWSTR action);
UINT MimeMsiFormat(MIME_MSI_PACKAGE *package, PCWSTR template, PWSTR *out);
UINT MimeMsiComponentState(MIME_MSI_PACKAGE *package, PCWSTR name, INSTALLSTATE *installed, INSTALLSTATE *action);
BOOL MimeMsiLogEnabled(void);
UINT MimeMsiFileTargetPath(MIME_MSI_PACKAGE *package, PCWSTR key, PWSTR *out);
UINT MimeMsiComponentDirectory(MIME_MSI_PACKAGE *package, PCWSTR name, PWSTR *out);

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

UINT MimeMsiStandardAction(MIME_MSI_PACKAGE *package, PCWSTR action);
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
UINT MimeMsiCopyInstalledFile(MIME_MSI_PACKAGE *package, PCWSTR source, PCWSTR target);
UINT MimeMsiFeatureSelected(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *features, PCWSTR name, ULONG depth, BOOL *selected);
UINT MimeMsiRegistrationAction(MIME_MSI_PACKAGE *package, PCWSTR action);
UINT MimeMsiProcessComponents(MIME_MSI_PACKAGE *package, HKEY root);
UINT MimeMsiComponentSelected(MIME_MSI_PACKAGE *package, PCWSTR name, PCWSTR *directory, BOOL *selected);
UINT MimeMsiRegistryText(HKEY key, PCWSTR name, PWSTR *out);
UINT MimeMsiCurrentSid(WCHAR sid[256]);
UINT MimeMsiRegisterSource(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR product_path);
UINT MimeMsiWriteInstalledFile(MIME_MSI_PACKAGE *package, const void *data, SIZE_T size, PCWSTR target);
UINT MimeMsiCacheTransforms(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR product_path, PCWSTR cache);
UINT MimeMsiRestoreTransforms(MSIHANDLE database, HKEY product);
UINT MimeMsiCachePatches(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR product_path, PCWSTR cache);
UINT MimeMsiRestorePatches(MSIHANDLE database, HKEY product);
UINT MimeMsiSaveDirectories(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path);
UINT MimeMsiLoadMaintenance(MIME_MSI_PACKAGE *package, HKEY key, PCWSTR product, BOOL machine);
UINT MimeMsiValidateMaintenance(MIME_MSI_PACKAGE *package);
UINT MimeMsiLoadFeatures(MIME_MSI_PACKAGE *package);
UINT MimeMsiRefreshFilePlan(MIME_MSI_PACKAGE *package);
UINT MimeMsiFeatureState(MIME_MSI_PACKAGE *package, PCWSTR name, INSTALLSTATE *installed, INSTALLSTATE *action);

UINT MimeMsiInitializeSystem(MIME_MSI_PACKAGE *package);
UINT MimeMsiSetScopeFolders(MIME_MSI_PACKAGE *package);
BOOL MimeMsiIs64BitPackage(MIME_MSI_PACKAGE *package);

WINBASEAPI BOOL WINAPI GetUserNameW(LPWSTR buffer, LPDWORD size);

#endif
