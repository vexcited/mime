#ifndef MIME_MSI_PRIVATE_H
#define MIME_MSI_PRIVATE_H

#define MSIAPI DECLSPEC_EXPORT
#include <mime/win32/msi.h>
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

enum { FIELD_NULL, FIELD_INTEGER, FIELD_STRING };
typedef struct MIME_MSI_FIELD {
    ULONG Kind;
    int Integer;
    PWSTR String;
} MIME_MSI_FIELD;

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
} MIME_MSI_DATABASE;

typedef struct MIME_MSI_TRANSFORM {
    struct MIME_MSI_TRANSFORM *Next;
    MIME_MSI_DATABASE *Database;
    UINT Flags;
} MIME_MSI_TRANSFORM;

UINT MimeMsiLoadStrings(MIME_MSI_DATABASE *database);
UINT MimeMsiOpenDatabaseFile(PCWSTR path, BOOL transform, MIME_MSI_DATABASE **out);
UINT MimeMsiOpenStorageFile(PCWSTR path, BYTE kind, MIME_MSI_DATABASE **out);
UINT MimeMsiCreateSummary(MIME_MSI_DATABASE *database, MSIHANDLE *out);
UINT MimeMsiApplyTransformList(MSIHANDLE database, PCWSTR list);
UINT MimeMsiLoadTable(MIME_MSI_DATABASE *database, PCWSTR name, MIME_MSI_TABLE **out);
UINT MimeMsiReadStream(MIME_MSI_DATABASE *database, PCWSTR name, BYTE **out, SIZE_T *size);
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
} MIME_MSI_PACKAGE;

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
UINT MimeMsiDirectoryPath(MIME_MSI_PACKAGE *package, PCWSTR name, BOOL source, PWSTR *out);

UINT MimeMsiStandardAction(MIME_MSI_PACKAGE *package, PCWSTR action);
UINT MimeMsiFinishTransaction(MIME_MSI_PACKAGE *package, BOOL commit);

UINT MimeMsiCatalogPath(PCWSTR product, WCHAR path[96]);

UINT MimeMsiWriteRegistry(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path, PCWSTR name,
                         DWORD type, const void *data, DWORD bytes);
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
UINT MimeMsiSaveDirectories(MIME_MSI_PACKAGE *package, HKEY root, PCWSTR path);
UINT MimeMsiLoadMaintenance(MIME_MSI_PACKAGE *package, HKEY key, PCWSTR product, BOOL machine);
UINT MimeMsiValidateMaintenance(MIME_MSI_PACKAGE *package);
UINT MimeMsiLoadFeatures(MIME_MSI_PACKAGE *package);
UINT MimeMsiRefreshFilePlan(MIME_MSI_PACKAGE *package);
UINT MimeMsiFeatureState(MIME_MSI_PACKAGE *package, PCWSTR name, INSTALLSTATE *installed, INSTALLSTATE *action);

#endif
