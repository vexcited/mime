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

/* reads NAME=value words, with blanks around the equals sign and quotes doubled inside quoted values. */
UINT MimeMsiParseProperties(PCWSTR command, MIME_MSI_PROPERTY_SINK sink, void *context)
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
        error = sink(context, name, value);
    }
    MimeMsiFree(copy);
    return error;
}

static UINT set_through_handle(void *context, PCWSTR name, PCWSTR value)
{
    return MsiSetPropertyW((MSIHANDLE)(UINT_PTR)context, name, value);
}

static UINT command_properties(MSIHANDLE package, PCWSTR command)
{
    return MimeMsiParseProperties(command, set_through_handle, (void *)(UINT_PTR)package);
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
    PWSTR source = package ? MimeMsiDuplicate(MimeMsiGetProperty(package, L"SourceDir")) : nullptr;
    BOOL secure = package && !wcscmp(MimeMsiGetProperty(package, L"TRANSFORMSSECURE"), L"1");
    BOOL at_source = package && !wcscmp(MimeMsiGetProperty(package, L"TRANSFORMSATSOURCE"), L"1");
    if (!error && !source)
        error = ERROR_NOT_ENOUGH_MEMORY;
    ReleaseSRWLockShared(&MimeMsiLock);
    if (!error && *list) {
        MSIHANDLE database = MsiGetActiveDatabase(*handle), transformed = 0;
        error = database ? MimeMsiApplyTransformList(database, list, source, secure, at_source) : ERROR_NOT_ENOUGH_MEMORY;
        if (error && error != ERROR_NOT_ENOUGH_MEMORY) {
            WCHAR note[300];
            _snwprintf(note, RTL_NUMBER_OF(note), L"msi: the transforms %.200s did not apply, error %u\n", list, error);
            OutputDebugStringW(note);
            error = ERROR_INSTALL_TRANSFORM_FAILURE;
        }
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
    MimeMsiFree(source);
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

/* the directory of a full path, with its closing backslash. */
static PWSTR directory_of(PCWSTR path)
{
    PWSTR copy = MimeMsiDuplicate(path);
    PWSTR separator = copy ? wcsrchr(copy, L'\\') : nullptr;
    if (separator)
        separator[1] = 0;
    return copy;
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
    PCWSTR ui_sequence = L"InstallUISequence", execute_sequence = L"InstallExecuteSequence";
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    if (!error) {
        PCWSTR action = MimeMsiGetProperty(package, L"ACTION");
        BOOL admin = !_wcsicmp(action, L"ADMIN"), advertise = !_wcsicmp(action, L"ADVERTISE");
        if ((*MimeMsiGetProperty(package, L"PATCH") && !package->Database->PatchCount) ||
            (*action && wcscmp(action, L"INSTALL") && !admin && !advertise))
            error = ERROR_CALL_NOT_IMPLEMENTED;
        else if (advertise) {
            /* the product is made known to the machine or the user, with nothing of it installed. */
            ui_sequence = L"AdvtUISequence";
            execute_sequence = L"AdvtExecuteSequence";
            package->Advertise = TRUE;
            error = MimeMsiSetProperty(package, L"ACTION", L"ADVERTISE");
        } else if (admin) {
            /* the image is made on the target, whatever is installed, and a patch of one is made in its own folder. */
            ui_sequence = L"AdminUISequence";
            execute_sequence = L"AdminExecuteSequence";
            package->Admin = TRUE;
            error = MimeMsiSetProperty(package, L"ACTION", L"ADMIN");
            if (!error)
                error = MimeMsiSetProperty(package, L"Installed", L"");
            if (!error && *MimeMsiGetProperty(package, L"PATCH") && !*MimeMsiGetProperty(package, L"TARGETDIR")) {
                PWSTR directory = directory_of(MimeMsiGetProperty(package, L"OriginalDatabase"));
                error = directory ? MimeMsiSetProperty(package, L"TARGETDIR", directory) : ERROR_NOT_ENOUGH_MEMORY;
                MimeMsiFree(directory);
            }
        } else
            error = MimeMsiSetProperty(package, L"ACTION", L"INSTALL");
    }
    BOOL shown_sequence = FALSE;
    if (!error && level > INSTALLUILEVEL_BASIC) {
        MIME_MSI_TABLE *ui, *dialogs;
        UINT status = MimeMsiLoadTable(package->Database, ui_sequence, &ui);
        if (!status && ui->Rows) {
            /* a package with dialogs needs a window to show them, one that only checks things runs its sequence. */
            UINT found = MimeMsiLoadTable(package->Database, L"Dialog", &dialogs);
            if (!found && dialogs->Rows)
                error = ERROR_CALL_NOT_IMPLEMENTED;
            else if (found && found != ERROR_BAD_QUERY_SYNTAX)
                error = found;
            else
                shown_sequence = TRUE;
        } else if (status && status != ERROR_BAD_QUERY_SYNTAX)
            error = status;
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (!error && shown_sequence)
        error = MimeMsiRunInterfaceSequence(handle, ui_sequence);
    if (!error)
        error = MsiSequenceW(handle, execute_sequence, 0);
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

/* the values of the Property table, for what a patch changes among them to be told after. */
typedef struct PROPERTY_ROW {
    PWSTR Name, Value;
} PROPERTY_ROW;

static void free_rows(PROPERTY_ROW *rows, ULONG count)
{
    for (ULONG i = 0; rows && i < count; i++) {
        MimeMsiFree(rows[i].Name);
        MimeMsiFree(rows[i].Value);
    }
    MimeMsiFree(rows);
}

static UINT property_rows(MIME_MSI_PACKAGE *package, PROPERTY_ROW **out, ULONG *count)
{
    MIME_MSI_TABLE *table;
    *out = nullptr;
    *count = 0;
    UINT error = MimeMsiLoadTable(package->Database, L"Property", &table);
    if (error)
        return error;
    PROPERTY_ROW *rows = MimeMsiAllocate(((SIZE_T)table->Rows + 1) * sizeof(*rows));
    if (!rows)
        return ERROR_NOT_ENOUGH_MEMORY;
    for (ULONG row = 0; !error && row < table->Rows; row++) {
        PCWSTR name = MimeMsiText(package->Database, table, row, 0), value = MimeMsiText(package->Database, table, row, 1);
        rows[row].Name = MimeMsiDuplicate(name ? name : L"");
        rows[row].Value = MimeMsiDuplicate(value ? value : L"");
        if (!rows[row].Name || !rows[row].Value)
            error = ERROR_NOT_ENOUGH_MEMORY;
        *count = row + 1;
    }
    if (error)
        free_rows(rows, *count);
    else
        *out = rows;
    return error;
}

/*
 * a patch of an installed product is applied to the database of its session, with the properties it changes taken on, and what
 * it changes is reinstalled. the session caches the patch when it registers.
 */
static UINT patch_session(MSIHANDLE handle)
{
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    PWSTR list = package ? MimeMsiDuplicate(MimeMsiGetProperty(package, L"PATCH")) : nullptr;
    PWSTR product = package ? MimeMsiDuplicate(MimeMsiGetProperty(package, L"ProductCode")) : nullptr;
    PROPERTY_ROW *before = nullptr;
    ULONG before_count = 0;
    UINT error = !package ? ERROR_INVALID_HANDLE : !list || !product ? ERROR_NOT_ENOUGH_MEMORY : ERROR_SUCCESS;
    if (!error && *list)
        error = property_rows(package, &before, &before_count);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (!error && *list) {
        MSIHANDLE database = MsiGetActiveDatabase(handle);
        error = database ? MimeMsiApplyPatches(database, product, list) : ERROR_NOT_ENOUGH_MEMORY;
        if (database)
            MsiCloseHandle(database);
        PROPERTY_ROW *after = nullptr;
        ULONG after_count = 0;
        AcquireSRWLockExclusive(&MimeMsiLock);
        package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
        if (!error && !package)
            error = ERROR_INVALID_HANDLE;
        if (!error)
            error = property_rows(package, &after, &after_count);
        for (ULONG i = 0; !error && i < after_count; i++) {
            BOOL changed = TRUE;
            for (ULONG k = 0; k < before_count; k++)
                if (!wcscmp(before[k].Name, after[i].Name))
                    changed = wcscmp(before[k].Value, after[i].Value) != 0;
            if (changed)
                error = MimeMsiSetProperty(package, after[i].Name, after[i].Value);
        }
        if (!error) {
            package->NewPatches = TRUE;
            if (!*MimeMsiGetProperty(package, L"REINSTALL"))
                error = MimeMsiSetProperty(package, L"REINSTALL", L"ALL");
        }
        ReleaseSRWLockExclusive(&MimeMsiLock);
        free_rows(after, after_count);
    }
    free_rows(before, before_count);
    MimeMsiFree(list);
    MimeMsiFree(product);
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
    if (!error)
        error = patch_session(cached);
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
        if (!error)
            error = patch_session(source);
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

static UINT reopen_installed(MSIHANDLE *handle, PCWSTR command, PCWSTR instance)
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
    /* the instance a code names is the product to maintain, whatever code the package has. */
    if (*instance) {
        MimeMsiFree(product);
        product = MimeMsiDuplicate(instance);
        if (!product)
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!error && MsiQueryProductStateW(product) == INSTALLSTATE_DEFAULT) {
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

/* the properties of a multiple instance installation: a new instance with the transform that makes it, or an old one by its code. */
static UINT instance_properties(MSIHANDLE handle, BOOL *fresh, WCHAR code[39])
{
    WCHAR flag[8], transforms[8];
    DWORD size = RTL_NUMBER_OF(flag), transforms_size = RTL_NUMBER_OF(transforms), code_size = 39;
    *fresh = FALSE;
    code[0] = 0;
    UINT flag_error = MsiGetPropertyW(handle, L"MSINEWINSTANCE", flag, &size);
    UINT code_error = MsiGetPropertyW(handle, L"MSIINSTANCEGUID", code, &code_size);
    UINT list_error = MsiGetPropertyW(handle, L"TRANSFORMS", transforms, &transforms_size);
    if ((flag_error && flag_error != ERROR_MORE_DATA) || (code_error && code_error != ERROR_MORE_DATA) ||
        (list_error && list_error != ERROR_MORE_DATA))
        return flag_error ? flag_error : code_error ? code_error : list_error;
    BOOL named = code_error == ERROR_MORE_DATA || *code;
    if (flag_error == ERROR_MORE_DATA || *flag) {
        /* a new instance needs the transform that makes it, and has no code of an old one. */
        if (named || (!list_error && !*transforms))
            return ERROR_INVALID_COMMAND_LINE;
        *fresh = TRUE;
        return ERROR_SUCCESS;
    }
    if (!named)
        return ERROR_SUCCESS;
    WCHAR packed[33];
    WCHAR info[8];
    DWORD info_size = RTL_NUMBER_OF(info);
    if (code_error || !MimeMsiPackGuid(code, packed))
        return ERROR_INVALID_COMMAND_LINE;
    INSTALLSTATE state = MsiQueryProductStateW(code);
    if (state != INSTALLSTATE_ADVERTISED && state != INSTALLSTATE_DEFAULT)
        return ERROR_INVALID_COMMAND_LINE;
    if (MsiGetProductInfoW(code, L"InstanceType", info, &info_size) || wcscmp(info, L"1"))
        return ERROR_INVALID_COMMAND_LINE;
    return ERROR_SUCCESS;
}

/* the transform that makes an instance is one, comes first and leaves a product code that is not installed. */
static UINT instance_transform(MSIHANDLE handle)
{
    MSIHANDLE database = MsiGetActiveDatabase(handle);
    ULONG count;
    BOOL first;
    WCHAR code[39];
    UINT error = database ? MimeMsiInstanceTransforms(database, &count, &first, code) : ERROR_NOT_ENOUGH_MEMORY;
    if (database)
        MsiCloseHandle(database);
    if (error)
        return error;
    if (count != 1 || !first)
        return ERROR_INVALID_COMMAND_LINE;
    INSTALLSTATE state = MsiQueryProductStateW(code);
    if (state == INSTALLSTATE_ADVERTISED || state == INSTALLSTATE_DEFAULT)
        return ERROR_INVALID_COMMAND_LINE;
    return MsiSetPropertyW(handle, L"MSIINSTANCEGUID", code);
}

/* whether the properties ask for an administrative installation or an advertisement, which are never of an installed product. */
static BOOL administrative(MSIHANDLE handle)
{
    WCHAR action[16];
    DWORD size = RTL_NUMBER_OF(action);
    return !MsiGetPropertyW(handle, L"ACTION", action, &size) && (!_wcsicmp(action, L"ADMIN") || !_wcsicmp(action, L"ADVERTISE"));
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
    BOOL fresh = FALSE;
    WCHAR instance[39];
    if (!error)
        error = instance_properties(handle, &fresh, instance);
    if (!error && !administrative(handle) && !fresh)
        error = reopen_installed(&handle, command, instance);
    if (!error)
        error = install_transforms(&handle, command);
    if (!error && fresh)
        error = instance_transform(handle);
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

/* one feature added or removed in a session of maintenance, the others left as they are. */
UINT WINAPI MsiConfigureFeatureW(LPCWSTR product, LPCWSTR feature, INSTALLSTATE state)
{
    WCHAR packed[33];
    if (!product || !MimeMsiPackGuid(product, packed) || !feature || !*feature || wcslen(feature) > 38 ||
        state < INSTALLSTATE_ADVERTISED || state > INSTALLSTATE_DEFAULT)
        return ERROR_INVALID_PARAMETER;
    if (state != INSTALLSTATE_LOCAL && state != INSTALLSTATE_DEFAULT && state != INSTALLSTATE_ABSENT)
        return ERROR_CALL_NOT_IMPLEMENTED;
    WCHAR command[512];
    _snwprintf(command, RTL_NUMBER_OF(command), state == INSTALLSTATE_ABSENT ? L"REMOVE=%s" : L"ADDLOCAL=%s", feature);
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
