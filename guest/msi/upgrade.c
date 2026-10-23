#include "msi_private.h"

/*
 * RemoveExistingProducts removes the products FindRelatedProducts found before the new one installs. an upgrade is an
 * installation inside an installation, so the removal runs without the lock, through msiexec's entry point.
 */

typedef struct RELATED {
    PWSTR Products;
    int Attributes;
} RELATED;

static UINT remove_existing(MSIHANDLE handle, MIME_MSI_PACKAGE *package)
{
    RELATED *related = nullptr;
    ULONG count = 0;
    WCHAR current[39] = {};
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_TABLE *table;
    UINT error = MimeMsiLoadTable(package->Database, L"Upgrade", &table);
    if (error == ERROR_BAD_QUERY_SYNTAX)
        error = ERROR_SUCCESS;
    else if (!error) {
        PCWSTR code = MimeMsiGetProperty(package, L"ProductCode");
        if (wcslen(code) < RTL_NUMBER_OF(current))
            wcscpy(current, code);
        ULONG property = MimeMsiColumn(table, L"ActionProperty", TRUE);
        ULONG attributes = MimeMsiColumn(table, L"Attributes", FALSE);
        related = MimeMsiAllocate((table->Rows + 1) * sizeof(*related));
        if (!related || property == UINT32_MAX || attributes == UINT32_MAX)
            error = related ? ERROR_INSTALL_PACKAGE_INVALID : ERROR_NOT_ENOUGH_MEMORY;
        for (ULONG row = 0; !error && row < table->Rows; row++) {
            PCWSTR name = MimeMsiText(package->Database, table, row, property);
            int flags = MimeMsiInteger(table, row, attributes);
            if (!name || !*name || (flags != MSI_NULL_INTEGER && (flags & 2)))
                continue;
            PCWSTR products = MimeMsiGetProperty(package, name);
            if (!*products)
                continue;
            related[count].Products = MimeMsiDuplicate(products);
            related[count].Attributes = flags == MSI_NULL_INTEGER ? 0 : flags;
            if (!related[count].Products)
                error = ERROR_NOT_ENOUGH_MEMORY;
            else
                count++;
        }
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    for (ULONG i = 0; !error && i < count; i++) {
        PWSTR cursor = related[i].Products;
        while (!error && *cursor) {
            PWSTR end = wcschr(cursor, L';');
            if (end)
                *end = 0;
            if (wcslen(cursor) == 38 && MsiQueryProductStateW(cursor) == INSTALLSTATE_DEFAULT) {
                WCHAR command[160];
                _snwprintf(command, RTL_NUMBER_OF(command), L"UPGRADINGPRODUCTCODE=%s REMOVE=ALL", current);
                UINT status = MsiConfigureProductExW(cursor, 0, INSTALLSTATE_ABSENT, command);
                if (status && !(related[i].Attributes & 4))
                    error = status;
            }
            cursor = end ? end + 1 : cursor + wcslen(cursor);
        }
    }
    for (ULONG i = 0; i < count; i++)
        MimeMsiFree(related[i].Products);
    MimeMsiFree(related);
    return error;
}

UINT MimeMsiRunUnlocked(MSIHANDLE handle, MIME_MSI_PACKAGE *package, PCWSTR action)
{
    if (!wcscmp(action, L"RemoveExistingProducts"))
        return remove_existing(handle, package);
    return ERROR_FUNCTION_NOT_CALLED;
}
