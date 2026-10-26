#ifndef MIME_MSI_AUTOMATION_H
#define MIME_MSI_AUTOMATION_H

#include "msi_private.h"
#include <mime/win32/objbase.h>
#include <mime/win32/oleauto.h>

/*
 * the scripting objects of the installer: one dispatch interface per object, whose members come from a table of
 * ids, names and handlers. there is no type library behind them.
 */

typedef struct AUTO_OBJECT AUTO_OBJECT;

/* what a member sees of a call: arguments counted from one, the value of a property put as argument zero. */
typedef struct AUTO_ARGS {
    UINT Count, NamedCount;
    DISPID *NamedIds;
    VARIANT *Values;
    VARIANT *Result;
    WORD Flags;
} AUTO_ARGS;

typedef HRESULT (*AUTO_METHOD)(AUTO_OBJECT *object, AUTO_ARGS *args);

/* Names is the member, then its parameters, separated by commas. */
typedef struct AUTO_ENTRY {
    SHORT Id;
    USHORT HelpContext;
    ULONG Flags;
    AUTO_METHOD Method;
    PCWSTR Names;
} AUTO_ENTRY;

struct AUTO_OBJECT {
    const IDispatchVtbl *lpVtbl;
    LONG References;
    const AUTO_ENTRY *Entries;
    ULONG Count;
    const GUID *Iid;
    MSIHANDLE Handle;
    void (*Destroy)(AUTO_OBJECT *object);
    /* an object of the installer, the object it came from, kept alive while this one is. */
    AUTO_OBJECT *Owner;
    /* a hook for the interfaces an object has besides its own, or null. */
    HRESULT (*QueryExtra)(AUTO_OBJECT *object, REFIID iid, void **out);
};

extern LONG MimeMsiAutoInstances;
extern HMODULE MimeMsiModule;

#define AUTO_ID_HANDLE 9999
#define AUTO_WIN32_ERROR_MASK 0xFFFF8000u

extern const GUID IID_AutoInstaller, IID_AutoRecord, IID_AutoSession, IID_AutoDatabase, IID_AutoSummaryInfo,
    IID_AutoView, IID_AutoUIPreview, IID_AutoFeatureInfo, IID_AutoRecordList, IID_AutoStringList, IID_AutoProduct,
    IID_AutoPatch, IID_AutoComponentPath, IID_AutoComponent, IID_AutoComponentClient, IID_AutoTypeLib,
    IID_AutoObjectSafety;

/* the bound of the value a call can pass, kept apart from the object it is a member of. */
VARIANT *AutoArgument(AUTO_ARGS *args, UINT index);
BOOL AutoPresent(AUTO_ARGS *args, UINT index);
HRESULT AutoGetInt(const VARIANT *value, int *out);
HRESULT AutoGetString(const VARIANT *value, PCWSTR *out);
HRESULT AutoGetBool(const VARIANT *value, BOOL *out);
BOOL AutoIsNumeric(const VARIANT *value);
MSIHANDLE AutoGetHandle(const VARIANT *value, const GUID *iid);

HRESULT AutoSetInt(AUTO_ARGS *args, int value);
HRESULT AutoSetBool(AUTO_ARGS *args, BOOL value);
HRESULT AutoSetDate(AUTO_ARGS *args, double value);
HRESULT AutoSetString(AUTO_ARGS *args, PCWSTR value);
HRESULT AutoSetBstr(AUTO_ARGS *args, BSTR value);
HRESULT AutoSetObject(AUTO_ARGS *args, AUTO_OBJECT *object);
HRESULT AutoSetEmpty(AUTO_ARGS *args);

#define AUTO_STRING_ARG(args, index, name) \
    PCWSTR name = nullptr; \
    do { \
        HRESULT hr_ = AutoGetString(AutoArgument(args, index), &name); \
        if (hr_) \
            return hr_; \
    } while (0)
#define AUTO_INT_ARG(args, index, name) \
    int name = 0; \
    do { \
        HRESULT hr_ = AutoGetInt(AutoArgument(args, index), &name); \
        if (hr_) \
            return hr_; \
    } while (0)

void AutoInit(AUTO_OBJECT *object, const AUTO_ENTRY *entries, ULONG count, const GUID *iid, MSIHANDLE handle,
              void (*destroy)(AUTO_OBJECT *));
AUTO_OBJECT *AutoAllocate(size_t size, const AUTO_ENTRY *entries, ULONG count, const GUID *iid, MSIHANDLE handle,
                          void (*destroy)(AUTO_OBJECT *));
void AutoAddRef(AUTO_OBJECT *object);
void AutoRelease(AUTO_OBJECT *object);
HRESULT AutoWin32ToHresult(UINT error);

/* the objects. each takes ownership of the handle it is given, and closes it when it fails. */
AUTO_OBJECT *AutoNewInstaller(void);
AUTO_OBJECT *AutoNewRecord(MSIHANDLE handle);
AUTO_OBJECT *AutoNewDatabase(MSIHANDLE handle);
AUTO_OBJECT *AutoNewView(MSIHANDLE handle);
AUTO_OBJECT *AutoNewSummaryInfo(MSIHANDLE handle);
AUTO_OBJECT *AutoNewUIPreview(MSIHANDLE handle);
AUTO_OBJECT *AutoNewSession(MSIHANDLE handle, AUTO_OBJECT *installer);
AUTO_OBJECT *AutoNewFeatureInfo(MSIHANDLE session, PCWSTR feature);
AUTO_OBJECT *AutoNewProduct(PCWSTR product, PCWSTR sid, DWORD context);
AUTO_OBJECT *AutoNewPatch(PCWSTR patch, PCWSTR product, PCWSTR sid, DWORD context);
AUTO_OBJECT *AutoNewComponent(PCWSTR component, PCWSTR sid, DWORD context);
AUTO_OBJECT *AutoNewComponentClient(PCWSTR component, PCWSTR product, PCWSTR sid, DWORD context);
AUTO_OBJECT *AutoNewComponentPath(PCWSTR component, PCWSTR path, INSTALLSTATE state);

extern const AUTO_ENTRY AutoInstallerEntries[];
extern const ULONG AutoInstallerCount;

/* a list of values for the scripts to count and enumerate, filled before the script sees it. */
typedef struct AUTO_LIST AUTO_LIST;
AUTO_LIST *AutoListCreate(BOOL objects);
BOOL AutoListAddString(AUTO_LIST *list, PCWSTR text, size_t length);
BOOL AutoListAddObject(AUTO_LIST *list, AUTO_OBJECT *object);
void AutoListDiscard(AUTO_LIST *list);
HRESULT AutoListReturn(AUTO_ARGS *args, AUTO_LIST *list);

/* the kinds of enumeration of strings an installer object makes. */
enum {
    AUTO_ENUM_QUALIFIERS = 1, AUTO_ENUM_PRODUCTS, AUTO_ENUM_FEATURES, AUTO_ENUM_COMPONENTS, AUTO_ENUM_CLIENTS,
    AUTO_ENUM_PATCHES, AUTO_ENUM_RELATED, AUTO_ENUM_PATCH_FILES, AUTO_ENUM_SOURCES
};
HRESULT AutoEnumerateStrings(AUTO_ARGS *args, ULONG kind, PCWSTR first, PCWSTR second, PCWSTR sid, DWORD context,
                             DWORD source_type);

/* a string that grows while the installer reports it needs more room, in the stack buffer until then. */
typedef struct AUTO_TEXT {
    WCHAR Stack[260];
    PWSTR Buffer;
    DWORD Size, Capacity;
} AUTO_TEXT;

void AutoTextBegin(AUTO_TEXT *text, DWORD size);
BOOL AutoTextGrow(AUTO_TEXT *text);
void AutoTextReset(AUTO_TEXT *text);
void AutoTextEnd(AUTO_TEXT *text);

/*
 * installer functions the objects call that the installer proper has not got everywhere yet, in auto_api.c.
 * the ones that return an error return ERROR_CALL_NOT_IMPLEMENTED until they are implemented.
 */
UINT AutoApiDatabaseCommit(MSIHANDLE database);
UINT AutoApiDatabaseImport(MSIHANDLE database, PCWSTR folder, PCWSTR file);
UINT AutoApiDatabaseExport(MSIHANDLE database, PCWSTR table, PCWSTR folder, PCWSTR file);
UINT AutoApiDatabaseMerge(MSIHANDLE database, MSIHANDLE merge, PCWSTR table);
UINT AutoApiDatabaseGenerateTransform(MSIHANDLE database, MSIHANDLE reference, PCWSTR file);
UINT AutoApiCreateTransformSummaryInfo(MSIHANDLE database, MSIHANDLE reference, PCWSTR file, int errors,
                                       int validation);
UINT AutoApiEnableUIPreview(MSIHANDLE database, MSIHANDLE *preview);
UINT AutoApiSummaryInfoSetProperty(MSIHANDLE summary, UINT id, UINT type, int number, FILETIME *time, PCWSTR text);
UINT AutoApiSummaryInfoPersist(MSIHANDLE summary);
UINT AutoApiGetFeatureValidStates(MSIHANDLE session, PCWSTR feature, DWORD *states);
UINT AutoApiSetComponentState(MSIHANDLE session, PCWSTR component, INSTALLSTATE state);
UINT AutoApiEnumComponentCosts(MSIHANDLE session, PCWSTR component, DWORD index, INSTALLSTATE state, LPWSTR drive,
                               LPDWORD drive_size, int *cost, int *temporary);
UINT AutoApiSetFeatureAttributes(MSIHANDLE session, PCWSTR feature, DWORD attributes);
UINT AutoApiCollectUserInfo(PCWSTR product);
UINT AutoApiApplyMultiplePatches(PCWSTR patches, PCWSTR product, PCWSTR command);
UINT AutoApiConfigureFeature(PCWSTR product, PCWSTR feature, INSTALLSTATE state);
UINT AutoApiReinstallFeature(PCWSTR product, PCWSTR feature, DWORD mode);
INSTALLSTATE AutoApiUseFeature(PCWSTR product, PCWSTR feature, int mode);
UINT AutoApiGetFeatureUsage(PCWSTR product, PCWSTR feature, DWORD *count, WORD *date);
UINT AutoApiProvideComponent(PCWSTR product, PCWSTR feature, PCWSTR component, DWORD mode, LPWSTR path,
                             LPDWORD size);
UINT AutoApiProvideQualifiedComponent(PCWSTR category, PCWSTR qualifier, DWORD mode, LPWSTR path, LPDWORD size);
UINT AutoApiGetQualifierDescription(PCWSTR category, PCWSTR qualifier, LPWSTR out, LPDWORD size);
UINT AutoApiEnumComponentQualifiers(PCWSTR category, DWORD index, LPWSTR qualifier, LPDWORD qualifier_size);
UINT AutoApiProvideAssembly(PCWSTR assembly, PCWSTR context, DWORD mode, DWORD info, LPWSTR path, LPDWORD size);
UINT AutoApiGetProductInfoFromScript(PCWSTR script, WCHAR product[39], int *language, DWORD *version, LPWSTR name,
                                     LPDWORD name_size, LPWSTR package, LPDWORD package_size);
UINT AutoApiAdvertiseProduct(PCWSTR package, PCWSTR script, BOOL user, PCWSTR transforms, LANGID language,
                             DWORD platform, DWORD options);
UINT AutoApiAdvertiseScript(PCWSTR script, DWORD flags, BOOL remove);
UINT AutoApiGetShortcutTarget(PCWSTR shortcut, WCHAR product[39], WCHAR feature[39], WCHAR component[39]);
/* the encoded certificate of a signed file, or its hash, in memory to free with LocalFree. */
UINT AutoApiGetFileSignature(PCWSTR path, DWORD flags, BOOL hash, BYTE **data, DWORD *size);
UINT AutoApiSourceListClearAll(PCWSTR product, PCWSTR user, DWORD reserved);
UINT AutoApiSourceListForceResolution(PCWSTR product, PCWSTR user, DWORD reserved);
UINT AutoApiGetPatchFileList(PCWSTR product, PCWSTR patches, DWORD *count, MSIHANDLE **records);
UINT AutoApiSourceListEnumMediaDisks(PCWSTR product, PCWSTR sid, DWORD context, DWORD options, DWORD index,
                                     DWORD *disk, LPWSTR label, LPDWORD label_size, LPWSTR prompt,
                                     LPDWORD prompt_size);
UINT AutoApiSourceListAddMediaDisk(PCWSTR product, PCWSTR sid, DWORD context, DWORD options, DWORD disk,
                                   PCWSTR label, PCWSTR prompt);
UINT AutoApiSourceListClearSource(PCWSTR product, PCWSTR sid, DWORD context, DWORD options, PCWSTR source);
UINT AutoApiSourceListClearMediaDisk(PCWSTR product, PCWSTR sid, DWORD context, DWORD options, DWORD disk);
UINT AutoApiSourceListClearAllEx(PCWSTR product, PCWSTR sid, DWORD context, DWORD options);
UINT AutoApiSourceListForceResolutionEx(PCWSTR product, PCWSTR sid, DWORD context, DWORD options);

#endif
