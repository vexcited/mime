#include "msi_private.h"
#include "automation.h"

/*
 * the ANSI installer functions, which convert their text with the system code page and call the wide ones, and the
 * session functions custom actions call: run mode, component state, language, record streams.
 */

static PWSTR to_wide(PCSTR text)
{
    if (!text)
        return nullptr;
    int count = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    PWSTR wide = count ? MimeMsiAllocate((SIZE_T)count * sizeof(WCHAR)) : nullptr;
    if (wide)
        MultiByteToWideChar(CP_ACP, 0, text, -1, wide, count);
    return wide;
}

/* the size is in bytes without the terminator, and a short buffer gets a truncated copy. */
static UINT copy_ansi(PCWSTR text, LPSTR out, LPDWORD size)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    int length = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length)
        length--;
    DWORD capacity = *size;
    *size = (DWORD)length;
    if (!out)
        return capacity ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    if (!capacity)
        return ERROR_MORE_DATA;
    char *copy = MimeMsiAllocate((SIZE_T)length + 1);
    if (!copy)
        return ERROR_NOT_ENOUGH_MEMORY;
    WideCharToMultiByte(CP_ACP, 0, text, -1, copy, length + 1, nullptr, nullptr);
    DWORD copied = (DWORD)length < capacity ? (DWORD)length : capacity - 1;
    memcpy(out, copy, copied);
    out[copied] = 0;
    MimeMsiFree(copy);
    return (DWORD)length >= capacity ? ERROR_MORE_DATA : ERROR_SUCCESS;
}

#define WITH_WIDE(a, name_a) PWSTR name_a = to_wide(a)

UINT WINAPI MsiGetPropertyA(MSIHANDLE handle, LPCSTR name, LPSTR out, LPDWORD size)
{
    if (!name || !size)
        return ERROR_INVALID_PARAMETER;
    PWSTR wide = to_wide(name);
    if (!wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD needed = 0;
    WCHAR probe[1];
    UINT error = MsiGetPropertyW(handle, wide, probe, &needed);
    if (error && error != ERROR_MORE_DATA) {
        MimeMsiFree(wide);
        return error;
    }
    PWSTR text = MimeMsiAllocate(((SIZE_T)needed + 1) * sizeof(WCHAR));
    DWORD capacity = needed + 1;
    error = text ? MsiGetPropertyW(handle, wide, text, &capacity) : ERROR_NOT_ENOUGH_MEMORY;
    if (!error)
        error = copy_ansi(text, out, size);
    MimeMsiFree(text);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiSetPropertyA(MSIHANDLE handle, LPCSTR name, LPCSTR value)
{
    PWSTR wide_name = to_wide(name), wide_value = to_wide(value);
    UINT error = (name && !wide_name) || (value && !wide_value) ? ERROR_NOT_ENOUGH_MEMORY :
                 MsiSetPropertyW(handle, wide_name, wide_value);
    MimeMsiFree(wide_name);
    MimeMsiFree(wide_value);
    return error;
}

UINT WINAPI MsiRecordSetStringA(MSIHANDLE record, UINT field, LPCSTR value)
{
    PWSTR wide = to_wide(value);
    UINT error = value && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiRecordSetStringW(record, field, wide);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiRecordGetStringA(MSIHANDLE record, UINT field, LPSTR out, LPDWORD size)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    DWORD needed = 0;
    WCHAR probe[1];
    UINT error = MsiRecordGetStringW(record, field, probe, &needed);
    if (error && error != ERROR_MORE_DATA)
        return error;
    PWSTR text = MimeMsiAllocate(((SIZE_T)needed + 1) * sizeof(WCHAR));
    DWORD capacity = needed + 1;
    error = text ? MsiRecordGetStringW(record, field, text, &capacity) : ERROR_NOT_ENOUGH_MEMORY;
    if (!error)
        error = copy_ansi(text, out, size);
    MimeMsiFree(text);
    return error;
}

UINT WINAPI MsiOpenDatabaseA(LPCSTR path, LPCSTR persist, MSIHANDLE *out)
{
    /* persist passes through as is: a mode number, or a file name for output modes, which are not supported. */
    PWSTR wide = to_wide(path);
    UINT error = path && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiOpenDatabaseW(wide, (LPCWSTR)persist, out);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiDatabaseOpenViewA(MSIHANDLE database, LPCSTR query, MSIHANDLE *view)
{
    PWSTR wide = to_wide(query);
    UINT error = query && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiDatabaseOpenViewW(database, wide, view);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiOpenPackageExA(LPCSTR path, DWORD options, MSIHANDLE *out)
{
    PWSTR wide = to_wide(path);
    UINT error = path && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiOpenPackageExW(wide, options, out);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiOpenPackageA(LPCSTR path, MSIHANDLE *out)
{
    return MsiOpenPackageExA(path, 0, out);
}

UINT WINAPI MsiInstallProductA(LPCSTR path, LPCSTR command)
{
    PWSTR wide_path = to_wide(path), wide_command = to_wide(command);
    UINT error = (path && !wide_path) || (command && !wide_command) ? ERROR_NOT_ENOUGH_MEMORY :
                 MsiInstallProductW(wide_path, wide_command);
    MimeMsiFree(wide_path);
    MimeMsiFree(wide_command);
    return error;
}

MSICONDITION WINAPI MsiEvaluateConditionA(MSIHANDLE handle, LPCSTR condition)
{
    PWSTR wide = to_wide(condition);
    MSICONDITION result = condition && !wide ? MSICONDITION_ERROR : MsiEvaluateConditionW(handle, wide);
    MimeMsiFree(wide);
    return result;
}

UINT WINAPI MsiDoActionA(MSIHANDLE handle, LPCSTR action)
{
    PWSTR wide = to_wide(action);
    UINT error = action && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiDoActionW(handle, wide);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiSequenceA(MSIHANDLE handle, LPCSTR table, int mode)
{
    PWSTR wide = to_wide(table);
    UINT error = table && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiSequenceW(handle, wide, mode);
    MimeMsiFree(wide);
    return error;
}

static UINT path_a(MSIHANDLE handle, LPCSTR folder, LPSTR out, LPDWORD size, BOOL source)
{
    PWSTR wide = to_wide(folder);
    if (!wide)
        return folder ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER;
    DWORD needed = 0;
    WCHAR probe[1];
    UINT error = source ? MsiGetSourcePathW(handle, wide, probe, &needed) : MsiGetTargetPathW(handle, wide, probe, &needed);
    if (!error || error == ERROR_MORE_DATA) {
        PWSTR text = MimeMsiAllocate(((SIZE_T)needed + 1) * sizeof(WCHAR));
        DWORD capacity = needed + 1;
        error = !text ? ERROR_NOT_ENOUGH_MEMORY : source ? MsiGetSourcePathW(handle, wide, text, &capacity) :
                                                             MsiGetTargetPathW(handle, wide, text, &capacity);
        if (!error)
            error = copy_ansi(text, out, size);
        MimeMsiFree(text);
    }
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiGetTargetPathA(MSIHANDLE handle, LPCSTR folder, LPSTR out, LPDWORD size)
{
    return path_a(handle, folder, out, size, FALSE);
}

UINT WINAPI MsiGetSourcePathA(MSIHANDLE handle, LPCSTR folder, LPSTR out, LPDWORD size)
{
    return path_a(handle, folder, out, size, TRUE);
}

UINT WINAPI MsiSetTargetPathA(MSIHANDLE handle, LPCSTR folder, LPCSTR path)
{
    PWSTR wide_folder = to_wide(folder), wide_path = to_wide(path);
    UINT error = (folder && !wide_folder) || (path && !wide_path) ? ERROR_NOT_ENOUGH_MEMORY :
                 MsiSetTargetPathW(handle, wide_folder, wide_path);
    MimeMsiFree(wide_folder);
    MimeMsiFree(wide_path);
    return error;
}

UINT WINAPI MsiFormatRecordA(MSIHANDLE install, MSIHANDLE record, LPSTR out, LPDWORD size)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    DWORD needed = 0;
    WCHAR probe[1];
    UINT error = MsiFormatRecordW(install, record, probe, &needed);
    if (error && error != ERROR_MORE_DATA)
        return error;
    PWSTR text = MimeMsiAllocate(((SIZE_T)needed + 1) * sizeof(WCHAR));
    DWORD capacity = needed + 1;
    error = text ? MsiFormatRecordW(install, record, text, &capacity) : ERROR_NOT_ENOUGH_MEMORY;
    if (!error)
        error = copy_ansi(text, out, size);
    MimeMsiFree(text);
    return error;
}

UINT WINAPI MsiGetProductInfoA(LPCSTR product, LPCSTR attribute, LPSTR out, LPDWORD size)
{
    PWSTR wide_product = to_wide(product), wide_attribute = to_wide(attribute);
    UINT error = (product && !wide_product) || (attribute && !wide_attribute) ? ERROR_NOT_ENOUGH_MEMORY : ERROR_SUCCESS;
    if (!error && !size)
        error = out ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    if (!error && size) {
        DWORD needed = 0;
        WCHAR probe[1];
        error = MsiGetProductInfoW(wide_product, wide_attribute, probe, &needed);
        if (!error || error == ERROR_MORE_DATA) {
            PWSTR text = MimeMsiAllocate(((SIZE_T)needed + 1) * sizeof(WCHAR));
            DWORD capacity = needed + 1;
            error = text ? MsiGetProductInfoW(wide_product, wide_attribute, text, &capacity) : ERROR_NOT_ENOUGH_MEMORY;
            if (!error)
                error = copy_ansi(text, out, size);
            MimeMsiFree(text);
        }
    }
    MimeMsiFree(wide_product);
    MimeMsiFree(wide_attribute);
    return error;
}

INSTALLSTATE WINAPI MsiQueryProductStateA(LPCSTR product)
{
    PWSTR wide = to_wide(product);
    INSTALLSTATE state = product && !wide ? INSTALLSTATE_INVALIDARG : MsiQueryProductStateW(wide);
    MimeMsiFree(wide);
    return state;
}

UINT WINAPI MsiEnumRelatedProductsA(LPCSTR upgrade, DWORD reserved, DWORD index, LPSTR product)
{
    PWSTR wide = to_wide(upgrade);
    WCHAR code[39];
    UINT error = upgrade && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiEnumRelatedProductsW(wide, reserved, index, code);
    if (!error && product)
        WideCharToMultiByte(CP_ACP, 0, code, -1, product, 39, nullptr, nullptr);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiEnumProductsA(DWORD index, LPSTR product)
{
    WCHAR code[39];
    UINT error = product ? MsiEnumProductsW(index, code) : ERROR_INVALID_PARAMETER;
    if (!error)
        WideCharToMultiByte(CP_ACP, 0, code, -1, product, 39, nullptr, nullptr);
    return error;
}

UINT WINAPI MsiRemovePatchesA(LPCSTR patches, LPCSTR product, INSTALLTYPE type, LPCSTR command)
{
    PWSTR wide_patches = to_wide(patches), wide_product = to_wide(product), wide_command = to_wide(command);
    UINT error = (patches && !wide_patches) || (product && !wide_product) || (command && !wide_command) ?
                 ERROR_NOT_ENOUGH_MEMORY : MsiRemovePatchesW(wide_patches, wide_product, type, wide_command);
    MimeMsiFree(wide_patches);
    MimeMsiFree(wide_product);
    MimeMsiFree(wide_command);
    return error;
}

INSTALLSTATE WINAPI MsiLocateComponentA(LPCSTR component, LPSTR out, LPDWORD size)
{
    PWSTR wide = to_wide(component);
    WCHAR path[MAX_PATH];
    DWORD length = MAX_PATH;
    INSTALLSTATE state = component && !wide ? INSTALLSTATE_INVALIDARG : MsiLocateComponentW(wide, out ? path : nullptr, &length);
    if (out && size && (state == INSTALLSTATE_LOCAL || state == INSTALLSTATE_SOURCE || state == INSTALLSTATE_ABSENT)) {
        int needed = WideCharToMultiByte(CP_ACP, 0, path, -1, nullptr, 0, nullptr, nullptr);
        if (needed > 0 && (DWORD)needed <= *size) {
            WideCharToMultiByte(CP_ACP, 0, path, -1, out, needed, nullptr, nullptr);
            *size = (DWORD)needed - 1;
        } else {
            *size = needed > 0 ? (DWORD)needed - 1 : 0;
            state = INSTALLSTATE_MOREDATA;
        }
    } else if (!out && size && state != INSTALLSTATE_INVALIDARG) {
        *size = length;
    }
    MimeMsiFree(wide);
    return state;
}

UINT WINAPI MsiSourceListAddSourceExA(LPCSTR product, LPCSTR sid, MSIINSTALLCONTEXT context, DWORD options,
                                      LPCSTR source, DWORD index)
{
    PWSTR wide_product = to_wide(product), wide_sid = to_wide(sid), wide_source = to_wide(source);
    UINT error = (product && !wide_product) || (sid && !wide_sid) || (source && !wide_source) ?
                 ERROR_NOT_ENOUGH_MEMORY :
                 MsiSourceListAddSourceExW(wide_product, wide_sid, context, options, wide_source, index);
    MimeMsiFree(wide_product);
    MimeMsiFree(wide_sid);
    MimeMsiFree(wide_source);
    return error;
}

UINT WINAPI MsiConfigureProductExA(LPCSTR product, int level, INSTALLSTATE state, LPCSTR command)
{
    PWSTR wide_product = to_wide(product), wide_command = to_wide(command);
    UINT error = (product && !wide_product) || (command && !wide_command) ? ERROR_NOT_ENOUGH_MEMORY :
                 MsiConfigureProductExW(wide_product, level, state, wide_command);
    MimeMsiFree(wide_product);
    MimeMsiFree(wide_command);
    return error;
}

UINT WINAPI MsiConfigureProductW(LPCWSTR product, int level, INSTALLSTATE state)
{
    return MsiConfigureProductExW(product, level, state, nullptr);
}

UINT WINAPI MsiConfigureProductA(LPCSTR product, int level, INSTALLSTATE state)
{
    return MsiConfigureProductExA(product, level, state, nullptr);
}

UINT WINAPI MsiReinstallProductA(LPCSTR product, DWORD mode)
{
    PWSTR wide = to_wide(product);
    UINT error = product && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiReinstallProductW(wide, mode);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiReinstallFeatureA(LPCSTR product, LPCSTR feature, DWORD mode)
{
    PWSTR wide_product = to_wide(product), wide_feature = to_wide(feature);
    UINT error = (product && !wide_product) || (feature && !wide_feature) ? ERROR_NOT_ENOUGH_MEMORY :
                 MsiReinstallFeatureW(wide_product, wide_feature, mode);
    MimeMsiFree(wide_product);
    MimeMsiFree(wide_feature);
    return error;
}

/* ------------------------------------------------------------------ streams */

UINT WINAPI MsiRecordReadStream(MSIHANDLE handle, UINT field, char *buffer, LPDWORD size)
{
    MIME_REMOTE(PROXY_RecordReadStream, UINT, MIME_ARG(handle), MIME_ARG(field), MIME_ARG(buffer), MIME_ARG(size));
    if (!size)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_RECORD *record = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_RECORD);
    UINT error = !record ? ERROR_INVALID_HANDLE : field > record->Count ? ERROR_INVALID_PARAMETER :
                 record->Fields[field].Kind != FIELD_STREAM ? ERROR_INVALID_DATATYPE : ERROR_SUCCESS;
    if (!error) {
        MIME_MSI_FIELD *stream = &record->Fields[field];
        SIZE_T remaining = stream->Stream->Size - stream->Position;
        if (!buffer) {
            *size = (DWORD)remaining;
        } else {
            SIZE_T count = *size < remaining ? *size : remaining;
            memcpy(buffer, stream->Stream->Data + stream->Position, count);
            stream->Position += count;
            *size = (DWORD)count;
        }
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

UINT WINAPI MsiRecordSetStreamW(MSIHANDLE handle, UINT field, LPCWSTR path)
{
    MIME_REMOTE(PROXY_RecordSetStreamW, UINT, MIME_ARG(handle), MIME_ARG(field), MIME_ARG(path));
    HANDLE file = INVALID_HANDLE_VALUE;
    MIME_MSI_BLOB *blob = nullptr;
    UINT error = ERROR_SUCCESS;
    if (path) {
        file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        LARGE_INTEGER size;
        if (file == INVALID_HANDLE_VALUE)
            return ERROR_OPEN_FAILED;
        if (!GetFileSizeEx(file, &size) || size.QuadPart > INT32_MAX) {
            CloseHandle(file);
            return ERROR_FUNCTION_FAILED;
        }
        blob = MimeMsiBlobCreate(nullptr, (SIZE_T)size.QuadPart);
        DWORD read;
        if (!blob || (size.QuadPart && (!ReadFile(file, blob->Data, (DWORD)size.QuadPart, &read, nullptr) || read != size.QuadPart)))
            error = blob ? ERROR_FUNCTION_FAILED : ERROR_NOT_ENOUGH_MEMORY;
        CloseHandle(file);
    }
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_RECORD *record = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_RECORD);
    if (!error && !record)
        error = ERROR_INVALID_HANDLE;
    else if (!error && field == 0)
        error = ERROR_INVALID_PARAMETER;
    else if (!error && field > record->Count)
        error = ERROR_INVALID_PARAMETER;
    if (!error) {
        if (blob)
            error = MimeMsiFieldStream(&record->Fields[field], blob);
        else
            MimeMsiFieldInteger(&record->Fields[field], MSI_NULL_INTEGER);
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    MimeMsiBlobRelease(blob);
    return error;
}

UINT WINAPI MsiRecordSetStreamA(MSIHANDLE handle, UINT field, LPCSTR path)
{
    PWSTR wide = to_wide(path);
    UINT error = path && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiRecordSetStreamW(handle, field, wide);
    MimeMsiFree(wide);
    return error;
}

/* ------------------------------------------------------------------ session */

#define MSIRUNMODE_ADMIN 0
#define MSIRUNMODE_ADVERTISE 1
#define MSIRUNMODE_MAINTENANCE 2
#define MSIRUNMODE_ROLLBACKENABLED 3
#define MSIRUNMODE_LOGENABLED 4
#define MSIRUNMODE_OPERATIONS 5
#define MSIRUNMODE_REBOOTATEND 6
#define MSIRUNMODE_REBOOTNOW 7
#define MSIRUNMODE_CABINET 8
#define MSIRUNMODE_SOURCESHORTNAMES 9
#define MSIRUNMODE_TARGETSHORTNAMES 10
#define MSIRUNMODE_RESERVED11 11
#define MSIRUNMODE_WINDOWS9X 12
#define MSIRUNMODE_ZAWENABLED 13
#define MSIRUNMODE_RESERVED14 14
#define MSIRUNMODE_RESERVED15 15
#define MSIRUNMODE_SCHEDULED 16
#define MSIRUNMODE_ROLLBACK 17
#define MSIRUNMODE_COMMIT 18

BOOL WINAPI MsiGetMode(MSIHANDLE handle, UINT mode)
{
    MIME_REMOTE(PROXY_GetMode, BOOL, MIME_ARG(handle), MIME_ARG(mode));
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    BOOL result = FALSE;
    if (package) {
        switch (mode) {
        case MSIRUNMODE_MAINTENANCE: result = package->Maintenance; break;
        case MSIRUNMODE_ROLLBACKENABLED: result = !package->NoRollback; break;
        case MSIRUNMODE_LOGENABLED: result = MimeMsiLogEnabled(); break;
        case MSIRUNMODE_OPERATIONS: result = package->TransactionActive; break;
        case MSIRUNMODE_REBOOTATEND: result = package->RebootAtEnd; break;
        case MSIRUNMODE_REBOOTNOW: result = package->RebootNow; break;
        case MSIRUNMODE_CABINET: {
            MIME_MSI_TABLE *media;
            result = !MimeMsiLoadTable(package->Database, L"Media", &media) && media->Rows;
            break;
        }
        case MSIRUNMODE_SCHEDULED: result = package->SessionKind >= 2; break;
        case MSIRUNMODE_ROLLBACK: result = package->SessionKind == 3; break;
        case MSIRUNMODE_COMMIT: result = package->SessionKind == 4; break;
        default: break;
        }
    }
    ReleaseSRWLockShared(&MimeMsiLock);
    return result;
}

UINT WINAPI MsiSetMode(MSIHANDLE handle, UINT mode, BOOL state)
{
    MIME_REMOTE(PROXY_SetMode, UINT, MIME_ARG(handle), MIME_ARG(mode), MIME_ARG(state));
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    UINT error = package ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
    if (!error) {
        if (mode == MSIRUNMODE_REBOOTATEND)
            package->RebootAtEnd = !!state;
        else if (mode == MSIRUNMODE_REBOOTNOW)
            package->RebootNow = !!state;
        else
            error = ERROR_ACCESS_DENIED;
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

LANGID WINAPI MsiGetLanguage(MSIHANDLE handle)
{
    MIME_REMOTE(PROXY_GetLanguage, LANGID, MIME_ARG(handle));
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    LANGID language = 0;
    if (package) {
        PCWSTR text = MimeMsiGetProperty(package, L"ProductLanguage");
        while (*text >= L'0' && *text <= L'9')
            language = (LANGID)(language * 10 + (*text++ - L'0'));
    }
    ReleaseSRWLockShared(&MimeMsiLock);
    return language;
}

UINT WINAPI MsiGetComponentStateW(MSIHANDLE handle, LPCWSTR name, INSTALLSTATE *installed, INSTALLSTATE *action)
{
    MIME_REMOTE(PROXY_GetComponentStateW, UINT, MIME_ARG(handle), MIME_ARG(name), MIME_ARG(installed), MIME_ARG(action));
    if (!name || !installed || !action)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    UINT error = !package ? ERROR_INVALID_HANDLE :
                 package->SessionKind >= 2 || package->CostStage < 3 ? ERROR_FUNCTION_NOT_CALLED :
                 MimeMsiComponentState(package, name, installed, action);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

UINT WINAPI MsiGetComponentStateA(MSIHANDLE handle, LPCSTR name, INSTALLSTATE *installed, INSTALLSTATE *action)
{
    PWSTR wide = to_wide(name);
    UINT error = name && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiGetComponentStateW(handle, wide, installed, action);
    MimeMsiFree(wide);
    return error;
}

/* ------------------------------------------------------------------ descriptors and qualified components */

#define PATH_CHARS 32768

/* the size is in bytes; without a buffer the caller only learns the size. */
static UINT path_to_ansi(UINT error, PCWSTR path, LPSTR out, LPDWORD size)
{
    if (error || !size)
        return error;
    if (!out) {
        int length = WideCharToMultiByte(CP_ACP, 0, path, -1, nullptr, 0, nullptr, nullptr);
        *size = length ? (DWORD)length - 1 : 0;
        return ERROR_SUCCESS;
    }
    return copy_ansi(path, out, size);
}

static void to_ansi(PCWSTR text, LPSTR out, int capacity)
{
    if (out)
        WideCharToMultiByte(CP_ACP, 0, text, -1, out, capacity, nullptr, nullptr);
}

UINT WINAPI MsiDecomposeDescriptorA(LPCSTR descriptor, LPSTR product, LPSTR feature, LPSTR component, LPDWORD used)
{
    PWSTR wide = to_wide(descriptor);
    if (descriptor && !wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    WCHAR wide_product[39], wide_feature[MAX_FEATURE_CHARS + 1], wide_component[39];
    DWORD length;
    UINT error = MsiDecomposeDescriptorW(wide, wide_product, wide_feature, wide_component, &length);
    if (!error) {
        to_ansi(wide_product, product, 39);
        to_ansi(wide_feature, feature, MAX_FEATURE_CHARS + 1);
        to_ansi(wide_component, component, 39);
        if (used)
            *used = (DWORD)WideCharToMultiByte(CP_ACP, 0, wide, (int)length, nullptr, 0, nullptr, nullptr);
    }
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiConfigureFeatureA(LPCSTR product, LPCSTR feature, INSTALLSTATE state)
{
    PWSTR wide_product = to_wide(product), wide_feature = to_wide(feature);
    UINT error = (product && !wide_product) || (feature && !wide_feature)
                     ? ERROR_NOT_ENOUGH_MEMORY
                     : MsiConfigureFeatureW(wide_product, wide_feature, state);
    MimeMsiFree(wide_product);
    MimeMsiFree(wide_feature);
    return error;
}

UINT WINAPI MsiGetShortcutTargetA(LPCSTR shortcut, LPSTR product, LPSTR feature, LPSTR component)
{
    PWSTR wide = to_wide(shortcut);
    if (shortcut && !wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    WCHAR wide_product[39], wide_feature[MAX_FEATURE_CHARS + 1], wide_component[39];
    UINT error = MsiGetShortcutTargetW(wide, product ? wide_product : nullptr, feature ? wide_feature : nullptr,
                                       component ? wide_component : nullptr);
    if (!error) {
        to_ansi(wide_product, product, 39);
        to_ansi(wide_feature, feature, MAX_FEATURE_CHARS + 1);
        to_ansi(wide_component, component, 39);
    }
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiProvideComponentA(LPCSTR product, LPCSTR feature, LPCSTR component, DWORD mode, LPSTR path,
                                 LPDWORD size)
{
    PWSTR wide_product = to_wide(product), wide_feature = to_wide(feature), wide_component = to_wide(component);
    PWSTR buffer = MimeMsiAllocate(PATH_CHARS * sizeof(WCHAR));
    DWORD length = PATH_CHARS;
    UINT error = !buffer || (product && !wide_product) || (feature && !wide_feature) || (component && !wide_component)
                     ? ERROR_NOT_ENOUGH_MEMORY
                     : MsiProvideComponentW(wide_product, wide_feature, wide_component, mode, buffer, &length);
    error = path_to_ansi(error, buffer, path, size);
    MimeMsiFree(buffer);
    MimeMsiFree(wide_product);
    MimeMsiFree(wide_feature);
    MimeMsiFree(wide_component);
    return error;
}

UINT WINAPI MsiProvideComponentFromDescriptorA(LPCSTR descriptor, LPSTR path, LPDWORD size, LPDWORD args)
{
    PWSTR wide = to_wide(descriptor);
    PWSTR buffer = MimeMsiAllocate(PATH_CHARS * sizeof(WCHAR));
    DWORD length = PATH_CHARS, offset = 0;
    UINT error = !buffer || (descriptor && !wide) ? ERROR_NOT_ENOUGH_MEMORY
                                                  : MsiProvideComponentFromDescriptorW(wide, buffer, &length, &offset);
    error = path_to_ansi(error, buffer, path, size);
    if (!error && args)
        *args = (DWORD)WideCharToMultiByte(CP_ACP, 0, wide, (int)offset, nullptr, 0, nullptr, nullptr);
    MimeMsiFree(buffer);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiProvideQualifiedComponentExA(LPCSTR category, LPCSTR qualifier, DWORD mode, LPCSTR product,
                                            DWORD unused1, DWORD unused2, LPSTR path, LPDWORD size)
{
    PWSTR wide_category = to_wide(category), wide_qualifier = to_wide(qualifier), wide_product = to_wide(product);
    PWSTR buffer = MimeMsiAllocate(PATH_CHARS * sizeof(WCHAR));
    DWORD length = PATH_CHARS;
    UINT error = !buffer || (category && !wide_category) || (qualifier && !wide_qualifier) || (product && !wide_product)
                     ? ERROR_NOT_ENOUGH_MEMORY
                     : MsiProvideQualifiedComponentExW(wide_category, wide_qualifier, mode, wide_product, unused1,
                                                       unused2, buffer, &length);
    error = path_to_ansi(error, buffer, path, size);
    MimeMsiFree(buffer);
    MimeMsiFree(wide_category);
    MimeMsiFree(wide_qualifier);
    MimeMsiFree(wide_product);
    return error;
}

UINT WINAPI MsiProvideQualifiedComponentA(LPCSTR category, LPCSTR qualifier, DWORD mode, LPSTR path, LPDWORD size)
{
    return MsiProvideQualifiedComponentExA(category, qualifier, mode, nullptr, 0, 0, path, size);
}

UINT WINAPI MsiEnumComponentQualifiersA(LPCSTR category, DWORD index, LPSTR qualifier, LPDWORD qualifier_size,
                                        LPSTR data, LPDWORD data_size)
{
    if (!qualifier || !qualifier_size || (data && !data_size))
        return ERROR_INVALID_PARAMETER;
    PWSTR wide = to_wide(category);
    PWSTR name = MimeMsiAllocate(PATH_CHARS * sizeof(WCHAR)), text = MimeMsiAllocate(PATH_CHARS * sizeof(WCHAR));
    DWORD name_length = PATH_CHARS, text_length = PATH_CHARS;
    UINT error = !name || !text || (category && !wide)
                     ? ERROR_NOT_ENOUGH_MEMORY
                     : MsiEnumComponentQualifiersW(wide, index, name, &name_length, text, &text_length);
    if (!error) {
        error = copy_ansi(name, qualifier, qualifier_size);
        UINT data_error = path_to_ansi(ERROR_SUCCESS, text, data, data_size);
        if (!error)
            error = data_error;
    }
    MimeMsiFree(name);
    MimeMsiFree(text);
    MimeMsiFree(wide);
    return error;
}

INSTALLSTATE WINAPI MsiGetComponentPathA(LPCSTR product, LPCSTR component, LPSTR out, LPDWORD size)
{
    if (out && !size)
        return INSTALLSTATE_INVALIDARG;
    PWSTR wide_product = to_wide(product), wide_component = to_wide(component);
    WCHAR buffer[1024];
    DWORD length = RTL_NUMBER_OF(buffer);
    INSTALLSTATE state = (product && !wide_product) || (component && !wide_component)
                             ? INSTALLSTATE_INVALIDARG
                             : MsiGetComponentPathW(wide_product, wide_component, buffer, &length);
    if ((state == INSTALLSTATE_LOCAL || state == INSTALLSTATE_SOURCE || state == INSTALLSTATE_ABSENT) && size &&
        path_to_ansi(ERROR_SUCCESS, buffer, out, size) == ERROR_MORE_DATA)
        state = INSTALLSTATE_MOREDATA;
    MimeMsiFree(wide_product);
    MimeMsiFree(wide_component);
    return state;
}

UINT WINAPI MsiGetProductCodeA(LPCSTR component, LPSTR product)
{
    if (!product || !component)
        return ERROR_INVALID_PARAMETER;
    PWSTR wide = to_wide(component);
    WCHAR code[39];
    UINT error = wide ? MsiGetProductCodeW(wide, code) : ERROR_NOT_ENOUGH_MEMORY;
    if (!error)
        WideCharToMultiByte(CP_ACP, 0, code, -1, product, 39, nullptr, nullptr);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiGetSummaryInformationA(MSIHANDLE database, LPCSTR path, UINT updates, MSIHANDLE *summary)
{
    PWSTR wide = to_wide(path);
    UINT error = path && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiGetSummaryInformationW(database, wide, updates, summary);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiSummaryInfoGetPropertyA(MSIHANDLE summary, UINT id, PUINT type, int *number, FILETIME *time, LPSTR text,
                                       LPDWORD size)
{
    if (text && !size)
        return ERROR_INVALID_PARAMETER;
    DWORD needed = 0;
    WCHAR probe[1];
    UINT error = MsiSummaryInfoGetPropertyW(summary, id, type, number, time, text ? probe : nullptr, text ? &needed : nullptr);
    if (error != ERROR_SUCCESS && error != ERROR_MORE_DATA)
        return error;
    if (!text)
        return ERROR_SUCCESS;
    PWSTR wide = MimeMsiAllocate(((SIZE_T)needed + 1) * sizeof(WCHAR));
    DWORD capacity = needed + 1;
    error = wide ? MsiSummaryInfoGetPropertyW(summary, id, type, number, time, wide, &capacity) : ERROR_NOT_ENOUGH_MEMORY;
    if (!error)
        error = copy_ansi(wide, text, size);
    MimeMsiFree(wide);
    return error;
}

/* the summary of a package of this installer is read: it is not opened for changes, and a call that changes it fails. */
UINT WINAPI MsiSummaryInfoSetPropertyW(MSIHANDLE summary, UINT id, UINT type, int number, FILETIME *time, LPCWSTR text)
{
    MIME_REMOTE(PROXY_SummaryInfoSetPropertyW, UINT, MIME_ARG(summary), MIME_ARG(id), MIME_ARG(type), MIME_ARG(number), MIME_ARG(time), MIME_ARG(text));
    AcquireSRWLockShared(&MimeMsiLock);
    BOOL valid = MimeMsiGetHandle(summary, MIME_MSI_KIND_SUMMARY) != nullptr;
    ReleaseSRWLockShared(&MimeMsiLock);
    if (!valid)
        return ERROR_INVALID_HANDLE;
    return !id || id == 17 || id >= 20 ? ERROR_UNKNOWN_PROPERTY : ERROR_FUNCTION_FAILED;
}

UINT WINAPI MsiSummaryInfoSetPropertyA(MSIHANDLE summary, UINT id, UINT type, int number, FILETIME *time, LPCSTR text)
{
    PWSTR wide = to_wide(text);
    UINT error = text && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiSummaryInfoSetPropertyW(summary, id, type, number, time, wide);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiSummaryInfoPersist(MSIHANDLE summary)
{
    MIME_REMOTE(PROXY_SummaryInfoPersist, UINT, MIME_ARG(summary));
    AcquireSRWLockShared(&MimeMsiLock);
    BOOL valid = MimeMsiGetHandle(summary, MIME_MSI_KIND_SUMMARY) != nullptr;
    ReleaseSRWLockShared(&MimeMsiLock);
    return valid ? ERROR_FUNCTION_FAILED : ERROR_INVALID_HANDLE;
}

/* a database of this installer is read only, so it never has anything to write. */
UINT WINAPI MsiDatabaseCommit(MSIHANDLE database)
{
    MIME_REMOTE(PROXY_DatabaseCommit, UINT, MIME_ARG(database));
    AcquireSRWLockShared(&MimeMsiLock);
    BOOL valid = MimeMsiGetHandle(database, MIME_MSI_KIND_DATABASE) != nullptr;
    ReleaseSRWLockShared(&MimeMsiLock);
    return valid ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}

static BOOL databases_valid(MSIHANDLE database, MSIHANDLE reference)
{
    AcquireSRWLockShared(&MimeMsiLock);
    BOOL valid = MimeMsiGetHandle(database, MIME_MSI_KIND_DATABASE) && (!reference || MimeMsiGetHandle(reference, MIME_MSI_KIND_DATABASE));
    ReleaseSRWLockShared(&MimeMsiLock);
    return valid;
}

/* a transform is the difference of two databases, which the installer does not compute. */
UINT WINAPI MsiDatabaseGenerateTransformW(MSIHANDLE database, MSIHANDLE reference, LPCWSTR file, int reserved1,
                                          int reserved2)
{
    if (!databases_valid(database, reference))
        return ERROR_INVALID_HANDLE;
    return AutoApiDatabaseGenerateTransform(database, reference, file);
}

UINT WINAPI MsiDatabaseGenerateTransformA(MSIHANDLE database, MSIHANDLE reference, LPCSTR file, int reserved1,
                                          int reserved2)
{
    PWSTR wide = to_wide(file);
    UINT error = file && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiDatabaseGenerateTransformW(database, reference, wide, 0, 0);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiCreateTransformSummaryInfoW(MSIHANDLE database, MSIHANDLE reference, LPCWSTR file, int errors,
                                           int validation)
{
    if (!databases_valid(database, reference))
        return ERROR_INVALID_HANDLE;
    return AutoApiCreateTransformSummaryInfo(database, reference, file, errors, validation);
}

UINT WINAPI MsiCreateTransformSummaryInfoA(MSIHANDLE database, MSIHANDLE reference, LPCSTR file, int errors,
                                           int validation)
{
    PWSTR wide = to_wide(file);
    UINT error = file && !wide ? ERROR_NOT_ENOUGH_MEMORY
                               : MsiCreateTransformSummaryInfoW(database, reference, wide, errors, validation);
    MimeMsiFree(wide);
    return error;
}

/*
 * what is installed is found, and what is not there is not restored: the files would come from the source of the
 * product, which the installer does not resolve.
 */
UINT WINAPI MsiInstallMissingComponentW(LPCWSTR product, LPCWSTR component, INSTALLSTATE state)
{
    if (!product || !component || wcslen(product) != 38 || wcslen(component) != 38)
        return ERROR_INVALID_PARAMETER;
    if (state != INSTALLSTATE_LOCAL && state != INSTALLSTATE_SOURCE && state != INSTALLSTATE_DEFAULT)
        return ERROR_INVALID_PARAMETER;
    if (MsiQueryProductStateW(product) == INSTALLSTATE_UNKNOWN)
        return ERROR_UNKNOWN_PRODUCT;
    WCHAR path[1024];
    DWORD length = RTL_NUMBER_OF(path);
    switch (MsiGetComponentPathW(product, component, path, &length)) {
    case INSTALLSTATE_LOCAL:
    case INSTALLSTATE_SOURCE:
        return ERROR_SUCCESS;
    case INSTALLSTATE_UNKNOWN:
        return ERROR_UNKNOWN_COMPONENT;
    case INSTALLSTATE_INVALIDARG:
        return ERROR_INVALID_PARAMETER;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

UINT WINAPI MsiInstallMissingComponentA(LPCSTR product, LPCSTR component, INSTALLSTATE state)
{
    PWSTR wide_product = to_wide(product), wide_component = to_wide(component);
    UINT error = (product && !wide_product) || (component && !wide_component)
                     ? ERROR_NOT_ENOUGH_MEMORY
                     : MsiInstallMissingComponentW(wide_product, wide_component, state);
    MimeMsiFree(wide_product);
    MimeMsiFree(wide_component);
    return error;
}

/* ------------------------------------------------------------------ features, components and views */

UINT WINAPI MsiGetFeatureStateA(MSIHANDLE handle, LPCSTR feature, INSTALLSTATE *installed, INSTALLSTATE *action)
{
    PWSTR wide = to_wide(feature);
    UINT error = feature && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiGetFeatureStateW(handle, wide, installed, action);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiSetFeatureStateA(MSIHANDLE handle, LPCSTR feature, INSTALLSTATE state)
{
    PWSTR wide = to_wide(feature);
    UINT error = feature && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiSetFeatureStateW(handle, wide, state);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiGetFeatureCostA(MSIHANDLE handle, LPCSTR feature, MSICOSTTREE tree, INSTALLSTATE state, LPINT cost)
{
    PWSTR wide = to_wide(feature);
    UINT error = feature && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiGetFeatureCostW(handle, wide, tree, state, cost);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiSetComponentStateA(MSIHANDLE handle, LPCSTR component, INSTALLSTATE state)
{
    PWSTR wide = to_wide(component);
    UINT error = component && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiSetComponentStateW(handle, wide, state);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiSetFeatureAttributesA(MSIHANDLE handle, LPCSTR feature, DWORD attributes)
{
    PWSTR wide = to_wide(feature);
    UINT error = feature && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiSetFeatureAttributesW(handle, wide, attributes);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiGetFeatureValidStatesA(MSIHANDLE handle, LPCSTR feature, LPDWORD states)
{
    PWSTR wide = to_wide(feature);
    UINT error = feature && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiGetFeatureValidStatesW(handle, wide, states);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiEnumComponentCostsA(MSIHANDLE handle, LPCSTR component, DWORD index, INSTALLSTATE state, LPSTR drive,
                                   LPDWORD drive_size, LPINT cost, LPINT temporary)
{
    if (!drive || !drive_size)
        return ERROR_INVALID_PARAMETER;
    PWSTR wide = to_wide(component);
    WCHAR text[16];
    DWORD length = RTL_NUMBER_OF(text);
    UINT error = component && !wide ? ERROR_NOT_ENOUGH_MEMORY :
                 MsiEnumComponentCostsW(handle, wide, index, state, text, &length, cost, temporary);
    if (!error)
        error = copy_ansi(text, drive, drive_size);
    MimeMsiFree(wide);
    return error;
}

MSICONDITION WINAPI MsiDatabaseIsTablePersistentA(MSIHANDLE database, LPCSTR table)
{
    PWSTR wide = to_wide(table);
    MSICONDITION result = table && !wide ? MSICONDITION_ERROR : MsiDatabaseIsTablePersistentW(database, wide);
    MimeMsiFree(wide);
    return result;
}

UINT WINAPI MsiDatabaseGetPrimaryKeysA(MSIHANDLE database, LPCSTR table, MSIHANDLE *record)
{
    PWSTR wide = to_wide(table);
    UINT error = table && !wide ? ERROR_NOT_ENOUGH_MEMORY : MsiDatabaseGetPrimaryKeysW(database, wide, record);
    MimeMsiFree(wide);
    return error;
}

UINT WINAPI MsiViewGetErrorA(MSIHANDLE view, LPSTR column, LPDWORD size)
{
    WCHAR text[1] = {};
    DWORD length = size && column ? 1 : 0;
    UINT result = MsiViewGetErrorW(view, size && column ? text : nullptr, size ? &length : nullptr);
    if (size) {
        if (column && *size)
            column[0] = 0;
        *size = length;
    }
    return result;
}
