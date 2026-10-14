#include "msi_private.h"

PWSTR MimeMsiDuplicate(PCWSTR text)
{
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    PWSTR copy = MimeMsiAllocate(bytes);
    if (copy)
        memcpy(copy, text, bytes);
    return copy;
}

UINT MimeMsiCopyString(PCWSTR text, LPWSTR out, LPDWORD size)
{
    if (!out || !size)
        return ERROR_INVALID_PARAMETER;
    DWORD capacity = *size, length = (DWORD)wcslen(text);
    *size = length;
    if (capacity) {
        DWORD copied = length < capacity ? length : capacity - 1;
        memcpy(out, text, copied * sizeof(WCHAR));
        out[copied] = 0;
    }
    return length >= capacity ? ERROR_MORE_DATA : ERROR_SUCCESS;
}

PCWSTR MimeMsiGetProperty(const MIME_MSI_PACKAGE *package, PCWSTR name)
{
    for (MIME_MSI_PROPERTY *property = package->Properties; property; property = property->Next)
        if (!wcscmp(property->Name, name))
            return property->Value;
    return L"";
}

UINT MimeMsiSetProperty(MIME_MSI_PACKAGE *package, PCWSTR name, PCWSTR value)
{
    MIME_MSI_PROPERTY **link = &package->Properties;
    while (*link && wcscmp((*link)->Name, name))
        link = &(*link)->Next;
    MIME_MSI_PROPERTY *property = *link;
    if (!value || !*value) {
        if (property) {
            *link = property->Next;
            MimeMsiFree(property->Name);
            MimeMsiFree(property->Value);
            MimeMsiFree(property);
        }
        return ERROR_SUCCESS;
    }
    PWSTR copy = MimeMsiDuplicate(value);
    if (!copy)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (!property) {
        property = MimeMsiAllocate(sizeof(*property));
        if (property)
            property->Name = MimeMsiDuplicate(name);
        if (!property || !property->Name) {
            MimeMsiFree(property);
            MimeMsiFree(copy);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        *link = property;
    }
    MimeMsiFree(property->Value);
    property->Value = copy;
    return ERROR_SUCCESS;
}

static void destroy_package(MIME_MSI_OBJECT *object)
{
    MIME_MSI_PACKAGE *package = (void *)object;
    UINT rollback = MimeMsiFinishTransaction(package, FALSE);
    if (rollback)
        DbgPrint("msi: package close rollback failed with error %u\n", rollback);
    while (package->Properties) {
        MIME_MSI_PROPERTY *property = package->Properties;
        package->Properties = property->Next;
        MimeMsiFree(property->Name);
        MimeMsiFree(property->Value);
        MimeMsiFree(property);
    }
    if (package->Database)
        MimeMsiRelease(&package->Database->Object);
    MimeMsiFree(package->ActionFormat);
    MimeMsiFree(package->Features);
    MimeMsiFree(package);
}

static UINT load_properties(MIME_MSI_PACKAGE *package)
{
    MIME_MSI_DATABASE *database = package->Database;
    MIME_MSI_TABLE *table;
    UINT error = MimeMsiLoadTable(database, L"Property", &table);
    if (error)
        return error;
    if (table->Count != 2 || wcscmp(table->Columns[0].Name, L"Property") ||
        wcscmp(table->Columns[1].Name, L"Value") || (table->Columns[0].Type & 0x0c00) != 0x0c00 ||
        (table->Columns[1].Type & 0x0c00) != 0x0c00)
        return ERROR_INSTALL_PACKAGE_INVALID;
    for (ULONG row = 0; row < table->Rows; row++) {
        PCWSTR name = database->Strings[MimeMsiCell(table, row, 0)];
        PCWSTR value = database->Strings[MimeMsiCell(table, row, 1)];
        if (!name || !*name)
            return ERROR_INSTALL_PACKAGE_INVALID;
        error = MimeMsiSetProperty(package, name, value);
        if (error)
            return error;
    }
    error = MimeMsiSetProperty(package, L"OriginalDatabase", database->Path);
    PWSTR directory = MimeMsiDuplicate(database->Path);
    if (!directory)
        return ERROR_NOT_ENOUGH_MEMORY;
    PWSTR separator = wcsrchr(directory, L'\\');
    if (separator)
        separator[1] = 0;
    if (!error)
        error = MimeMsiSetProperty(package, L"SourceDir", directory);
    MimeMsiFree(directory);
    if (!error && !(package->Options & MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE) &&
        MsiQueryProductStateW(MimeMsiGetProperty(package, L"ProductCode")) == INSTALLSTATE_DEFAULT)
        error = MimeMsiSetProperty(package, L"Installed", L"1");
    return error;
}

UINT WINAPI MsiOpenPackageExW(LPCWSTR path, DWORD options, MSIHANDLE *out)
{
    if (!path || !*path || !out || (options & ~MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE))
        return ERROR_INVALID_PARAMETER;
    *out = 0;
    MSIHANDLE handle = 0;
    BOOL borrowed = path[0] == L'#';
    UINT error = ERROR_SUCCESS;
    if (borrowed) {
        PCWSTR p = path + 1;
        if (!*p)
            return ERROR_INVALID_HANDLE;
        while (*p) {
            if (*p < L'0' || *p > L'9' || handle > (UINT32_MAX - (*p - L'0')) / 10)
                return ERROR_INVALID_HANDLE;
            handle = handle * 10 + *p++ - L'0';
        }
    } else {
        error = MsiOpenDatabaseW(path, MSIDBOPEN_READONLY, &handle);
        if (error)
            return error;
    }
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_DATABASE *database = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_DATABASE);
    MIME_MSI_PACKAGE *package = NULL;
    if (!database)
        error = ERROR_INVALID_HANDLE;
    if (!error && !(package = MimeMsiAllocate(sizeof(*package))))
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (!error) {
        package->Object.Kind = MIME_MSI_KIND_PACKAGE;
        package->Object.Destroy = destroy_package;
        package->Database = database;
        package->Options = options;
        database->Object.References++;
        error = load_properties(package);
        if (!error && !(*out = MimeMsiAddHandle(&package->Object)))
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (error && package)
        destroy_package(&package->Object);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (!borrowed)
        MsiCloseHandle(handle);
    return error;
}

UINT WINAPI MsiOpenPackageW(LPCWSTR path, MSIHANDLE *out)
{
    return MsiOpenPackageExW(path, 0, out);
}

MSIHANDLE WINAPI MsiGetActiveDatabase(MSIHANDLE handle)
{
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    MSIHANDLE result = package ? MimeMsiAddHandle(&package->Database->Object) : 0;
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return result;
}

UINT WINAPI MsiGetPropertyW(MSIHANDLE handle, LPCWSTR name, LPWSTR out, LPDWORD size)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    UINT error = package ? MimeMsiCopyString(MimeMsiGetProperty(package, name), out, size) : ERROR_INVALID_HANDLE;
    ReleaseSRWLockShared(&MimeMsiLock);
    return error;
}

UINT WINAPI MsiSetPropertyW(MSIHANDLE handle, LPCWSTR name, LPCWSTR value)
{
    if (!name || !*name)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    UINT error = package ? MimeMsiSetProperty(package, name, value) : ERROR_INVALID_HANDLE;
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}
