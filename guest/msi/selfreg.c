/*
 * the SelfReg and TypeLib tables: libraries that register themselves when the installer asks, and type libraries the installer
 * registers for them.
 */
#include "msi_private.h"
#include <mime/win32/oleauto.h>

static PCWSTR cell_text(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row, PCWSTR column)
{
    ULONG index = MimeMsiColumn(table, column, TRUE);
    PCWSTR text = index == UINT32_MAX ? nullptr : MimeMsiText(package->Database, table, row, index);
    return text && *text ? text : nullptr;
}

static UINT optional_table(MIME_MSI_PACKAGE *package, PCWSTR name, MIME_MSI_TABLE **table)
{
    UINT error = MimeMsiLoadTable(package->Database, name, table);
    if (error == ERROR_BAD_QUERY_SYNTAX) {
        *table = nullptr;
        return ERROR_SUCCESS;
    }
    return error;
}

/* whether a row follows its component being installed, or being removed. */
static UINT wanted(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *features, PCWSTR component, PCWSTR feature, BOOL remove,
                   BOOL *out)
{
    INSTALLSTATE installed, action;
    *out = FALSE;
    UINT error = MimeMsiComponentState(package, component, &installed, &action);
    if (error == ERROR_UNKNOWN_COMPONENT)
        return ERROR_SUCCESS;
    if (error)
        return error;
    if (remove) {
        *out = action == INSTALLSTATE_ABSENT;
        return ERROR_SUCCESS;
    }
    if (action != INSTALLSTATE_LOCAL)
        return ERROR_SUCCESS;
    *out = TRUE;
    return feature && features ? MimeMsiFeatureSelected(package, features, feature, 0, out) : ERROR_SUCCESS;
}

/* the component that owns a file of the package. */
static PCWSTR owner_of_file(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *files, PCWSTR key)
{
    for (ULONG row = 0; row < files->Rows; row++) {
        PCWSTR name = cell_text(package, files, row, L"File");
        if (name && !wcscmp(name, key))
            return cell_text(package, files, row, L"Component_");
    }
    return nullptr;
}

/* the answer of a library to its call to register: failing to load or lacking the function is a failure to register. */
static HRESULT call_registration(MIME_MSI_PACKAGE *package, PCWSTR path, BOOL remove)
{
    WORD machine = 0;
    BYTE header[0x400];
    DWORD read = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    BOOL ok = ReadFile(file, header, sizeof(header), &read, nullptr);
    CloseHandle(file);
    UINT error = ok ? MimeMsiMachineOfImage(header, read, &machine) : GetLastError();
    if (error)
        return HRESULT_FROM_WIN32(error);
    PCSTR entry = remove ? "DllUnregisterServer" : "DllRegisterServer";
    if (machine != IMAGE_FILE_MACHINE_NATIVE) {
        UINT result = 0;
        error = MimeMsiRemoteRegister(package, path, entry, machine, &result);
        return error ? HRESULT_FROM_WIN32(error) : (HRESULT)result;
    }
    HMODULE library = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!library)
        return HRESULT_FROM_WIN32(GetLastError());
    HRESULT (WINAPI *function)(void) = (void *)GetProcAddress(library, entry);
    HRESULT result = function ? function() : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    FreeLibrary(library);
    return result;
}

static UINT self_registration(MIME_MSI_PACKAGE *package, BOOL remove)
{
    MIME_MSI_TABLE *table, *files;
    UINT error = optional_table(package, L"SelfReg", &table);
    if (error || !table || !table->Rows)
        return error;
    error = MimeMsiRequireTransaction(package);
    if (!error)
        error = MimeMsiLoadTable(package->Database, L"File", &files);
    /* a library registers itself once it is there to be loaded. */
    if (!error && !remove)
        error = MimeMsiInstallQueuedFiles(package);
    for (ULONG row = 0; !error && row < table->Rows; row++) {
        PCWSTR file = cell_text(package, table, row, L"File_");
        PCWSTR component = file ? owner_of_file(package, files, file) : nullptr;
        BOOL use;
        if (!component)
            return ERROR_INSTALL_PACKAGE_INVALID;
        error = wanted(package, nullptr, component, nullptr, remove, &use);
        if (error || !use)
            continue;
        PWSTR path = nullptr;
        error = MimeMsiFileTargetPath(package, file, &path);
        if (error)
            break;
        HRESULT result = call_registration(package, path, remove);
        if (FAILED(result)) {
            DbgPrint("msi: the library %ws did not %s, %08x\n", path, remove ? "unregister" : "register", result);
            /* a library that cannot be installed fails the install, one that cannot be taken away does not. */
            if (!remove)
                error = ERROR_INSTALL_FAILURE;
        }
        MimeMsiFree(path);
    }
    return error;
}

/* ------------------------------------------------------------------ type libraries */

typedef struct TYPELIB_UNDO {
    GUID Id;
    WORD Major, Minor;
    LCID Language;
} TYPELIB_UNDO;

/* the registration is taken back when the install is, and the note goes either way. */
static UINT undo_typelib(void *context, BOOL rollback)
{
    TYPELIB_UNDO *undo = context;
    if (rollback) {
        UnRegisterTypeLib(&undo->Id, undo->Major, undo->Minor, undo->Language, SYS_WIN32);
        UnRegisterTypeLib(&undo->Id, undo->Major, undo->Minor, undo->Language, SYS_WIN64);
    }
    MimeMsiFree(undo);
    return ERROR_SUCCESS;
}

static UINT register_typelib(MIME_MSI_PACKAGE *package, MIME_MSI_TABLE *table, ULONG row, PCWSTR component, BOOL remove)
{
    PCWSTR id = cell_text(package, table, row, L"LibID");
    ULONG language_column = MimeMsiColumn(table, L"Language", FALSE), version_column = MimeMsiColumn(table, L"Version", FALSE);
    int language = language_column == UINT32_MAX ? 0 : MimeMsiInteger(table, row, language_column);
    int version = version_column == UINT32_MAX ? 0 : MimeMsiInteger(table, row, version_column);
    TYPELIB_UNDO undo = {};
    if (!id || language == MSI_NULL_INTEGER || version == MSI_NULL_INTEGER || FAILED(CLSIDFromString((LPOLESTR)id, &undo.Id)))
        return ERROR_INSTALL_PACKAGE_INVALID;
    undo.Major = (WORD)(version >> 8);
    undo.Minor = (WORD)(version & 0xff);
    undo.Language = (LCID)language;
    if (remove) {
        /* the library is registered for the kind of program that loaded it, which the package does not say. */
        UnRegisterTypeLib(&undo.Id, undo.Major, undo.Minor, undo.Language, SYS_WIN32);
        UnRegisterTypeLib(&undo.Id, undo.Major, undo.Minor, undo.Language, SYS_WIN64);
        return ERROR_SUCCESS;
    }
    MIME_MSI_TABLE *components;
    UINT error = MimeMsiLoadTable(package->Database, L"Component", &components);
    PWSTR path = nullptr, help = nullptr;
    for (ULONG other = 0; !error && other < components->Rows; other++) {
        PCWSTR name = cell_text(package, components, other, L"Component");
        if (!name || wcscmp(name, component))
            continue;
        PCWSTR key = cell_text(package, components, other, L"KeyPath");
        error = key ? MimeMsiFileTargetPath(package, key, &path) : ERROR_INSTALL_PACKAGE_INVALID;
        break;
    }
    PCWSTR directory = cell_text(package, table, row, L"Directory_");
    if (!error && directory) {
        error = MimeMsiDirectoryPath(package, directory, FALSE, &help);
        if (error == ERROR_DIRECTORY)
            error = ERROR_INSTALL_PACKAGE_INVALID;
    }
    if (!error && !path)
        error = ERROR_INSTALL_PACKAGE_INVALID;
    if (!error) {
        ITypeLib *library = nullptr;
        HRESULT result = LoadTypeLibEx(path, REGKIND_NONE, &library);
        if (SUCCEEDED(result)) {
            result = RegisterTypeLib(library, path, help);
            library->lpVtbl->Release(library);
        }
        if (FAILED(result)) {
            DbgPrint("msi: the type library %ws did not register, %08x\n", path, result);
            error = ERROR_INSTALL_FAILURE;
        } else {
            TYPELIB_UNDO *note = MimeMsiAllocate(sizeof(*note));
            if (!note)
                error = ERROR_NOT_ENOUGH_MEMORY;
            else {
                *note = undo;
                error = MimeMsiNoteUndo(package, undo_typelib, note);
                if (error)
                    MimeMsiFree(note);
            }
        }
    }
    MimeMsiFree(path);
    MimeMsiFree(help);
    return error;
}

static UINT type_libraries(MIME_MSI_PACKAGE *package, BOOL remove)
{
    MIME_MSI_TABLE *table, *features;
    UINT error = optional_table(package, L"TypeLib", &table);
    if (error || !table || !table->Rows)
        return error;
    error = MimeMsiRequireTransaction(package);
    if (!error)
        error = MimeMsiLoadTable(package->Database, L"Feature", &features);
    /* a library is read to be registered, once it is in place. */
    if (!error && !remove)
        error = MimeMsiInstallQueuedFiles(package);
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (ULONG row = 0; !error && row < table->Rows; row++) {
        PCWSTR component = cell_text(package, table, row, L"Component_");
        PCWSTR feature = cell_text(package, table, row, L"Feature_");
        BOOL use;
        if (!component)
            return ERROR_INSTALL_PACKAGE_INVALID;
        error = wanted(package, features, component, feature, remove, &use);
        if (!error && use)
            error = register_typelib(package, table, row, component, remove);
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    return error;
}

UINT MimeMsiRegistrationTablesAction(MIME_MSI_PACKAGE *package, PCWSTR action)
{
    if (!wcscmp(action, L"SelfRegModules"))
        return self_registration(package, FALSE);
    if (!wcscmp(action, L"SelfUnregModules"))
        return self_registration(package, TRUE);
    if (!wcscmp(action, L"RegisterTypeLibraries"))
        return type_libraries(package, FALSE);
    if (!wcscmp(action, L"UnregisterTypeLibraries"))
        return type_libraries(package, TRUE);
    return UINT32_MAX;
}
