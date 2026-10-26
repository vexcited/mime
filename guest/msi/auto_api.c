/*
 * the installer functions the scripting objects call that the installer proper has not got. each returns
 * ERROR_CALL_NOT_IMPLEMENTED until it is implemented, which a script sees as an exception of the call.
 */
#include "automation.h"

#define UNSUPPORTED ERROR_CALL_NOT_IMPLEMENTED

UINT AutoApiDatabaseCommit(MSIHANDLE database)
{
    return UNSUPPORTED;
}

UINT AutoApiDatabaseImport(MSIHANDLE database, PCWSTR folder, PCWSTR file)
{
    return UNSUPPORTED;
}

UINT AutoApiDatabaseExport(MSIHANDLE database, PCWSTR table, PCWSTR folder, PCWSTR file)
{
    return UNSUPPORTED;
}

UINT AutoApiDatabaseMerge(MSIHANDLE database, MSIHANDLE merge, PCWSTR table)
{
    return UNSUPPORTED;
}

UINT AutoApiDatabaseGenerateTransform(MSIHANDLE database, MSIHANDLE reference, PCWSTR file)
{
    return UNSUPPORTED;
}

UINT AutoApiCreateTransformSummaryInfo(MSIHANDLE database, MSIHANDLE reference, PCWSTR file, int errors, int validation)
{
    return UNSUPPORTED;
}

UINT AutoApiEnableUIPreview(MSIHANDLE database, MSIHANDLE *preview)
{
    return UNSUPPORTED;
}

UINT AutoApiSummaryInfoSetProperty(MSIHANDLE summary, UINT id, UINT type, int number, FILETIME *time, PCWSTR text)
{
    return UNSUPPORTED;
}

UINT AutoApiSummaryInfoPersist(MSIHANDLE summary)
{
    return UNSUPPORTED;
}

UINT AutoApiGetFeatureValidStates(MSIHANDLE session, PCWSTR feature, DWORD *states)
{
    return UNSUPPORTED;
}

UINT AutoApiSetComponentState(MSIHANDLE session, PCWSTR component, INSTALLSTATE state)
{
    return UNSUPPORTED;
}

UINT AutoApiEnumComponentCosts(MSIHANDLE session, PCWSTR component, DWORD index, INSTALLSTATE state, LPWSTR drive,
                               LPDWORD drive_size, int *cost, int *temporary)
{
    return UNSUPPORTED;
}

UINT AutoApiSetFeatureAttributes(MSIHANDLE session, PCWSTR feature, DWORD attributes)
{
    return UNSUPPORTED;
}

UINT AutoApiCollectUserInfo(PCWSTR product)
{
    return UNSUPPORTED;
}

UINT AutoApiApplyMultiplePatches(PCWSTR patches, PCWSTR product, PCWSTR command)
{
    return UNSUPPORTED;
}

UINT AutoApiConfigureFeature(PCWSTR product, PCWSTR feature, INSTALLSTATE state)
{
    return UNSUPPORTED;
}

UINT AutoApiReinstallFeature(PCWSTR product, PCWSTR feature, DWORD mode)
{
    return UNSUPPORTED;
}

INSTALLSTATE AutoApiUseFeature(PCWSTR product, PCWSTR feature, int mode)
{
    return INSTALLSTATE_INVALIDARG;
}

UINT AutoApiGetFeatureUsage(PCWSTR product, PCWSTR feature, DWORD *count, WORD *date)
{
    return UNSUPPORTED;
}

UINT AutoApiProvideComponent(PCWSTR product, PCWSTR feature, PCWSTR component, DWORD mode, LPWSTR path, LPDWORD size)
{
    return UNSUPPORTED;
}

UINT AutoApiProvideQualifiedComponent(PCWSTR category, PCWSTR qualifier, DWORD mode, LPWSTR path, LPDWORD size)
{
    return UNSUPPORTED;
}

UINT AutoApiGetQualifierDescription(PCWSTR category, PCWSTR qualifier, LPWSTR out, LPDWORD size)
{
    return UNSUPPORTED;
}

UINT AutoApiEnumComponentQualifiers(PCWSTR category, DWORD index, LPWSTR qualifier, LPDWORD qualifier_size)
{
    return UNSUPPORTED;
}

UINT AutoApiProvideAssembly(PCWSTR assembly, PCWSTR context, DWORD mode, DWORD info, LPWSTR path, LPDWORD size)
{
    return UNSUPPORTED;
}

UINT AutoApiGetProductInfoFromScript(PCWSTR script, WCHAR product[39], int *language, DWORD *version, LPWSTR name,
                                     LPDWORD name_size, LPWSTR package, LPDWORD package_size)
{
    return UNSUPPORTED;
}

UINT AutoApiAdvertiseProduct(PCWSTR package, PCWSTR script, BOOL user, PCWSTR transforms, LANGID language,
                             DWORD platform, DWORD options)
{
    return UNSUPPORTED;
}

UINT AutoApiAdvertiseScript(PCWSTR script, DWORD flags, BOOL remove)
{
    return UNSUPPORTED;
}

UINT AutoApiGetShortcutTarget(PCWSTR shortcut, WCHAR product[39], WCHAR feature[39], WCHAR component[39])
{
    return UNSUPPORTED;
}

UINT AutoApiGetFileSignature(PCWSTR path, DWORD flags, BOOL hash, BYTE **data, DWORD *size)
{
    return UNSUPPORTED;
}

UINT AutoApiSourceListClearAll(PCWSTR product, PCWSTR user, DWORD reserved)
{
    return UNSUPPORTED;
}

UINT AutoApiSourceListForceResolution(PCWSTR product, PCWSTR user, DWORD reserved)
{
    return UNSUPPORTED;
}

UINT AutoApiGetPatchFileList(PCWSTR product, PCWSTR patches, DWORD *count, MSIHANDLE **records)
{
    return UNSUPPORTED;
}

UINT AutoApiSourceListEnumMediaDisks(PCWSTR product, PCWSTR sid, DWORD context, DWORD options, DWORD index,
                                     DWORD *disk, LPWSTR label, LPDWORD label_size, LPWSTR prompt, LPDWORD prompt_size)
{
    return UNSUPPORTED;
}

UINT AutoApiSourceListAddMediaDisk(PCWSTR product, PCWSTR sid, DWORD context, DWORD options, DWORD disk,
                                   PCWSTR label, PCWSTR prompt)
{
    return UNSUPPORTED;
}

UINT AutoApiSourceListClearSource(PCWSTR product, PCWSTR sid, DWORD context, DWORD options, PCWSTR source)
{
    return UNSUPPORTED;
}

UINT AutoApiSourceListClearMediaDisk(PCWSTR product, PCWSTR sid, DWORD context, DWORD options, DWORD disk)
{
    return UNSUPPORTED;
}

UINT AutoApiSourceListClearAllEx(PCWSTR product, PCWSTR sid, DWORD context, DWORD options)
{
    return UNSUPPORTED;
}

UINT AutoApiSourceListForceResolutionEx(PCWSTR product, PCWSTR sid, DWORD context, DWORD options)
{
    return UNSUPPORTED;
}
