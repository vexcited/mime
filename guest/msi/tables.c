/*
 * the standard actions that read tables the package may leave out, and the reboot a package asks for. the actions that
 * write classes, fonts and .ini files are in their own files and reached from here.
 */
#include "msi_private.h"

#define ERROR_SUCCESS_REBOOT_REQUIRED 3010L

/* patching files is not supported, so only an empty Patch table passes. */
static UINT patch_files(MIME_MSI_PACKAGE *package)
{
    MIME_MSI_TABLE *table;
    UINT error = MimeMsiLoadTable(package->Database, L"Patch", &table);
    if (error == ERROR_BAD_QUERY_SYNTAX)
        return ERROR_SUCCESS;
    if (error)
        return error;
    return table->Rows ? ERROR_CALL_NOT_IMPLEMENTED : ERROR_SUCCESS;
}

UINT MimeMsiTableAction(MIME_MSI_PACKAGE *package, PCWSTR action)
{
    if (!wcscmp(action, L"PatchFiles"))
        return patch_files(package);
    if (!wcscmp(action, L"ScheduleReboot")) {
        package->RebootAtEnd = TRUE;
        return MimeMsiSetProperty(package, L"MsiRebootActionScheduled", L"1");
    }
    UINT error = MimeMsiClassesAction(package, action);
    if (error == UINT32_MAX)
        error = MimeMsiFontsAction(package, action);
    if (error == UINT32_MAX)
        error = MimeMsiIniAction(package, action);
    return error;
}

/*
 * what the REBOOT property and the actions that ask for a restart make of a finished install. nothing is restarted,
 * the caller learns that one is needed, which is also what a suppressed restart is reported as.
 */
UINT MimeMsiRebootResult(MIME_MSI_PACKAGE *package)
{
    WCHAR mode = MimeMsiGetProperty(package, L"REBOOT")[0] & 0xDF;
    if (mode == L'F')
        package->RebootAtEnd = TRUE;
    return package->RebootAtEnd || package->RebootNow ? ERROR_SUCCESS_REBOOT_REQUIRED : ERROR_SUCCESS;
}
