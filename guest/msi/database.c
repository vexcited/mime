#include "msi_private.h"

static void destroy_database(MIME_MSI_OBJECT *object)
{
    MIME_MSI_DATABASE *database = (void *)object;
    while (database->Transforms) {
        MIME_MSI_TRANSFORM *transform = database->Transforms;
        database->Transforms = transform->Next;
        MimeMsiRelease(&transform->Database->Object);
        MimeMsiFree(transform);
    }
    for (ULONG i = 0; i < database->StringCount; i++)
        MimeMsiFree(database->Strings[i]);
    MimeMsiFree(database->Strings);
    while (database->Tables) {
        MIME_MSI_TABLE *table = database->Tables;
        database->Tables = table->Next;
        MimeMsiFree(table->Data);
        MimeMsiFree(table);
    }
    MimeCfbClose(&database->File);
    if (database->Parent)
        MimeMsiRelease(&database->Parent->Object);
    else if (database->OwnedData)
        MimeMsiFree(database->Data);
    else if (database->Data)
        UnmapViewOfFile(database->Data);
    for (ULONG i = 0; i < database->PatchCount; i++)
        MimeMsiRelease(&database->Patches[i]->Object);
    MimeMsiFree(database->Patches);
    MimeMsiFree(database->Path);
    MimeMsiFree(database);
}

/* opens a sub storage, such as a transform inside a patch, as a database rooted there. */
UINT MimeMsiOpenSubStorage(MIME_MSI_DATABASE *parent, PCWSTR name, MIME_MSI_DATABASE **out)
{
    *out = nullptr;
    ULONG *list, count;
    UINT error = MimeMsiChildren(parent, &list, &count);
    if (error)
        return error;
    ULONG index = UINT32_MAX;
    for (ULONG i = 0; i < count && index == UINT32_MAX; i++) {
        MIME_CFB_ENTRY entry;
        if (MimeCfbEntry(&parent->File, list[i], &entry) && entry.Type == 1 && !wcscmp((PCWSTR)entry.Name, name))
            index = list[i];
    }
    MimeMsiFree(list);
    if (index == UINT32_MAX)
        return ERROR_FILE_NOT_FOUND;
    MIME_MSI_DATABASE *database = MimeMsiAllocate(sizeof(*database));
    if (!database)
        return ERROR_NOT_ENOUGH_MEMORY;
    database->Data = parent->Data;
    database->Root = index;
    if (!MimeCfbOpen(&database->File, parent->Data, parent->File.Size, MimeMsiAllocate, MimeMsiFree)) {
        MimeMsiFree(database);
        return ERROR_INSTALL_PACKAGE_INVALID;
    }
    database->Path = MimeMsiDuplicate(parent->Path);
    if (!database->Path) {
        MimeCfbClose(&database->File);
        MimeMsiFree(database);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    database->Parent = parent;
    parent->Object.References++;
    database->Object.Kind = MIME_MSI_KIND_DATABASE;
    database->Object.Destroy = destroy_database;
    error = MimeMsiLoadStrings(database);
    if (error) {
        destroy_database(&database->Object);
        return error;
    }
    *out = database;
    return ERROR_SUCCESS;
}

UINT MimeMsiOpenStorageFile(PCWSTR path, BYTE kind, MIME_MSI_DATABASE **out)
{
    if (!path || !out)
        return ERROR_INVALID_PARAMETER;
    *out = nullptr;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || (ULONGLONG)size.QuadPart > SIZE_MAX) {
        CloseHandle(file);
        return ERROR_INSTALL_PACKAGE_INVALID;
    }
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    CloseHandle(file);
    if (!mapping)
        return GetLastError();
    MIME_MSI_DATABASE *database = MimeMsiAllocate(sizeof(*database));
    if (!database) {
        CloseHandle(mapping);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    database->Data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, (SIZE_T)size.QuadPart);
    CloseHandle(mapping);
    if (database->Data) {
        PVOID snapshot = MimeMsiAllocate((SIZE_T)size.QuadPart);
        if (!snapshot) {
            destroy_database(&database->Object);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        memcpy(snapshot, database->Data, (SIZE_T)size.QuadPart);
        UnmapViewOfFile(database->Data);
        database->Data = snapshot;
        database->OwnedData = TRUE;
    }
    if (!database->Data || !MimeCfbOpen(&database->File, database->Data, (SIZE_T)size.QuadPart,
                                       MimeMsiAllocate, MimeMsiFree)) {
        destroy_database(&database->Object);
        return ERROR_INSTALL_PACKAGE_INVALID;
    }
    BYTE actual = database->File.Directory[80];
    BYTE database_clsid[] = {kind ? kind : actual, 0x10, 0x0c, 0, 0, 0, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46};
    if ((actual != 0x82 && actual != 0x84 && actual != 0x86) ||
        memcmp(database->File.Directory + 80, database_clsid, sizeof(database_clsid))) {
        destroy_database(&database->Object);
        return ERROR_INSTALL_PACKAGE_INVALID;
    }
    DWORD characters = GetFullPathNameW(path, 0, nullptr, nullptr);
    database->Path = characters ? MimeMsiAllocate((SIZE_T)characters * sizeof(WCHAR)) : nullptr;
    if (!database->Path || !GetFullPathNameW(path, characters, database->Path, nullptr)) {
        destroy_database(&database->Object);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    database->Object.Kind = MIME_MSI_KIND_DATABASE;
    database->Object.Destroy = destroy_database;
    *out = database;
    return ERROR_SUCCESS;
}

UINT MimeMsiOpenDatabaseFile(PCWSTR path, BOOL transform, MIME_MSI_DATABASE **out)
{
    UINT error = MimeMsiOpenStorageFile(path, transform ? 0x82 : 0x84, out);
    if (error)
        return error;
    error = MimeMsiLoadStrings(*out);
    if (error) {
        destroy_database(&(*out)->Object);
        *out = nullptr;
    }
    return error;
}

UINT WINAPI MsiOpenDatabaseW(LPCWSTR path, LPCWSTR persist, MSIHANDLE *out)
{
    MIME_REMOTE(PROXY_OpenDatabaseW, UINT, MIME_ARG(path), MIME_ARG(persist), MIME_ARG(out));
    if (!path || !out)
        return ERROR_INVALID_PARAMETER;
    *out = 0;
    /* a number below the addresses is a mode of opening, and one that is no mode is refused. */
    if ((ULONG_PTR)persist < 0x10000 && ((ULONG_PTR)persist & ~(ULONG_PTR)MSIDBOPEN_PATCHFILE) > 4)
        return ERROR_INVALID_PARAMETER;
    if (persist != MSIDBOPEN_READONLY)
        return ERROR_CALL_NOT_IMPLEMENTED;
    MIME_MSI_DATABASE *database;
    UINT error = MimeMsiOpenDatabaseFile(path, FALSE, &database);
    if (error)
        return error;
    AcquireSRWLockExclusive(&MimeMsiLock);
    *out = MimeMsiAddHandle(&database->Object);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (!*out) {
        destroy_database(&database->Object);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    return ERROR_SUCCESS;
}
