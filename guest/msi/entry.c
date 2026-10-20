#include "msi_private.h"

static volatile LONG installing;
static volatile DWORD installing_thread;
static ULONG nested_installs;

/* one installation at a time, but the one that is running may start another from the same thread, as an upgrade does. */
static BOOL begin_install(void)
{
    if (!InterlockedCompareExchange(&installing, 1, 0)) {
        installing_thread = GetCurrentThreadId();
        return TRUE;
    }
    if (installing_thread != GetCurrentThreadId())
        return FALSE;
    nested_installs++;
    return TRUE;
}

static void end_install(void)
{
    if (nested_installs) {
        nested_installs--;
        return;
    }
    installing_thread = 0;
    InterlockedExchange(&installing, 0);
}

static UINT command_properties(MSIHANDLE package, PCWSTR command)
{
    if (!command)
        return ERROR_SUCCESS;
    PWSTR copy = MimeMsiDuplicate(command);
    if (!copy)
        return ERROR_NOT_ENOUGH_MEMORY;
    PWSTR cursor = copy;
    UINT error = ERROR_SUCCESS;
    while (*cursor && !error) {
        while (*cursor == L' ' || *cursor == L'\t')
            cursor++;
        if (!*cursor)
            break;
        PWSTR name = cursor;
        while ((*cursor >= L'A' && *cursor <= L'Z') || (*cursor >= L'a' && *cursor <= L'z') ||
               (*cursor >= L'0' && *cursor <= L'9') || *cursor == L'_' || *cursor == L'.')
            cursor++;
        if (cursor == name || *cursor != L'=' || (*name >= L'0' && *name <= L'9')) {
            error = ERROR_INVALID_COMMAND_LINE;
            break;
        }
        *cursor++ = 0;
        PWSTR value = cursor, output = cursor;
        BOOL quoted = FALSE;
        while (*cursor) {
            if (*cursor == L'"') {
                cursor++;
                if (quoted && *cursor == L'"')
                    *output++ = *cursor++;
                else
                    quoted = !quoted;
            } else if (!quoted && (*cursor == L' ' || *cursor == L'\t'))
                break;
            else
                *output++ = *cursor++;
        }
        if (quoted) {
            error = ERROR_INVALID_COMMAND_LINE;
            break;
        }
        if (*cursor)
            cursor++;
        *output = 0;
        error = MsiSetPropertyW(package, name, value);
    }
    MimeMsiFree(copy);
    return error;
}

static UINT install_transforms(MSIHANDLE *handle, PCWSTR command)
{
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(*handle, MIME_MSI_KIND_PACKAGE);
    if (package && package->Maintenance) {
        ReleaseSRWLockShared(&MimeMsiLock);
        return ERROR_SUCCESS;
    }
    PWSTR list = package ? MimeMsiDuplicate(MimeMsiGetProperty(package, L"TRANSFORMS")) : nullptr;
    UINT error = !package ? ERROR_INVALID_HANDLE : !list ? ERROR_NOT_ENOUGH_MEMORY : ERROR_SUCCESS;
    if (!error && *list && (!wcscmp(MimeMsiGetProperty(package, L"TRANSFORMSSECURE"), L"1") ||
                           !wcscmp(MimeMsiGetProperty(package, L"TRANSFORMSATSOURCE"), L"1")))
        error = ERROR_CALL_NOT_IMPLEMENTED;
    ReleaseSRWLockShared(&MimeMsiLock);
    if (!error && *list) {
        MSIHANDLE database = MsiGetActiveDatabase(*handle), transformed = 0;
        error = database ? MimeMsiApplyTransformList(database, list) : ERROR_NOT_ENOUGH_MEMORY;
        if (!error) {
            WCHAR borrowed[32];
            _snwprintf(borrowed, RTL_NUMBER_OF(borrowed), L"#%u", database);
            error = MsiOpenPackageW(borrowed, &transformed);
        }
        if (!error) {
            MsiCloseHandle(*handle);
            *handle = transformed;
            error = command_properties(*handle, command);
            if (!error)
                error = MsiSetPropertyW(*handle, L"TRANSFORMS", list);
        }
        if (database)
            MsiCloseHandle(database);
    }
    MimeMsiFree(list);
    return error;
}

static UINT execute_package(MSIHANDLE handle)
{
    UINT error = ERROR_SUCCESS;
    UINT level = (UINT)MsiSetInternalUI(INSTALLUILEVEL_NOCHANGE, nullptr) & 0x1f;
    if (level == INSTALLUILEVEL_DEFAULT)
        level = INSTALLUILEVEL_FULL;
    if (!error) {
        WCHAR text[2] = {(WCHAR)(L'0' + level), 0};
        error = MsiSetPropertyW(handle, L"UILevel", text);
    }
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    if (!error) {
        PCWSTR action = MimeMsiGetProperty(package, L"ACTION");
        if (*MimeMsiGetProperty(package, L"PATCH") ||
            (*action && wcscmp(action, L"INSTALL")))
            error = ERROR_CALL_NOT_IMPLEMENTED;
        else
            error = MimeMsiSetProperty(package, L"ACTION", L"INSTALL");
    }
    if (!error && level > INSTALLUILEVEL_BASIC) {
        MIME_MSI_TABLE *ui;
        UINT status = MimeMsiLoadTable(package->Database, L"InstallUISequence", &ui);
        if (!status && ui->Rows)
            error = ERROR_CALL_NOT_IMPLEMENTED;
        else if (status && status != ERROR_BAD_QUERY_SYNTAX)
            error = status;
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (!error)
        error = MsiSequenceW(handle, L"InstallExecuteSequence", 0);
    AcquireSRWLockExclusive(&MimeMsiLock);
    package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    if (!error && (!package || !package->TransactionCommitted))
        error = ERROR_FUNCTION_NOT_CALLED;
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

static UINT maintenance_command(MSIHANDLE handle, PCWSTR command)
{
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    PWSTR transforms = package ? MimeMsiDuplicate(MimeMsiGetProperty(package, L"TRANSFORMS")) : nullptr;
    ReleaseSRWLockShared(&MimeMsiLock);
    if (!transforms)
        return package ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_HANDLE;
    UINT error = command_properties(handle, command);
    AcquireSRWLockExclusive(&MimeMsiLock);
    package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    if (!error && !package)
        error = ERROR_INVALID_HANDLE;
    if (!error && wcscmp(transforms, MimeMsiGetProperty(package, L"TRANSFORMS")))
        error = ERROR_CALL_NOT_IMPLEMENTED;
    if (!error)
        error = MimeMsiValidateMaintenance(package);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    MimeMsiFree(transforms);
    return error;
}

static UINT reopen_installed(MSIHANDLE *handle, PCWSTR command)
{
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(*handle, MIME_MSI_KIND_PACKAGE);
    PWSTR product = package ? MimeMsiDuplicate(MimeMsiGetProperty(package, L"ProductCode")) : nullptr;
    ReleaseSRWLockShared(&MimeMsiLock);
    if (!product)
        return package ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_HANDLE;
    UINT error = ERROR_SUCCESS;
    if (MsiQueryProductStateW(product) == INSTALLSTATE_DEFAULT) {
        MSIHANDLE reopened = 0;
        error = MsiOpenProductW(product, &reopened);
        if (!error)
            error = maintenance_command(reopened, command);
        if (!error) {
            MsiCloseHandle(*handle);
            *handle = reopened;
        } else if (reopened)
            MsiCloseHandle(reopened);
    }
    MimeMsiFree(product);
    return error;
}

UINT WINAPI MsiInstallProductW(LPCWSTR path, LPCWSTR command)
{
    if (!path || !*path)
        return ERROR_INVALID_PARAMETER;
    if (!begin_install())
        return ERROR_INSTALL_ALREADY_RUNNING;
    MSIHANDLE handle = 0;
    UINT error = MsiOpenPackageW(path, &handle);
    /* a package that is not there, or not a package, is one that could not be opened. */
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        error = ERROR_INSTALL_PACKAGE_OPEN_FAILED;
    if (!error)
        error = command_properties(handle, command);
    if (!error)
        error = reopen_installed(&handle, command);
    if (!error)
        error = install_transforms(&handle, command);
    if (!error)
        error = execute_package(handle);
    if (handle)
        MsiCloseHandle(handle);
    end_install();
    return error;
}

UINT WINAPI MsiReinstallProductW(LPCWSTR product, DWORD mode)
{
    WCHAR packed[33];
    if (!MimeMsiPackGuid(product, packed) || (mode & ~0x7ff))
        return ERROR_INVALID_PARAMETER;
    if (mode & (REINSTALLMODE_FILEOLDERVERSION | REINSTALLMODE_FILEEQUALVERSION | REINSTALLMODE_FILEEXACT |
                REINSTALLMODE_FILEVERIFY | REINSTALLMODE_PACKAGE))
        return ERROR_CALL_NOT_IMPLEMENTED;
    if (!begin_install())
        return ERROR_INSTALL_ALREADY_RUNNING;
    MSIHANDLE handle = 0;
    UINT error = MsiOpenProductW(product, &handle);
    WCHAR options[8] = {};
    ULONG length = 0;
    if (mode & REINSTALLMODE_FILEMISSING) options[length++] = L'p';
    if (mode & REINSTALLMODE_FILEREPLACE) options[length++] = L'a';
    if (mode & REINSTALLMODE_USERDATA) options[length++] = L'u';
    if (mode & REINSTALLMODE_MACHINEDATA) options[length++] = L'm';
    if (mode & REINSTALLMODE_SHORTCUT) options[length++] = L's';
    if (!error && !length)
        error = ERROR_CALL_NOT_IMPLEMENTED;
    if (!error)
        error = MsiSetPropertyW(handle, L"REINSTALL", L"ALL");
    if (!error)
        error = MsiSetPropertyW(handle, L"REINSTALLMODE", options);
    if (!error)
        error = execute_package(handle);
    if (handle)
        MsiCloseHandle(handle);
    end_install();
    return error;
}

UINT WINAPI MsiConfigureProductExW(LPCWSTR product, int level, INSTALLSTATE state, LPCWSTR command)
{
    WCHAR packed[33];
    if (!MimeMsiPackGuid(product, packed) || state < INSTALLSTATE_ADVERTISED || state > INSTALLSTATE_DEFAULT ||
        (state == INSTALLSTATE_DEFAULT && (level < 0 || level > 32767)))
        return ERROR_INVALID_PARAMETER;
    if (state != INSTALLSTATE_LOCAL && state != INSTALLSTATE_DEFAULT && state != INSTALLSTATE_ABSENT)
        return ERROR_CALL_NOT_IMPLEMENTED;
    if (!begin_install())
        return ERROR_INSTALL_ALREADY_RUNNING;
    MSIHANDLE handle = 0;
    UINT error = MsiOpenProductW(product, &handle);
    if (!error)
        error = maintenance_command(handle, command);
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    if (!error && !package)
        error = ERROR_INVALID_HANDLE;
    if (!error) {
        package->ConfigureDefaults = TRUE;
        if (state == INSTALLSTATE_ABSENT) {
            /* the product goes with all of its features. */
            if (*MimeMsiGetProperty(package, L"ADDLOCAL") || *MimeMsiGetProperty(package, L"ADDSOURCE") ||
                *MimeMsiGetProperty(package, L"REINSTALL"))
                error = ERROR_INVALID_PARAMETER;
            else if (!*MimeMsiGetProperty(package, L"REMOVE"))
                error = MimeMsiSetProperty(package, L"REMOVE", L"ALL");
        } else if (state == INSTALLSTATE_LOCAL) {
            if (*MimeMsiGetProperty(package, L"ADDLOCAL") || *MimeMsiGetProperty(package, L"REMOVE") ||
                *MimeMsiGetProperty(package, L"ADDSOURCE") || *MimeMsiGetProperty(package, L"REINSTALL"))
                error = ERROR_INVALID_PARAMETER;
            else
                error = MimeMsiSetProperty(package, L"ADDLOCAL", L"ALL");
        } else if (level) {
            WCHAR text[16];
            _snwprintf(text, RTL_NUMBER_OF(text), L"%d", level);
            error = MimeMsiSetProperty(package, L"INSTALLLEVEL", text);
        }
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (!error)
        error = execute_package(handle);
    if (handle)
        MsiCloseHandle(handle);
    end_install();
    return error;
}
