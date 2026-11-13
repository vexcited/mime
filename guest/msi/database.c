#include "msi_private.h"

static void destroy_database(MIME_MSI_OBJECT *object)
{
    MIME_MSI_DATABASE *database = (void *)object;
    while (database->Transforms) {
        MIME_MSI_TRANSFORM *transform = database->Transforms;
        database->Transforms = transform->Next;
        MimeMsiRelease(&transform->Database->Object);
        MimeMsiFree(transform->Name);
        MimeMsiFree(transform);
    }
    while (database->Streams) {
        MIME_MSI_STREAM *stream = database->Streams;
        database->Streams = stream->Next;
        MimeMsiFree(stream->Data);
        MimeMsiFree(stream);
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
    if (database->WriteLock)
        CloseHandle(database->WriteLock);
    MimeMsiFree(database->OutputPath);
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
        if (MimeCfbEntry(&parent->File, list[i], &entry) && entry.Type == 1 && !_wcsicmp((PCWSTR)entry.Name, name))
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
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, 0, nullptr);
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
    BOOL output = (ULONG_PTR)persist >= 0x10000;
    ULONG mode = output ? 1 : (ULONG)(ULONG_PTR)persist;
    /* DIRECT/CREATE and patch databases need separate semantics; do not pretend they are transactions. */
    if (mode > 1)
        return ERROR_CALL_NOT_IMPLEMENTED;
    PWSTR output_path = nullptr;
    HANDLE write_lock = nullptr;
    UINT error = ERROR_SUCCESS;
    if (mode == 1) {
        PCWSTR target = output ? persist : path;
        DWORD length = GetFullPathNameW(target, 0, nullptr, nullptr);
        output_path = length ? MimeMsiAllocate((SIZE_T)length * sizeof(WCHAR)) : nullptr;
        if (!output_path || !GetFullPathNameW(target, length, output_path, nullptr))
            error = ERROR_INVALID_PARAMETER;
        else {
            DWORD attributes = GetFileAttributesW(output_path);
            if (attributes != INVALID_FILE_ATTRIBUTES) {
                write_lock = CreateFileW(output_path, GENERIC_READ | GENERIC_WRITE,
                                         FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
                if (write_lock == INVALID_HANDLE_VALUE) {
                    error = GetLastError();
                    write_lock = nullptr;
                }
            } else if (!output)
                error = GetLastError();
        }
    }
    /* For an in-place transaction, acquire writer exclusivity before reading the snapshot. Otherwise a
     * competing writer can commit between the read and the lock, and this transaction would overwrite it. */
    MIME_MSI_DATABASE *database = nullptr;
    if (!error)
        error = MimeMsiOpenDatabaseFile(path, FALSE, &database);
    if (error) {
        if (write_lock)
            CloseHandle(write_lock);
        MimeMsiFree(output_path);
        return error;
    }
    database->Writable = mode == 1;
    database->OutputPending = output;
    database->OutputPath = output_path;
    database->WriteLock = write_lock;
    AcquireSRWLockExclusive(&MimeMsiLock);
    *out = MimeMsiAddHandle(&database->Object);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (!*out) {
        destroy_database(&database->Object);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    return ERROR_SUCCESS;
}
