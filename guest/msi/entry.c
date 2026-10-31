#include "msi_private.h"

static volatile LONG installing;
static volatile DWORD installing_thread;
static ULONG nested_installs;

/* one installation at a time, though a running one may start another on the same thread, as an upgrade does. */
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
        /* the name ends at an equals sign or a blank, and blanks may stand on both sides of the equals sign. */
        PWSTR name = cursor;
        while (*cursor && *cursor != L'=' && *cursor != L' ' && *cursor != L'\t')
            cursor++;
        PWSTR name_end = cursor;
        while (*cursor == L' ' || *cursor == L'\t')
            cursor++;
        BOOL valid = name_end != name && *cursor == L'=' && !(*name >= L'0' && *name <= L'9');
        for (PWSTR c = name; valid && c < name_end; c++)
            valid = (*c >= L'A' && *c <= L'Z') || (*c >= L'a' && *c <= L'z') || (*c >= L'0' && *c <= L'9') ||
                    *c == L'_' || *c == L'.';
        if (!valid) {
            error = ERROR_INVALID_COMMAND_LINE;
            break;
        }
        *name_end = 0;
        cursor++;
        while (*cursor == L' ' || *cursor == L'\t')
            cursor++;
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
        if (error && error != ERROR_NOT_ENOUGH_MEMORY)
            error = ERROR_INSTALL_TRANSFORM_FAILURE;
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

/* applies the PATCH property's patches to the package database, then reopens it. */
static UINT install_patches(MSIHANDLE *handle, PCWSTR command)
{
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(*handle, MIME_MSI_KIND_PACKAGE);
    PWSTR list = package ? MimeMsiDuplicate(MimeMsiGetProperty(package, L"PATCH")) : nullptr;
    PWSTR product = package ? MimeMsiDuplicate(MimeMsiGetProperty(package, L"ProductCode")) : nullptr;
    PWSTR transforms = package ? MimeMsiDuplicate(MimeMsiGetProperty(package, L"TRANSFORMS")) : nullptr;
    BOOL maintenance = package && package->Maintenance;
    UINT error = !package ? ERROR_INVALID_HANDLE : !list || !product || !transforms ? ERROR_NOT_ENOUGH_MEMORY : ERROR_SUCCESS;
    ReleaseSRWLockShared(&MimeMsiLock);
    if (!error && *list && !maintenance) {
        MSIHANDLE database = MsiGetActiveDatabase(*handle), patched = 0;
        error = database ? MimeMsiApplyPatches(database, product, list) : ERROR_NOT_ENOUGH_MEMORY;
        if (!error) {
            WCHAR borrowed[32];
            _snwprintf(borrowed, RTL_NUMBER_OF(borrowed), L"#%u", database);
            error = MsiOpenPackageW(borrowed, &patched);
        }
        if (!error) {
            MsiCloseHandle(*handle);
            *handle = patched;
            error = command_properties(*handle, command);
            if (!error && *transforms)
                error = MsiSetPropertyW(*handle, L"TRANSFORMS", transforms);
        }
        if (database)
            MsiCloseHandle(database);
    }
    MimeMsiFree(list);
    MimeMsiFree(product);
    MimeMsiFree(transforms);
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
        if ((*MimeMsiGetProperty(package, L"PATCH") && !package->Database->PatchCount) ||
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
    if (!error)
        error = MimeMsiRebootResult(package);
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

/* whether the mode of the session recaches the package, which asks for it from the source. */
static BOOL recaches(MSIHANDLE handle)
{
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    DWORD mode = 0;
    BOOL recache = package && !MimeMsiReinstallMode(package, &mode) && (mode & REINSTALLMODE_PACKAGE);
    ReleaseSRWLockShared(&MimeMsiLock);
    return recache;
}

/* the installed product as a session of maintenance, from the package at the source when the mode asks for it. */
static UINT open_installed(PCWSTR product, PCWSTR package, PCWSTR command, MSIHANDLE *out)
{
    MSIHANDLE cached = 0;
    UINT error = MsiOpenProductW(product, &cached);
    if (!error)
        error = maintenance_command(cached, command);
    if (!error && recaches(cached)) {
        WCHAR found[MAX_PATH];
        if (!package) {
            error = MimeMsiFindPackageSource(product, found, RTL_NUMBER_OF(found));
            package = found;
        }
        MSIHANDLE source = 0;
        if (!error)
            error = MimeMsiOpenProductEx(product, package, &source);
        if (!error)
            error = maintenance_command(source, command);
        if (error && source)
            MsiCloseHandle(source);
        else if (!error) {
            MsiCloseHandle(cached);
            cached = source;
        }
    }
    if (error && cached)
        MsiCloseHandle(cached);
    *out = error ? 0 : cached;
    return error;
}

static UINT reopen_installed(MSIHANDLE *handle, PCWSTR command)
{
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(*handle, MIME_MSI_KIND_PACKAGE);
    PWSTR product = package ? MimeMsiDuplicate(MimeMsiGetProperty(package, L"ProductCode")) : nullptr;
    PWSTR original = package ? MimeMsiDuplicate(MimeMsiGetProperty(package, L"OriginalDatabase")) : nullptr;
    ReleaseSRWLockShared(&MimeMsiLock);
    if (!product || !original) {
        MimeMsiFree(product);
        MimeMsiFree(original);
        return package ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_HANDLE;
    }
    UINT error = ERROR_SUCCESS;
    if (MsiQueryProductStateW(product) == INSTALLSTATE_DEFAULT) {
        MSIHANDLE reopened = 0;
        error = open_installed(product, original, command, &reopened);
        if (!error) {
            MsiCloseHandle(*handle);
            *handle = reopened;
        }
    }
    MimeMsiFree(product);
    MimeMsiFree(original);
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
    /* a missing file or one that is not a package counts as a package that could not be opened. */
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        error = ERROR_INSTALL_PACKAGE_OPEN_FAILED;
    if (!error)
        error = command_properties(handle, command);
    if (!error)
        error = reopen_installed(&handle, command);
    if (!error)
        error = install_transforms(&handle, command);
    if (!error)
        error = install_patches(&handle, command);
    if (!error)
        error = execute_package(handle);
    if (handle)
        MsiCloseHandle(handle);
    end_install();
    return error;
}

/* REINSTALL and REINSTALLMODE for the bits of a mode, which are the letters in the order of the bits. */
static void reinstall_command(PCWSTR features, DWORD mode, PWSTR command, SIZE_T capacity)
{
    static constexpr WCHAR letters[] = L"rpoedcamusv";
    WCHAR chosen[16] = {};
    ULONG length = 0;
    for (ULONG bit = 0; bit < 11; bit++)
        if (mode & (1u << bit))
            chosen[length++] = letters[bit];
    _snwprintf(command, capacity, L"REINSTALL=%s REINSTALLMODE=%s", features, chosen);
}

static UINT reinstall(PCWSTR product, PCWSTR features, DWORD mode)
{
    WCHAR packed[33];
    if (!product || !MimeMsiPackGuid(product, packed) || !features || !mode || (mode & ~0x7ff))
        return ERROR_INVALID_PARAMETER;
    WCHAR command[512];
    if (wcslen(features) > 400)
        return ERROR_INVALID_PARAMETER;
    reinstall_command(features, mode, command, RTL_NUMBER_OF(command));
    if (!begin_install())
        return ERROR_INSTALL_ALREADY_RUNNING;
    MSIHANDLE handle = 0;
    UINT error = open_installed(product, nullptr, command, &handle);
    if (!error)
        error = execute_package(handle);
    if (handle)
        MsiCloseHandle(handle);
    end_install();
    return error;
}

UINT WINAPI MsiReinstallProductW(LPCWSTR product, DWORD mode)
{
    return reinstall(product, L"ALL", mode);
}

UINT WINAPI MsiReinstallFeatureW(LPCWSTR product, LPCWSTR feature, DWORD mode)
{
    return feature && *feature && wcslen(feature) <= 38 ? reinstall(product, feature, mode) : ERROR_INVALID_PARAMETER;
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
    UINT error = open_installed(product, nullptr, command, &handle);
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    if (!error && !package)
        error = ERROR_INVALID_HANDLE;
    if (!error) {
        package->ConfigureDefaults = TRUE;
        if (state == INSTALLSTATE_ABSENT) {
            /* removing the product removes all of its features. */
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
