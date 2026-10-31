#include "msi_private.h"

static BOOL string_column(const MIME_MSI_TABLE *table, ULONG column, PCWSTR name)
{
    return column < table->Count && !wcscmp(table->Columns[column].Name, name) &&
           (table->Columns[column].Type & 0x0c00) == 0x0c00;
}

/* an action's description and data template, when the package has them. */
static void action_text(MSIHANDLE handle, PCWSTR action, PWSTR *description, PWSTR *template)
{
    *description = *template = nullptr;
    /* most packages have no ActionText table, and a query of it would only fail. */
    MSIHANDLE database = MsiGetActiveDatabase(handle);
    MSICONDITION present = database ? MsiDatabaseIsTablePersistentW(database, L"ActionText") : MSICONDITION_NONE;
    if (database)
        MsiCloseHandle(database);
    if (present != MSICONDITION_TRUE && present != MSICONDITION_FALSE)
        return;
    MSIHANDLE view;
    WCHAR query[512];
    _snwprintf(query, RTL_NUMBER_OF(query), L"SELECT `Description`, `Template` FROM `ActionText` WHERE `Action` = '%s'", action);
    if (MsiDatabaseOpenViewW(handle, query, &view))
        return;
    MSIHANDLE record;
    if (!MsiViewExecute(view, 0) && !MsiViewFetch(view, &record)) {
        for (UINT field = 1; field <= 2; field++) {
            DWORD size = 0;
            if (MsiRecordGetStringW(record, field, (LPWSTR)L"", &size) != ERROR_MORE_DATA || !size)
                continue;
            PWSTR text = MimeMsiAllocate((size + 1) * sizeof(WCHAR));
            if (text && !MsiRecordGetStringW(record, field, text, &(DWORD){size + 1}))
                *(field == 1 ? description : template) = text;
            else
                MimeMsiFree(text);
        }
        MsiCloseHandle(record);
    }
    MsiCloseHandle(view);
}

static UINT action_start(MSIHANDLE handle, PCWSTR action)
{
    MSIHANDLE record = MsiCreateRecord(3);
    if (!record)
        return ERROR_NOT_ENOUGH_MEMORY;
    PWSTR description, template;
    action_text(handle, action, &description, &template);
    UINT error = MsiRecordSetStringW(record, 1, action);
    if (!error)
        error = MsiRecordSetStringW(record, 2, description ? description : action);
    if (!error && template)
        error = MsiRecordSetStringW(record, 3, template);
    MimeMsiFree(description);
    MimeMsiFree(template);
    if (!error) {
        int response = MsiProcessMessage(handle, INSTALLMESSAGE_ACTIONSTART, record);
        if (response == 2)
            error = ERROR_INSTALL_USEREXIT;
        else if (response < 0)
            error = ERROR_INSTALL_FAILURE;
    }
    MsiCloseHandle(record);
    return error;
}

/* the actions of the installer of Windows. an action of any other name that the package has no custom action for is
   not an error in a sequence, it is skipped. */
static const PCWSTR STANDARD_ACTIONS[] = {
    L"ADVERTISE", L"AllocateRegistrySpace", L"AppSearch", L"BindImage", L"CCPSearch", L"CollectUserInfo", L"CostFinalize",
    L"CostInitialize", L"CreateFolders", L"CreateShortcuts", L"DeleteServices", L"DisableRollback", L"DuplicateFiles",
    L"ExecuteAction", L"FileCost", L"FindRelatedProducts", L"ForceReboot", L"INSTALL", L"InstallAdminPackage",
    L"InstallExecute", L"InstallExecuteAgain", L"InstallFiles", L"InstallFinalize", L"InstallInitialize", L"InstallODBC",
    L"InstallSFPCatalogFile", L"InstallServices", L"InstallValidate", L"IsolateComponents", L"LaunchConditions",
    L"MigrateFeatureStates", L"MoveFiles", L"MsiConfigureServices", L"MsiPublishAssemblies", L"MsiUnpublishAssemblies",
    L"PatchFiles", L"ProcessComponents", L"PublishComponents", L"PublishFeatures", L"PublishProduct", L"RMCCPSearch",
    L"RegisterClassInfo", L"RegisterComPlus", L"RegisterExtensionInfo", L"RegisterFonts", L"RegisterMIMEInfo",
    L"RegisterProduct", L"RegisterProgIdInfo", L"RegisterTypeLibraries", L"RegisterUser", L"RemoveDuplicateFiles",
    L"RemoveEnvironmentStrings", L"RemoveExistingProducts", L"RemoveFiles", L"RemoveFolders", L"RemoveIniValues",
    L"RemoveODBC", L"RemoveRegistryValues", L"RemoveShortcuts", L"ResolveSource", L"SEQUENCE", L"ScheduleReboot",
    L"SelfRegModules", L"SelfUnregModules", L"SetODBCFolders", L"StartServices", L"StopServices", L"UnpublishComponents",
    L"UnpublishFeatures", L"UnregisterClassInfo", L"UnregisterComPlus", L"UnregisterExtensionInfo", L"UnregisterFonts",
    L"UnregisterMIMEInfo", L"UnregisterProgIdInfo", L"UnregisterTypeLibraries", L"ValidateProductID",
    L"WriteEnvironmentStrings", L"WriteIniValues", L"WriteRegistryValues",
};

static BOOL standard_name(PCWSTR action)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(STANDARD_ACTIONS); i++)
        if (!wcscmp(action, STANDARD_ACTIONS[i]))
            return TRUE;
    return FALSE;
}

/* absent is set when the package has no such action and the installer of Windows knows none of that name. */
static UINT do_action(MSIHANDLE handle, LPCWSTR action, BOOL *absent)
{
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    if (package)
        package->Object.References++;
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (!package)
        return ERROR_INVALID_HANDLE;
    UINT error = action_start(handle, action);
    PWSTR message = nullptr;
    MIME_MSI_CA_ROW row = {};
    BOOL custom = FALSE;
    BOOL finalize = !wcscmp(action, L"InstallFinalize");
    AcquireSRWLockExclusive(&MimeMsiLock);
    if (!error) {
        error = MimeMsiStandardAction(package, action);
        if (error == UINT32_MAX)
            error = MimeMsiExtraAction(package, action);
        if (error == UINT32_MAX) {
            error = MimeMsiCustomActionLookup(package, action, &row);
            custom = !error;
            if (error == ERROR_FUNCTION_NOT_CALLED && !standard_name(action))
                *absent = TRUE;
        }
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (error == MIME_MSI_UNLOCKED_PENDING)
        error = MimeMsiRunUnlocked(handle, package, action);
    else if (error == MIME_MSI_SCRIPT_PENDING) {
        /* the script runs without the lock because its actions call back into the installer. */
        UINT script_error = MimeMsiRunScript(handle, package);
        if (script_error) {
            MimeMsiRunRollback(package);
            AcquireSRWLockExclusive(&MimeMsiLock);
            UINT rollback = MimeMsiFinishTransaction(package, FALSE);
            if (rollback)
                DbgPrint("msi: script rollback failed with error %u\n", rollback);
            MimeMsiClearScript(package);
            ReleaseSRWLockExclusive(&MimeMsiLock);
            error = script_error;
        } else if (finalize) {
            AcquireSRWLockExclusive(&MimeMsiLock);
            error = MimeMsiFinishTransaction(package, TRUE);
            ReleaseSRWLockExclusive(&MimeMsiLock);
            if (!error)
                error = MimeMsiRunCommit(package);
            AcquireSRWLockExclusive(&MimeMsiLock);
            MimeMsiClearScript(package);
            ReleaseSRWLockExclusive(&MimeMsiLock);
        } else
            error = ERROR_SUCCESS;
    } else if (custom) {
        error = MimeMsiRunCustomAction(handle, package, &row, &message);
        MimeMsiCustomActionFree(&row);
    }
    AcquireSRWLockExclusive(&MimeMsiLock);
    if (!message && error && package->LaunchMessage) {
        message = package->LaunchMessage;
        package->LaunchMessage = nullptr;
    }
    MimeMsiRelease(&package->Object);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (message) {
        MSIHANDLE record = MsiCreateRecord(0);
        if (record) {
            if (!MsiRecordSetStringW(record, 0, message))
                MsiProcessMessage(handle, INSTALLMESSAGE_ERROR, record);
            MsiCloseHandle(record);
        }
        MimeMsiFree(message);
    }
    return error;
}

UINT WINAPI MsiDoActionW(MSIHANDLE handle, LPCWSTR action)
{
    MIME_REMOTE(PROXY_DoActionW, UINT, MIME_ARG(handle), MIME_ARG(action));
    BOOL absent = FALSE;
    if (!action || !*action)
        return ERROR_INVALID_PARAMETER;
    return do_action(handle, action, &absent);
}

static void action_not_found(MSIHANDLE handle, PCWSTR action)
{
    DbgPrint("msi: action %ws is not found\n", action);
    MSIHANDLE record = MsiCreateRecord(1);
    if (!record)
        return;
    if (!MsiRecordSetStringW(record, 0, L"Action not found: [1]") && !MsiRecordSetStringW(record, 1, action))
        MsiProcessMessage(handle, INSTALLMESSAGE_INFO, record);
    MsiCloseHandle(record);
}

static UINT run_sequence_row(MSIHANDLE handle, MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row)
{
    MIME_MSI_DATABASE *database = package->Database;
    PCWSTR action = database->Strings[MimeMsiCell(table, row, 0)];
    PCWSTR condition = database->Strings[MimeMsiCell(table, row, 1)];
    if (!action || !*action)
        return ERROR_INSTALL_PACKAGE_INVALID;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MSICONDITION result = MimeMsiEvaluateCondition(package, condition ? condition : L"");
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (result == MSICONDITION_ERROR) {
        DbgPrint("msi: condition of %ws does not evaluate: %ws\n", action, condition ? condition : L"");
        return ERROR_INSTALL_FAILURE;
    }
    if (result == MSICONDITION_FALSE)
        return ERROR_SUCCESS;
    BOOL absent = FALSE;
    UINT error = do_action(handle, action, &absent);
    if (absent) {
        action_not_found(handle, action);
        return ERROR_SUCCESS;
    }
    if (error && error != ERROR_NO_MORE_ITEMS)
        DbgPrint("msi: action %ws returned %u\n", action, error);
    return error;
}

UINT WINAPI MsiSequenceW(MSIHANDLE handle, LPCWSTR name, int mode)
{
    MIME_REMOTE(PROXY_SequenceW, UINT, MIME_ARG(handle), MIME_ARG(name), MIME_ARG(mode));
    if (!name || !*name || mode)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    UINT error = !package ? ERROR_INVALID_HANDLE : package->SequenceRunning ? ERROR_INSTALL_ALREADY_RUNNING : ERROR_SUCCESS;
    if (error) {
        ReleaseSRWLockExclusive(&MimeMsiLock);
        return error;
    }
    package->SequenceRunning = TRUE;
    package->Object.References++;
    MIME_MSI_TABLE *table = nullptr;
    error = MimeMsiLoadTable(package->Database, name, &table);
    if (!error && (table->Count != 3 || !string_column(table, 0, L"Action") ||
        !string_column(table, 1, L"Condition") || wcscmp(table->Columns[2].Name, L"Sequence") || table->Columns[2].Width != 2))
        error = ERROR_INSTALL_PACKAGE_INVALID;
    ULONG *heads = nullptr, *next = nullptr;
    if (!error) {
        SIZE_T bytes = (SIZE_T)table->Rows * sizeof(ULONG);
        if (bytes / sizeof(ULONG) != table->Rows)
            error = ERROR_NOT_ENOUGH_MEMORY;
        else {
            heads = MimeMsiAllocate(32768 * sizeof(ULONG));
            next = MimeMsiAllocate(bytes ? bytes : 1);
            if (!heads || !next)
                error = ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    if (!error) {
        memset(heads, 0xff, 32768 * sizeof(ULONG));
        for (ULONG row = table->Rows; row; row--) {
            int sequence = (int)MimeMsiCell(table, row - 1, 2) - 0x8000;
            if (sequence > 0) {
                next[row - 1] = heads[sequence];
                heads[sequence] = row - 1;
            }
        }
    }
#ifndef _WIN64
    BOOL is64 = package->Is64Bit;
#endif
    ReleaseSRWLockExclusive(&MimeMsiLock);
    BOOL ended = FALSE;
#ifndef _WIN64
    /* a 64-bit package installs into the 64-bit folders, which a 32-bit caller sees redirected. */
    PVOID redirection = nullptr;
    BOOL redirected = is64 && Wow64DisableWow64FsRedirection(&redirection);
#endif
    if (!error) {
        for (ULONG sequence = 1; sequence < 32768 && !error && !ended; sequence++)
            for (ULONG row = heads[sequence]; row != UINT32_MAX && !error && !ended; row = next[row]) {
                error = run_sequence_row(handle, package, table, row);
                /* an action may end the sequence early with success. */
                if (error == ERROR_NO_MORE_ITEMS) {
                    error = ERROR_SUCCESS;
                    ended = TRUE;
                }
            }
        int exit_sequence = error == ERROR_INSTALL_USEREXIT ? -2 : error ? -3 : -1;
        for (ULONG row = 0; row < table->Rows; row++) {
            if ((int)MimeMsiCell(table, row, 2) - 0x8000 == exit_sequence) {
                UINT exit_error = run_sequence_row(handle, package, table, row);
                if (!error)
                    error = exit_error;
                break;
            }
        }
    }
    MimeMsiFree(next);
    MimeMsiFree(heads);
    AcquireSRWLockExclusive(&MimeMsiLock);
    if (error && package->TransactionActive) {
        ReleaseSRWLockExclusive(&MimeMsiLock);
        MimeMsiRunRollback(package);
        AcquireSRWLockExclusive(&MimeMsiLock);
        UINT rollback = MimeMsiFinishTransaction(package, FALSE);
        MimeMsiClearScript(package);
        if (rollback)
            DbgPrint("msi: sequence rollback failed with error %u\n", rollback);
    }
#ifndef _WIN64
    if (redirected)
        Wow64RevertWow64FsRedirection(redirection);
#endif
    package->SequenceRunning = FALSE;
    MimeMsiRelease(&package->Object);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}
