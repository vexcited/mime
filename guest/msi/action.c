#include "msi_private.h"

static BOOL string_column(const MIME_MSI_TABLE *table, ULONG column, PCWSTR name)
{
    return column < table->Count && !wcscmp(table->Columns[column].Name, name) &&
           (table->Columns[column].Type & 0x0c00) == 0x0c00;
}

static UINT custom_action(MIME_MSI_PACKAGE *package, PCWSTR name, PWSTR *message)
{
    MIME_MSI_DATABASE *database = package->Database;
    MIME_MSI_TABLE *table;
    UINT error = MimeMsiLoadTable(database, L"CustomAction", &table);
    if (error)
        return error == ERROR_BAD_QUERY_SYNTAX ? ERROR_FUNCTION_NOT_CALLED : error;
    if (!string_column(table, 0, L"Action") || table->Count < 4 ||
        wcscmp(table->Columns[1].Name, L"Type") || table->Columns[1].Width != 2 ||
        !string_column(table, 2, L"Source") || !string_column(table, 3, L"Target"))
        return ERROR_INSTALL_PACKAGE_INVALID;
    ULONG row;
    for (row = 0; row < table->Rows; row++) {
        PCWSTR action = database->Strings[MimeMsiCell(table, row, 0)];
        if (action && !wcscmp(action, name))
            break;
    }
    if (row == table->Rows)
        return ERROR_FUNCTION_NOT_CALLED;
    ULONG type = MimeMsiCell(table, row, 1) ^ 0x8000;
    PCWSTR source = database->Strings[MimeMsiCell(table, row, 2)];
    PCWSTR target = database->Strings[MimeMsiCell(table, row, 3)];
    if (type != 51 && type != 19)
        return ERROR_CALL_NOT_IMPLEMENTED;
    if (type == 51 && (!source || !*source))
        return ERROR_INSTALL_PACKAGE_INVALID;
    MIME_MSI_RECORD *record = MimeMsiRecordCreate(0);
    if (!record)
        return ERROR_NOT_ENOUGH_MEMORY;
    error = MimeMsiFieldString(&record->Fields[0], target);
    PWSTR formatted = NULL;
    DWORD length;
    if (!error)
        error = MimeMsiFormatRecord(package, record, &formatted, &length);
    record->Object.Destroy(&record->Object);
    if (!error && type == 51)
        error = MimeMsiSetProperty(package, source, formatted);
    if (!error && type == 19) {
        *message = formatted;
        return ERROR_INSTALL_FAILURE;
    }
    MimeMsiFree(formatted);
    return error;
}

static UINT action_start(MSIHANDLE handle, PCWSTR action)
{
    MSIHANDLE record = MsiCreateRecord(3);
    if (!record)
        return ERROR_NOT_ENOUGH_MEMORY;
    UINT error = MsiRecordSetStringW(record, 1, action);
    if (!error)
        error = MsiRecordSetStringW(record, 2, action);
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

UINT WINAPI MsiDoActionW(MSIHANDLE handle, LPCWSTR action)
{
    if (!action || !*action)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    if (package)
        package->Object.References++;
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (!package)
        return ERROR_INVALID_HANDLE;
    UINT error = action_start(handle, action);
    PWSTR message = NULL;
    AcquireSRWLockExclusive(&MimeMsiLock);
    if (!error) {
        error = MimeMsiStandardAction(package, action);
        if (error == UINT32_MAX)
            error = custom_action(package, action, &message);
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
    if (result == MSICONDITION_ERROR)
        return ERROR_INSTALL_FAILURE;
    if (result == MSICONDITION_FALSE)
        return ERROR_SUCCESS;
    return MsiDoActionW(handle, action);
}

UINT WINAPI MsiSequenceW(MSIHANDLE handle, LPCWSTR name, int mode)
{
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
    MIME_MSI_TABLE *table = NULL;
    error = MimeMsiLoadTable(package->Database, name, &table);
    if (!error && (table->Count != 3 || !string_column(table, 0, L"Action") ||
        !string_column(table, 1, L"Condition") || wcscmp(table->Columns[2].Name, L"Sequence") || table->Columns[2].Width != 2))
        error = ERROR_INSTALL_PACKAGE_INVALID;
    ULONG *heads = NULL, *next = NULL;
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
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (!error) {
        for (ULONG sequence = 1; sequence < 32768 && !error; sequence++)
            for (ULONG row = heads[sequence]; row != UINT32_MAX && !error; row = next[row])
                error = run_sequence_row(handle, package, table, row);
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
        UINT rollback = MimeMsiFinishTransaction(package, FALSE);
        if (rollback)
            DbgPrint("msi: sequence rollback failed with error %u\n", rollback);
    }
    package->SequenceRunning = FALSE;
    MimeMsiRelease(&package->Object);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}
