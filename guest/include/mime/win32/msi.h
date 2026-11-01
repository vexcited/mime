#ifndef MIME_WIN32_MSI_H
#define MIME_WIN32_MSI_H

#include <mime/win32/winbase.h>

#ifndef MSIAPI
#define MSIAPI DECLSPEC_IMPORT
#endif

typedef UINT MSIHANDLE;
#define MSI_NULL_INTEGER ((int)0x80000000u)
#define MSIDBOPEN_READONLY ((LPCWSTR)0)
#define MSIDBOPEN_PATCHFILE 32
#define ERROR_BAD_QUERY_SYNTAX 1615L
#define ERROR_INVALID_TABLE 1628L
#define ERROR_BAD_CONFIGURATION 1610L
#define ERROR_FUNCTION_FAILED 1627L
#define ERROR_INSTALL_PACKAGE_INVALID 1620L
#define ERROR_INSTALL_PACKAGE_OPEN_FAILED 1619L
#define ERROR_INSTALL_TRANSFORM_FAILURE 1624L
#ifndef ERROR_OPEN_FAILED
#define ERROR_OPEN_FAILED 110L
#endif
#define ERROR_INSTALL_SOURCE_ABSENT 1612L
#define ERROR_UNKNOWN_FEATURE 1606L
#define ERROR_UNKNOWN_COMPONENT 1607L
#define ERROR_PATCH_PACKAGE_OPEN_FAILED 1635L
#define ERROR_PATCH_PACKAGE_INVALID 1636L
#define ERROR_PATCH_PACKAGE_UNSUPPORTED 1637L
#define ERROR_PATCH_TARGET_NOT_FOUND 1642L

typedef enum {
    INSTALLTYPE_DEFAULT, INSTALLTYPE_NETWORK_IMAGE, INSTALLTYPE_SINGLE_INSTANCE
} INSTALLTYPE;

MSIAPI UINT WINAPI MsiApplyPatchW(LPCWSTR patch, LPCWSTR package, INSTALLTYPE type, LPCWSTR command);

MSIAPI UINT WINAPI MsiApplyMultiplePatchesW(LPCWSTR patches, LPCWSTR product, LPCWSTR command);
MSIAPI UINT WINAPI MsiApplyMultiplePatchesA(LPCSTR patches, LPCSTR product, LPCSTR command);
MSIAPI UINT WINAPI MsiApplyPatchA(LPCSTR patch, LPCSTR package, INSTALLTYPE type, LPCSTR command);

/* who an advertisement is for, in the place of a script file. */
#define ADVERTISEFLAGS_MACHINEASSIGN 0
#define ADVERTISEFLAGS_USERASSIGN 1
MSIAPI UINT WINAPI MsiAdvertiseProductW(LPCWSTR package, LPCWSTR script, LPCWSTR transforms, LANGID language);
MSIAPI UINT WINAPI MsiAdvertiseProductA(LPCSTR package, LPCSTR script, LPCSTR transforms, LANGID language);
MSIAPI UINT WINAPI MsiAdvertiseProductExW(LPCWSTR package, LPCWSTR script, LPCWSTR transforms, LANGID language, DWORD platform,
                                          DWORD options);
MSIAPI UINT WINAPI MsiAdvertiseProductExA(LPCSTR package, LPCSTR script, LPCSTR transforms, LANGID language, DWORD platform,
                                          DWORD options);

typedef enum {
    INSTALLFEATUREATTRIBUTE_FAVORLOCAL = 1,
    INSTALLFEATUREATTRIBUTE_FAVORSOURCE = 2,
    INSTALLFEATUREATTRIBUTE_FOLLOWPARENT = 4,
    INSTALLFEATUREATTRIBUTE_FAVORADVERTISE = 8,
    INSTALLFEATUREATTRIBUTE_DISALLOWADVERTISE = 16,
    INSTALLFEATUREATTRIBUTE_NOUNSUPPORTEDADVERTISE = 32
} INSTALLFEATUREATTRIBUTE;

MSIAPI UINT WINAPI MsiGetFeatureInfoW(MSIHANDLE product, LPCWSTR feature, LPDWORD attributes,
                                     LPWSTR title, LPDWORD title_size, LPWSTR help, LPDWORD help_size);

MSIAPI MSIHANDLE WINAPI MsiCreateRecord(UINT fields);
MSIAPI UINT WINAPI MsiCloseHandle(MSIHANDLE handle);
MSIAPI UINT WINAPI MsiCloseAllHandles(void);
MSIAPI UINT WINAPI MsiRecordGetFieldCount(MSIHANDLE record);
MSIAPI int WINAPI MsiRecordGetInteger(MSIHANDLE record, UINT field);
MSIAPI UINT WINAPI MsiRecordGetStringW(MSIHANDLE record, UINT field, LPWSTR out, LPDWORD size);
MSIAPI UINT WINAPI MsiRecordGetStringA(MSIHANDLE record, UINT field, LPSTR out, LPDWORD size);
MSIAPI UINT WINAPI MsiRecordSetInteger(MSIHANDLE record, UINT field, int value);
MSIAPI UINT WINAPI MsiRecordSetStringW(MSIHANDLE record, UINT field, LPCWSTR value);
MSIAPI BOOL WINAPI MsiRecordIsNull(MSIHANDLE record, UINT field);
MSIAPI UINT WINAPI MsiRecordClearData(MSIHANDLE record);
MSIAPI UINT WINAPI MsiRecordDataSize(MSIHANDLE record, UINT field);
MSIAPI UINT WINAPI MsiOpenDatabaseW(LPCWSTR path, LPCWSTR persist, MSIHANDLE *database);
MSIAPI UINT WINAPI MsiOpenDatabaseA(LPCSTR path, LPCSTR persist, MSIHANDLE *database);

MSIAPI UINT WINAPI MsiDatabaseOpenViewW(MSIHANDLE database, LPCWSTR query, MSIHANDLE *view);
MSIAPI UINT WINAPI MsiViewExecute(MSIHANDLE view, MSIHANDLE parameters);
MSIAPI UINT WINAPI MsiViewFetch(MSIHANDLE view, MSIHANDLE *record);
MSIAPI UINT WINAPI MsiViewClose(MSIHANDLE view);
MSIAPI UINT WINAPI MsiViewModify(MSIHANDLE view, int mode, MSIHANDLE record);
MSIAPI UINT WINAPI MsiRecordReadStream(MSIHANDLE record, UINT field, char *buffer, LPDWORD size);
MSIAPI UINT WINAPI MsiRecordSetStreamW(MSIHANDLE record, UINT field, LPCWSTR path);
MSIAPI UINT WINAPI MsiRecordSetStreamA(MSIHANDLE record, UINT field, LPCSTR path);
MSIAPI BOOL WINAPI MsiGetMode(MSIHANDLE install, UINT mode);
MSIAPI UINT WINAPI MsiSetMode(MSIHANDLE install, UINT mode, BOOL state);
#define MSICOLINFO_NAMES 0
#define MSICOLINFO_TYPES 1
MSIAPI UINT WINAPI MsiViewGetColumnInfo(MSIHANDLE view, UINT info, MSIHANDLE *record);
MSIAPI UINT WINAPI MsiDatabaseGetPrimaryKeysW(MSIHANDLE database, LPCWSTR table, MSIHANDLE *record);
MSIAPI UINT WINAPI MsiViewGetErrorW(MSIHANDLE view, LPWSTR column, LPDWORD size);
MSIAPI UINT WINAPI MsiViewGetErrorA(MSIHANDLE view, LPSTR column, LPDWORD size);
MSIAPI UINT WINAPI MsiDatabaseGetPrimaryKeysA(MSIHANDLE database, LPCSTR table, MSIHANDLE *record);

#define MSITRANSFORM_ERROR_ADDEXISTINGROW 0x01
#define MSITRANSFORM_ERROR_DELMISSINGROW 0x02
#define MSITRANSFORM_ERROR_ADDEXISTINGTABLE 0x04
#define MSITRANSFORM_ERROR_DELMISSINGTABLE 0x08
#define MSITRANSFORM_ERROR_UPDATEMISSINGROW 0x10
#define MSITRANSFORM_ERROR_CHANGECODEPAGE 0x20
#define MSITRANSFORM_ERROR_VIEWTRANSFORM 0x100
MSIAPI UINT WINAPI MsiDatabaseApplyTransformW(MSIHANDLE database, LPCWSTR path, int flags);

MSIAPI UINT WINAPI MsiEnumRelatedProductsW(LPCWSTR upgrade, DWORD reserved, DWORD index, LPWSTR product);

#define MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE 1
MSIAPI UINT WINAPI MsiOpenPackageW(LPCWSTR path, MSIHANDLE *package);
MSIAPI UINT WINAPI MsiOpenPackageExW(LPCWSTR path, DWORD options, MSIHANDLE *package);
MSIAPI MSIHANDLE WINAPI MsiGetActiveDatabase(MSIHANDLE package);
MSIAPI UINT WINAPI MsiGetPropertyW(MSIHANDLE package, LPCWSTR name, LPWSTR out, LPDWORD size);
MSIAPI UINT WINAPI MsiSetPropertyW(MSIHANDLE package, LPCWSTR name, LPCWSTR value);

MSIAPI UINT WINAPI MsiFormatRecordW(MSIHANDLE install, MSIHANDLE record, LPWSTR out, LPDWORD size);

typedef enum {
    INSTALLUILEVEL_NOCHANGE, INSTALLUILEVEL_DEFAULT, INSTALLUILEVEL_NONE,
    INSTALLUILEVEL_BASIC, INSTALLUILEVEL_REDUCED, INSTALLUILEVEL_FULL,
    INSTALLUILEVEL_HIDECANCEL = 0x20, INSTALLUILEVEL_PROGRESSONLY = 0x40,
    INSTALLUILEVEL_ENDDIALOG = 0x80, INSTALLUILEVEL_SOURCERESONLY = 0x100,
    INSTALLUILEVEL_UACONLY = 0x200
} INSTALLUILEVEL;

typedef enum {
    INSTALLMESSAGE_FATALEXIT = 0, INSTALLMESSAGE_ERROR = 0x01000000,
    INSTALLMESSAGE_WARNING = 0x02000000, INSTALLMESSAGE_USER = 0x03000000,
    INSTALLMESSAGE_INFO = 0x04000000, INSTALLMESSAGE_FILESINUSE = 0x05000000,
    INSTALLMESSAGE_RESOLVESOURCE = 0x06000000, INSTALLMESSAGE_OUTOFDISKSPACE = 0x07000000,
    INSTALLMESSAGE_ACTIONSTART = 0x08000000, INSTALLMESSAGE_ACTIONDATA = 0x09000000,
    INSTALLMESSAGE_PROGRESS = 0x0a000000, INSTALLMESSAGE_COMMONDATA = 0x0b000000,
    INSTALLMESSAGE_INITIALIZE = 0x0c000000, INSTALLMESSAGE_TERMINATE = 0x0d000000,
    INSTALLMESSAGE_SHOWDIALOG = 0x0e000000, INSTALLMESSAGE_PERFORMANCE = 0x0f000000,
    INSTALLMESSAGE_RMFILESINUSE = 0x19000000, INSTALLMESSAGE_INSTALLSTART = 0x1a000000,
    INSTALLMESSAGE_INSTALLEND = 0x1b000000
} INSTALLMESSAGE;

#define INSTALLLOGMODE_INFO 0x10
#define INSTALLLOGMODE_VERBOSE 0x1000
#define INSTALLLOGMODE_LOGONLYONERROR 0x4000
#define INSTALLLOGATTRIBUTES_APPEND 1
#define INSTALLLOGATTRIBUTES_FLUSHEACHLINE 2
#define ERROR_INSTALL_LOG_FAILURE 1622L

typedef int (WINAPI *INSTALLUI_HANDLERA)(LPVOID, UINT, LPCSTR);
typedef int (WINAPI *INSTALLUI_HANDLERW)(LPVOID, UINT, LPCWSTR);
typedef int (WINAPI *INSTALLUI_HANDLER_RECORD)(LPVOID, UINT, MSIHANDLE);
typedef INSTALLUI_HANDLER_RECORD *PINSTALLUI_HANDLER_RECORD;
MSIAPI INSTALLUILEVEL WINAPI MsiSetInternalUI(INSTALLUILEVEL level, HWND *window);
MSIAPI INSTALLUI_HANDLERA WINAPI MsiSetExternalUIA(INSTALLUI_HANDLERA handler, DWORD filter, LPVOID context);
MSIAPI INSTALLUI_HANDLERW WINAPI MsiSetExternalUIW(INSTALLUI_HANDLERW handler, DWORD filter, LPVOID context);
MSIAPI UINT WINAPI MsiSetExternalUIRecord(INSTALLUI_HANDLER_RECORD handler, DWORD filter, LPVOID context,
                                         PINSTALLUI_HANDLER_RECORD previous);
MSIAPI UINT WINAPI MsiEnableLogW(DWORD mode, LPCWSTR path, DWORD attributes);
MSIAPI int WINAPI MsiProcessMessage(MSIHANDLE install, INSTALLMESSAGE type, MSIHANDLE record);

typedef enum { MSICONDITION_FALSE, MSICONDITION_TRUE, MSICONDITION_NONE, MSICONDITION_ERROR } MSICONDITION;
MSIAPI MSICONDITION WINAPI MsiEvaluateConditionW(MSIHANDLE install, LPCWSTR condition);

#define ERROR_INSTALL_USEREXIT 1602L
#define ERROR_INSTALL_FAILURE 1603L
#define ERROR_INSTALL_SUSPEND 1604L
#define ERROR_INVALID_HANDLE_STATE 1609L
#define ERROR_INSTALL_ALREADY_RUNNING 1618L
#define ERROR_FUNCTION_NOT_CALLED 1626L
MSIAPI UINT WINAPI MsiDoActionW(MSIHANDLE install, LPCWSTR action);
MSIAPI UINT WINAPI MsiSequenceW(MSIHANDLE install, LPCWSTR table, int mode);
MSIAPI UINT WINAPI MsiSequenceA(MSIHANDLE install, LPCSTR table, int mode);

MSIAPI UINT WINAPI MsiGetTargetPathW(MSIHANDLE install, LPCWSTR folder, LPWSTR path, LPDWORD size);
MSIAPI UINT WINAPI MsiGetTargetPathA(MSIHANDLE install, LPCSTR folder, LPSTR path, LPDWORD size);
MSIAPI UINT WINAPI MsiSetTargetPathA(MSIHANDLE install, LPCSTR folder, LPCSTR path);
MSIAPI UINT WINAPI MsiGetSourcePathW(MSIHANDLE install, LPCWSTR folder, LPWSTR path, LPDWORD size);
MSIAPI UINT WINAPI MsiSetTargetPathW(MSIHANDLE install, LPCWSTR folder, LPCWSTR path);

MSIAPI UINT WINAPI MsiGetSummaryInformationW(MSIHANDLE database, LPCWSTR path, UINT updates, MSIHANDLE *summary);
MSIAPI UINT WINAPI MsiSummaryInfoGetPropertyW(MSIHANDLE summary, UINT id, PUINT type, int *number,
                                            FILETIME *time, LPWSTR text, LPDWORD size);
MSIAPI UINT WINAPI MsiSummaryInfoGetPropertyCount(MSIHANDLE summary, PUINT count);
MSIAPI UINT WINAPI MsiGetSummaryInformationA(MSIHANDLE database, LPCSTR path, UINT updates, MSIHANDLE *summary);
MSIAPI UINT WINAPI MsiSummaryInfoGetPropertyA(MSIHANDLE summary, UINT id, PUINT type, int *number, FILETIME *time,
                                            LPSTR text, LPDWORD size);
MSIAPI UINT WINAPI MsiSummaryInfoSetPropertyW(MSIHANDLE summary, UINT id, UINT type, int number, FILETIME *time,
                                            LPCWSTR text);
MSIAPI UINT WINAPI MsiSummaryInfoSetPropertyA(MSIHANDLE summary, UINT id, UINT type, int number, FILETIME *time,
                                            LPCSTR text);
MSIAPI UINT WINAPI MsiSummaryInfoPersist(MSIHANDLE summary);
MSIAPI UINT WINAPI MsiDatabaseCommit(MSIHANDLE database);
MSIAPI UINT WINAPI MsiDatabaseGenerateTransformW(MSIHANDLE database, MSIHANDLE reference, LPCWSTR file, int reserved1,
                                               int reserved2);
MSIAPI UINT WINAPI MsiDatabaseGenerateTransformA(MSIHANDLE database, MSIHANDLE reference, LPCSTR file, int reserved1,
                                               int reserved2);
MSIAPI UINT WINAPI MsiCreateTransformSummaryInfoW(MSIHANDLE database, MSIHANDLE reference, LPCWSTR file, int errors,
                                                int validation);
MSIAPI UINT WINAPI MsiCreateTransformSummaryInfoA(MSIHANDLE database, MSIHANDLE reference, LPCSTR file, int errors,
                                                int validation);

#define ERROR_UNKNOWN_PROPERTY 1608L
MSIAPI UINT WINAPI MsiGetPatchInfoW(LPCWSTR patch, LPCWSTR attribute, LPWSTR out, LPDWORD size);
MSIAPI UINT WINAPI MsiEnumPatchesW(LPCWSTR product, DWORD index, LPWSTR patch, LPWSTR transforms, LPDWORD size);

#define ERROR_INVALID_COMMAND_LINE 1639L
MSIAPI UINT WINAPI MsiInstallProductW(LPCWSTR path, LPCWSTR command);

typedef enum {
    INSTALLSTATE_NOTUSED = -7, INSTALLSTATE_BADCONFIG, INSTALLSTATE_INCOMPLETE,
    INSTALLSTATE_SOURCEABSENT, INSTALLSTATE_MOREDATA, INSTALLSTATE_INVALIDARG,
    INSTALLSTATE_UNKNOWN, INSTALLSTATE_BROKEN, INSTALLSTATE_ADVERTISED,
    INSTALLSTATE_ABSENT, INSTALLSTATE_LOCAL, INSTALLSTATE_SOURCE, INSTALLSTATE_DEFAULT
} INSTALLSTATE;
MSIAPI INSTALLSTATE WINAPI MsiQueryProductStateW(LPCWSTR product);
#define REINSTALLMODE_REPAIR 0x0001
#define REINSTALLMODE_FILEMISSING 0x0002
#define REINSTALLMODE_FILEOLDERVERSION 0x0004
#define REINSTALLMODE_FILEEQUALVERSION 0x0008
#define REINSTALLMODE_FILEEXACT 0x0010
#define REINSTALLMODE_FILEVERIFY 0x0020
#define REINSTALLMODE_FILEREPLACE 0x0040
#define REINSTALLMODE_MACHINEDATA 0x0080
#define REINSTALLMODE_USERDATA 0x0100
#define REINSTALLMODE_SHORTCUT 0x0200
#define REINSTALLMODE_PACKAGE 0x0400
MSIAPI UINT WINAPI MsiReinstallProductW(LPCWSTR product, DWORD mode);
MSIAPI UINT WINAPI MsiReinstallFeatureW(LPCWSTR product, LPCWSTR feature, DWORD mode);
MSIAPI UINT WINAPI MsiConfigureFeatureW(LPCWSTR product, LPCWSTR feature, INSTALLSTATE state);
MSIAPI UINT WINAPI MsiConfigureFeatureA(LPCSTR product, LPCSTR feature, INSTALLSTATE state);
MSIAPI UINT WINAPI MsiConfigureProductW(LPCWSTR product, int level, INSTALLSTATE state);
MSIAPI UINT WINAPI MsiConfigureProductExW(LPCWSTR product, int level, INSTALLSTATE state, LPCWSTR command);
MSIAPI INSTALLSTATE WINAPI MsiQueryFeatureStateW(LPCWSTR product, LPCWSTR feature);
MSIAPI UINT WINAPI MsiGetProductInfoW(LPCWSTR product, LPCWSTR attribute, LPWSTR out, LPDWORD size);
MSIAPI UINT WINAPI MsiEnumFeaturesW(LPCWSTR product, DWORD index, LPWSTR feature, LPWSTR parent);
MSIAPI UINT WINAPI MsiOpenProductW(LPCWSTR product, MSIHANDLE *out);
MSIAPI UINT WINAPI MsiEnumComponentsW(DWORD index, LPWSTR component);
MSIAPI UINT WINAPI MsiEnumClientsW(LPCWSTR component, DWORD index, LPWSTR product);
MSIAPI INSTALLSTATE WINAPI MsiGetComponentPathW(LPCWSTR product, LPCWSTR component, LPWSTR out, LPDWORD size);
MSIAPI INSTALLSTATE WINAPI MsiLocateComponentW(LPCWSTR component, LPWSTR out, LPDWORD size);
MSIAPI INSTALLSTATE WINAPI MsiLocateComponentA(LPCSTR component, LPSTR out, LPDWORD size);
#define ERROR_INDEX_ABSENT 1611L
#define MAX_FEATURE_CHARS 38
typedef enum tagINSTALLMODE {
    INSTALLMODE_NODETECTION_ANY = -4, INSTALLMODE_NOSOURCERESOLUTION = -3, INSTALLMODE_NODETECTION = -2,
    INSTALLMODE_EXISTING = -1, INSTALLMODE_DEFAULT = 0
} INSTALLMODE;
MSIAPI UINT WINAPI MsiDecomposeDescriptorW(LPCWSTR descriptor, LPWSTR product, LPWSTR feature, LPWSTR component,
                                           LPDWORD used);
MSIAPI UINT WINAPI MsiDecomposeDescriptorA(LPCSTR descriptor, LPSTR product, LPSTR feature, LPSTR component,
                                           LPDWORD used);
MSIAPI UINT WINAPI MsiProvideComponentW(LPCWSTR product, LPCWSTR feature, LPCWSTR component, DWORD mode, LPWSTR path,
                                        LPDWORD size);
MSIAPI UINT WINAPI MsiProvideComponentA(LPCSTR product, LPCSTR feature, LPCSTR component, DWORD mode, LPSTR path,
                                        LPDWORD size);
MSIAPI UINT WINAPI MsiProvideComponentFromDescriptorW(LPCWSTR descriptor, LPWSTR path, LPDWORD size, LPDWORD args);
MSIAPI UINT WINAPI MsiGetShortcutTargetW(LPCWSTR shortcut, LPWSTR product, LPWSTR feature, LPWSTR component);
MSIAPI UINT WINAPI MsiGetShortcutTargetA(LPCSTR shortcut, LPSTR product, LPSTR feature, LPSTR component);
MSIAPI UINT WINAPI MsiProvideComponentFromDescriptorA(LPCSTR descriptor, LPSTR path, LPDWORD size, LPDWORD args);
MSIAPI UINT WINAPI MsiProvideQualifiedComponentW(LPCWSTR category, LPCWSTR qualifier, DWORD mode, LPWSTR path,
                                                 LPDWORD size);
MSIAPI UINT WINAPI MsiProvideQualifiedComponentA(LPCSTR category, LPCSTR qualifier, DWORD mode, LPSTR path,
                                                 LPDWORD size);
MSIAPI UINT WINAPI MsiProvideQualifiedComponentExW(LPCWSTR category, LPCWSTR qualifier, DWORD mode, LPCWSTR product,
                                                   DWORD unused1, DWORD unused2, LPWSTR path, LPDWORD size);
MSIAPI UINT WINAPI MsiProvideQualifiedComponentExA(LPCSTR category, LPCSTR qualifier, DWORD mode, LPCSTR product,
                                                   DWORD unused1, DWORD unused2, LPSTR path, LPDWORD size);
MSIAPI UINT WINAPI MsiEnumComponentQualifiersW(LPCWSTR category, DWORD index, LPWSTR qualifier, LPDWORD qualifier_size,
                                               LPWSTR data, LPDWORD data_size);
MSIAPI UINT WINAPI MsiEnumComponentQualifiersA(LPCSTR category, DWORD index, LPSTR qualifier, LPDWORD qualifier_size,
                                               LPSTR data, LPDWORD data_size);
MSIAPI UINT WINAPI MsiEnumProductsW(DWORD index, LPWSTR product);
MSIAPI UINT WINAPI MsiEnumProductsA(DWORD index, LPSTR product);
MSIAPI UINT WINAPI MsiRemovePatchesW(LPCWSTR patches, LPCWSTR product, INSTALLTYPE type, LPCWSTR command);
MSIAPI UINT WINAPI MsiRemovePatchesA(LPCSTR patches, LPCSTR product, INSTALLTYPE type, LPCSTR command);
MSIAPI UINT WINAPI MsiGetComponentStateW(MSIHANDLE install, LPCWSTR component, INSTALLSTATE *installed, INSTALLSTATE *action);
MSIAPI UINT WINAPI MsiGetFeatureStateW(MSIHANDLE install, LPCWSTR feature, INSTALLSTATE *installed, INSTALLSTATE *action);
MSIAPI UINT WINAPI MsiSetFeatureStateW(MSIHANDLE install, LPCWSTR feature, INSTALLSTATE state);
MSIAPI UINT WINAPI MsiSetInstallLevel(MSIHANDLE install, int level);
MSIAPI LANGID WINAPI MsiGetLanguage(MSIHANDLE install);
typedef enum { MSICOSTTREE_SELFONLY, MSICOSTTREE_CHILDREN, MSICOSTTREE_PARENTS, MSICOSTTREE_RESERVED } MSICOSTTREE;
MSIAPI UINT WINAPI MsiGetFeatureCostW(MSIHANDLE install, LPCWSTR feature, MSICOSTTREE tree, INSTALLSTATE state, LPINT cost);
MSIAPI UINT WINAPI MsiGetFeatureCostA(MSIHANDLE install, LPCSTR feature, MSICOSTTREE tree, INSTALLSTATE state, LPINT cost);
MSIAPI UINT WINAPI MsiGetFeatureStateA(MSIHANDLE install, LPCSTR feature, INSTALLSTATE *installed, INSTALLSTATE *action);
MSIAPI UINT WINAPI MsiSetFeatureStateA(MSIHANDLE install, LPCSTR feature, INSTALLSTATE state);
MSIAPI UINT WINAPI MsiSetComponentStateW(MSIHANDLE install, LPCWSTR component, INSTALLSTATE state);
MSIAPI UINT WINAPI MsiSetComponentStateA(MSIHANDLE install, LPCSTR component, INSTALLSTATE state);
MSIAPI UINT WINAPI MsiSetFeatureAttributesW(MSIHANDLE install, LPCWSTR feature, DWORD attributes);
MSIAPI UINT WINAPI MsiSetFeatureAttributesA(MSIHANDLE install, LPCSTR feature, DWORD attributes);
MSIAPI UINT WINAPI MsiGetFeatureValidStatesW(MSIHANDLE install, LPCWSTR feature, LPDWORD states);
MSIAPI UINT WINAPI MsiGetFeatureValidStatesA(MSIHANDLE install, LPCSTR feature, LPDWORD states);
MSIAPI UINT WINAPI MsiEnumComponentCostsW(MSIHANDLE install, LPCWSTR component, DWORD index, INSTALLSTATE state,
                                          LPWSTR drive, LPDWORD drive_size, LPINT cost, LPINT temporary);
MSIAPI UINT WINAPI MsiEnumComponentCostsA(MSIHANDLE install, LPCSTR component, DWORD index, INSTALLSTATE state,
                                          LPSTR drive, LPDWORD drive_size, LPINT cost, LPINT temporary);

typedef enum {
    MSIINSTALLCONTEXT_NONE = 0, MSIINSTALLCONTEXT_USERMANAGED = 1,
    MSIINSTALLCONTEXT_USERUNMANAGED = 2, MSIINSTALLCONTEXT_MACHINE = 4,
    MSIINSTALLCONTEXT_ALL = 7
} MSIINSTALLCONTEXT;
#define MSICODE_PRODUCT 0
#define MSICODE_PATCH 0x40000000
#define MSISOURCETYPE_NETWORK 1
#define MSISOURCETYPE_URL 2
#define MSISOURCETYPE_MEDIA 4
#define ERROR_UNKNOWN_PATCH 1647L
#define ERROR_BAD_USERNAME 2202L
MSIAPI UINT WINAPI MsiSourceListAddSourceW(LPCWSTR product, LPCWSTR user, DWORD reserved, LPCWSTR source);
MSIAPI UINT WINAPI MsiSourceListAddSourceExW(LPCWSTR product, LPCWSTR sid, MSIINSTALLCONTEXT context,
                                           DWORD options, LPCWSTR source, DWORD index);
MSIAPI UINT WINAPI MsiSourceListAddSourceExA(LPCSTR product, LPCSTR sid, MSIINSTALLCONTEXT context,
                                           DWORD options, LPCSTR source, DWORD index);
MSIAPI UINT WINAPI MsiSourceListSetInfoW(LPCWSTR product, LPCWSTR sid, MSIINSTALLCONTEXT context,
                                       DWORD options, LPCWSTR property, LPCWSTR value);
MSIAPI UINT WINAPI MsiSourceListGetInfoW(LPCWSTR product, LPCWSTR sid, MSIINSTALLCONTEXT context,
                                       DWORD options, LPCWSTR property, LPWSTR out, LPDWORD size);
MSIAPI UINT WINAPI MsiSourceListEnumSourcesW(LPCWSTR product, LPCWSTR sid, MSIINSTALLCONTEXT context,
                                           DWORD options, DWORD index, LPWSTR out, LPDWORD size);


#define MSIPATCHSTATE_APPLIED 1
#define MSIPATCHSTATE_SUPERSEDED 2
#define MSIPATCHSTATE_OBSOLETED 4
#define MSIPATCHSTATE_REGISTERED 8
#define MSIPATCHSTATE_ALL 15
typedef enum { MSIPATCH_DATATYPE_PATCHFILE = 0, MSIPATCH_DATATYPE_XMLPATH, MSIPATCH_DATATYPE_XMLBLOB } MSIPATCHDATATYPE;
typedef struct {
    LPCWSTR szPatchData;
    MSIPATCHDATATYPE ePatchDataType;
    DWORD dwOrder;
    UINT uStatus;
} MSIPATCHSEQUENCEINFOW;
MSIAPI UINT WINAPI MsiEnumProductsExW(LPCWSTR product, LPCWSTR sid, DWORD contexts, DWORD index, WCHAR installed[39],
                                      MSIINSTALLCONTEXT *context, LPWSTR user_sid, LPDWORD sid_size);
MSIAPI UINT WINAPI MsiGetFileVersionW(LPCWSTR path, LPWSTR version, LPDWORD version_size, LPWSTR language,
                                      LPDWORD language_size);
MSIAPI UINT WINAPI MsiIsProductElevatedW(LPCWSTR product, BOOL *elevated);
MSIAPI UINT WINAPI MsiDeterminePatchSequenceW(LPCWSTR product, LPCWSTR sid, MSIINSTALLCONTEXT context, DWORD count,
                                              MSIPATCHSEQUENCEINFOW *patches);
MSIAPI UINT WINAPI MsiEnumPatchesExW(LPCWSTR product, LPCWSTR sid, DWORD contexts, DWORD filter, DWORD index,
                                     WCHAR patch[39], WCHAR target[39], MSIINSTALLCONTEXT *context, LPWSTR user_sid,
                                     LPDWORD sid_size);
MSIAPI UINT WINAPI MsiGetPatchInfoExW(LPCWSTR patch, LPCWSTR product, LPCWSTR sid, MSIINSTALLCONTEXT context,
                                      LPCWSTR property, LPWSTR value, LPDWORD size);
MSIAPI UINT WINAPI MsiExtractPatchXMLDataW(LPCWSTR patch, DWORD reserved, LPWSTR xml, LPDWORD size);


MSIAPI UINT WINAPI MsiGetProductInfoExW(LPCWSTR product, LPCWSTR sid, MSIINSTALLCONTEXT context, LPCWSTR property,
                                        LPWSTR value, LPDWORD size);
MSIAPI UINT WINAPI MsiQueryFeatureStateExW(LPCWSTR product, LPCWSTR sid, MSIINSTALLCONTEXT context, LPCWSTR feature,
                                           INSTALLSTATE *state);
MSIAPI UINT WINAPI MsiQueryComponentStateW(LPCWSTR product, LPCWSTR sid, MSIINSTALLCONTEXT context, LPCWSTR component,
                                           INSTALLSTATE *state);
MSIAPI INSTALLSTATE WINAPI MsiGetComponentPathExW(LPCWSTR product, LPCWSTR component, LPCWSTR sid,
                                                  MSIINSTALLCONTEXT context, LPWSTR path, LPDWORD size);
MSIAPI UINT WINAPI MsiEnumClientsExW(LPCWSTR component, LPCWSTR sid, DWORD contexts, DWORD index, WCHAR product[39],
                                     MSIINSTALLCONTEXT *context, LPWSTR user_sid, LPDWORD sid_size);
MSIAPI UINT WINAPI MsiEnumComponentsExW(LPCWSTR sid, DWORD contexts, DWORD index, WCHAR component[39],
                                        MSIINSTALLCONTEXT *context, LPWSTR user_sid, LPDWORD sid_size);
MSIAPI UINT WINAPI MsiGetProductCodeW(LPCWSTR component, LPWSTR product);
MSIAPI UINT WINAPI MsiGetProductCodeA(LPCSTR component, LPSTR product);
MSIAPI INSTALLSTATE WINAPI MsiGetComponentPathA(LPCSTR product, LPCSTR component, LPSTR out, LPDWORD size);
MSIAPI UINT WINAPI MsiInstallMissingComponentW(LPCWSTR product, LPCWSTR component, INSTALLSTATE state);
MSIAPI UINT WINAPI MsiInstallMissingComponentA(LPCSTR product, LPCSTR component, INSTALLSTATE state);
MSIAPI int WINAPI MsiGetDatabaseState(MSIHANDLE database);
MSIAPI MSICONDITION WINAPI MsiDatabaseIsTablePersistentW(MSIHANDLE database, LPCWSTR table);
MSIAPI MSICONDITION WINAPI MsiDatabaseIsTablePersistentA(MSIHANDLE database, LPCSTR table);
MSIAPI MSIHANDLE WINAPI MsiGetLastErrorRecord(void);
typedef struct MSIFILEHASHINFO {
    ULONG dwFileHashInfoSize;
    ULONG dwData[4];
} MSIFILEHASHINFO;
MSIAPI UINT WINAPI MsiGetFileHashW(LPCWSTR path, DWORD options, MSIFILEHASHINFO *hash);

MSIAPI UINT WINAPI MsiGetProductPropertyW(MSIHANDLE session, LPCWSTR property, LPWSTR value, LPDWORD size);
MSIAPI UINT WINAPI MsiVerifyDiskSpace(MSIHANDLE session);

#endif
