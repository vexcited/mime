/*
 * activation contexts: CreateActCtx and friends, which Windows implements in kernel32 with ntdll and csrss's sxs.dll.
 * mime's ntdll builds the context data itself. also the manifest driven settings: reported version and long paths.
 */
#include "kernelbase_private.h"

#define ACTCTX_VALID_FLAGS 0x000000FF

static BOOL fail(NTSTATUS status)
{
    KbSetStatus(status);
    return FALSE;
}

/* frees the data of a context created here when its last reference goes. */
static void NTAPI notification(ULONG type, PACTIVATION_CONTEXT context, const void *data, PVOID notification_context,
                               PVOID notification_data, PBOOLEAN disable)
{
    if (type == ACTIVATION_CONTEXT_NOTIFICATION_DESTROY)
        RtlFreeActivationContextData((PVOID)data);
    else if (disable)
        *disable = TRUE;
}

static BOOL reaches(ULONG size, SIZE_T end)
{
    return size >= end;
}

#define MEMBER_END(type, member) (offsetof(type, member) + sizeof(((type *)0)->member))

static BOOL file_exists(PCWSTR path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

/* dir\name followed by suffix, in a new heap string. */
static PWSTR join(PCWSTR dir, PCWSTR name, PCWSTR suffix)
{
    SIZE_T a = wcslen(dir), b = wcslen(name), c = suffix ? wcslen(suffix) : 0;
    PWSTR s = KbAlloc((a + b + c + 2) * sizeof(WCHAR));
    if (!s)
        return nullptr;
    memcpy(s, dir, a * sizeof(WCHAR));
    if (a && dir[a - 1] != '\\')
        s[a++] = '\\';
    memcpy(s + a, name, b * sizeof(WCHAR));
    if (c)
        memcpy(s + a + b, suffix, c * sizeof(WCHAR));
    s[a + b + c] = 0;
    return s;
}

HANDLE WINAPI CreateActCtxW(PCACTCTXW actctx)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    PWSTR source = nullptr, directory = nullptr, module_path = nullptr;
    PVOID data = nullptr;
    HANDLE result = INVALID_HANDLE_VALUE;
    if (!actctx || !reaches(actctx->cbSize, MEMBER_END(ACTCTXW, lpSource)))
        goto done;
    DWORD flags = actctx->dwFlags;
    ULONG size = actctx->cbSize;
    if ((flags & ~ACTCTX_VALID_FLAGS) ||
        ((flags & ACTCTX_FLAG_PROCESSOR_ARCHITECTURE_VALID) && !reaches(size, MEMBER_END(ACTCTXW, wProcessorArchitecture))) ||
        ((flags & ACTCTX_FLAG_LANGID_VALID) && !reaches(size, MEMBER_END(ACTCTXW, wLangId))) ||
        ((flags & ACTCTX_FLAG_ASSEMBLY_DIRECTORY_VALID) && !reaches(size, MEMBER_END(ACTCTXW, lpAssemblyDirectory))) ||
        ((flags & ACTCTX_FLAG_RESOURCE_NAME_VALID) && !reaches(size, MEMBER_END(ACTCTXW, lpResourceName))) ||
        ((flags & ACTCTX_FLAG_APPLICATION_NAME_VALID) && !reaches(size, MEMBER_END(ACTCTXW, lpApplicationName))) ||
        ((flags & ACTCTX_FLAG_HMODULE_VALID) && !reaches(size, MEMBER_END(ACTCTXW, hModule))))
        goto done;
    if ((flags & ACTCTX_FLAG_SOURCE_IS_ASSEMBLYREF) && (flags & (ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID)))
        goto done;
    if ((flags & ACTCTX_FLAG_SET_PROCESS_DEFAULT) && KbPeb()->ActivationContextData) {
        status = STATUS_SXS_PROCESS_DEFAULT_ALREADY_SET;
        goto done;
    }
    if (!actctx->lpSource && !(flags & (ACTCTX_FLAG_ASSEMBLY_DIRECTORY_VALID | ACTCTX_FLAG_HMODULE_VALID)))
        goto done;
    USHORT arch = 0xFFFF;
    if (flags & ACTCTX_FLAG_PROCESSOR_ARCHITECTURE_VALID) {
        arch = actctx->wProcessorArchitecture;
#ifdef __i386__
        /* a 32-bit process accepts x86, which is 0, or any. */
        if (arch == PROCESSOR_ARCHITECTURE_INTEL)
            arch = PROCESSOR_ARCHITECTURE_IA32_ON_WIN64;
        if (arch != 0xFFFF && arch != PROCESSOR_ARCHITECTURE_IA32_ON_WIN64)
            goto done;
#else
        if (arch != 0xFFFF && arch != PROCESSOR_ARCHITECTURE_AMD64)
            goto done;
#endif
    }
    PCWSTR from = actctx->lpSource;
    if (!from && (flags & ACTCTX_FLAG_HMODULE_VALID)) {
        WCHAR name[MAX_PATH];
        DWORD n = GetModuleFileNameW(actctx->hModule, name, RTL_NUMBER_OF(name));
        /* windows hands back INVALID_HANDLE_VALUE with GetModuleFileName's error. */
        if (!n || n >= RTL_NUMBER_OF(name))
            return INVALID_HANDLE_VALUE;
        module_path = KbAlloc((n + 1) * sizeof(WCHAR));
        if (!module_path) {
            status = STATUS_NO_MEMORY;
            goto done;
        }
        memcpy(module_path, name, (n + 1) * sizeof(WCHAR));
        from = module_path;
    }
    if (!(flags & ACTCTX_FLAG_SOURCE_IS_ASSEMBLYREF)) {
        WCHAR full[MAX_PATH];
        PWSTR file_part;
        ULONG bytes = RtlGetFullPathName_U(from, sizeof(full), full, &file_part);
        if (!bytes || bytes >= sizeof(full)) {
            status = bytes ? STATUS_NAME_TOO_LONG : STATUS_OBJECT_NAME_INVALID;
            goto done;
        }
        source = join(L"", full, nullptr);
        if (flags & ACTCTX_FLAG_ASSEMBLY_DIRECTORY_VALID)
            directory = actctx->lpAssemblyDirectory ? join(L"", actctx->lpAssemblyDirectory, nullptr) : nullptr;
    } else {
        if (flags & ACTCTX_FLAG_ASSEMBLY_DIRECTORY_VALID) {
            PCWSTR dir = actctx->lpAssemblyDirectory;
            RTL_PATH_TYPE type = dir && *dir ? RtlDetermineDosPathNameType_U(dir) : RtlPathTypeUnknown;
            if (type != RtlPathTypeUncAbsolute && type != RtlPathTypeDriveAbsolute && type != RtlPathTypeLocalDevice)
                goto done;
            directory = join(L"", dir, nullptr);
        } else {
            WCHAR windows[MAX_PATH];
            GetWindowsDirectoryW(windows, RTL_NUMBER_OF(windows));
            directory = join(windows, L"WinSxs", nullptr);
        }
        if (!directory) {
            status = STATUS_NO_MEMORY;
            goto done;
        }
        if (from && RtlDetermineDosPathNameType_U(from) != RtlPathTypeRelative) {
            source = join(L"", from, nullptr);
        } else if (from) {
            source = join(directory, from, nullptr);
        } else if (flags & ACTCTX_FLAG_APPLICATION_NAME_VALID) {
            if (!actctx->lpApplicationName || !*actctx->lpApplicationName)
                goto done;
            /* <application>.Manifest, then Application.Manifest when it is missing. */
            source = join(directory, actctx->lpApplicationName, L".Manifest");
            if (source && !file_exists(source)) {
                KbFree(source);
                source = join(directory, L"Application.Manifest", nullptr);
            }
        } else {
            source = join(directory, L"Application.Manifest", nullptr);
        }
    }
    if (!source) {
        status = STATUS_NO_MEMORY;
        goto done;
    }
    if ((flags & ACTCTX_FLAG_RESOURCE_NAME_VALID) && !actctx->lpResourceName)
        goto done;
    MIME_SXS_GENERATE request = {
        .Flags = flags,
        .Source = source,
        .AssemblyDirectory = directory,
        .ResourceName = (flags & ACTCTX_FLAG_RESOURCE_NAME_VALID) ? actctx->lpResourceName : nullptr,
        .Module = (flags & ACTCTX_FLAG_HMODULE_VALID) ? actctx->hModule : nullptr,
        .ProcessorArchitecture = arch,
        .LangId = (flags & ACTCTX_FLAG_LANGID_VALID) ? actctx->wLangId : 0,
    };
    status = RtlGenerateActivationContext(&request, &data);
    if (!NT_SUCCESS(status))
        goto done;
    if (flags & ACTCTX_FLAG_SET_PROCESS_DEFAULT) {
        /* the process default context has no handle: success returns NULL. */
        if (InterlockedCompareExchangePointer(&KbPeb()->ActivationContextData, data, nullptr)) {
            status = STATUS_SXS_PROCESS_DEFAULT_ALREADY_SET;
            goto done;
        }
        data = nullptr;
        result = nullptr;
        goto done;
    }
    PACTIVATION_CONTEXT context;
    status = RtlCreateActivationContext(0, data, 0, notification, nullptr, &context);
    if (!NT_SUCCESS(status))
        goto done;
    data = nullptr;
    result = context;
done:
    if (data)
        RtlFreeActivationContextData(data);
    KbFree(source);
    KbFree(directory);
    KbFree(module_path);
    if (result == INVALID_HANDLE_VALUE)
        KbSetStatus(status);
    return result;
}

/* the ansi structure has its own size checks, each one member off from the wide ones. */
HANDLE WINAPI CreateActCtxA(PCACTCTXA actctx)
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    if (!actctx || actctx->cbSize < offsetof(ACTCTXA, wProcessorArchitecture))
        return KbSetStatus(status), INVALID_HANDLE_VALUE;
    DWORD flags = actctx->dwFlags;
    ULONG size = actctx->cbSize;
    if ((flags & ~ACTCTX_VALID_FLAGS) ||
        ((flags & ACTCTX_FLAG_PROCESSOR_ARCHITECTURE_VALID) && size < offsetof(ACTCTXA, wLangId)) ||
        ((flags & ACTCTX_FLAG_LANGID_VALID) && size < offsetof(ACTCTXA, wLangId) + sizeof(LANGID)) ||
        ((flags & ACTCTX_FLAG_ASSEMBLY_DIRECTORY_VALID) && size < offsetof(ACTCTXA, lpResourceName)) ||
        ((flags & ACTCTX_FLAG_RESOURCE_NAME_VALID) && size < offsetof(ACTCTXA, lpApplicationName)) ||
        ((flags & ACTCTX_FLAG_APPLICATION_NAME_VALID) && size < offsetof(ACTCTXA, hModule)) ||
        ((flags & ACTCTX_FLAG_HMODULE_VALID) && size < sizeof(ACTCTXA)))
        return KbSetStatus(status), INVALID_HANDLE_VALUE;
    if (!actctx->lpSource && !(flags & ACTCTX_FLAG_HMODULE_VALID))
        return KbSetStatus(status), INVALID_HANDLE_VALUE;
    ACTCTXW w = {.cbSize = sizeof(w), .dwFlags = flags};
    PWSTR source = nullptr, directory = nullptr, resource = nullptr;
    if (actctx->lpSource && !(source = KbAnsiToWide(actctx->lpSource)))
        return INVALID_HANDLE_VALUE;
    w.lpSource = source;
    if (flags & ACTCTX_FLAG_PROCESSOR_ARCHITECTURE_VALID)
        w.wProcessorArchitecture = actctx->wProcessorArchitecture;
    if (flags & ACTCTX_FLAG_LANGID_VALID)
        w.wLangId = actctx->wLangId;
    if (flags & ACTCTX_FLAG_HMODULE_VALID)
        w.hModule = actctx->hModule;
    if (flags & ACTCTX_FLAG_ASSEMBLY_DIRECTORY_VALID) {
        directory = actctx->lpAssemblyDirectory ? KbAnsiToWide(actctx->lpAssemblyDirectory) : nullptr;
        if (directory && wcslen(directory) > MAX_PATH) {
            KbFree(source);
            KbFree(directory);
            return KbSetStatus(STATUS_NAME_TOO_LONG), INVALID_HANDLE_VALUE;
        }
        w.lpAssemblyDirectory = directory;
    }
    if (flags & ACTCTX_FLAG_RESOURCE_NAME_VALID) {
        if (IS_INTRESOURCE(actctx->lpResourceName))
            w.lpResourceName = (LPCWSTR)actctx->lpResourceName;
        else if (actctx->lpResourceName)
            w.lpResourceName = resource = KbAnsiToWide(actctx->lpResourceName);
    }
    /* like windows' CreateActCtxA, lpApplicationName is dropped. */
    HANDLE result = CreateActCtxW(&w);
    DWORD error = GetLastError();
    KbFree(source);
    KbFree(directory);
    KbFree(resource);
    SetLastError(error);
    return result;
}

void WINAPI AddRefActCtx(HANDLE actctx)
{
    RtlAddRefActivationContext(actctx);
}

void WINAPI ReleaseActCtx(HANDLE actctx)
{
    RtlReleaseActivationContext(actctx);
}

BOOL WINAPI ZombifyActCtx(HANDLE actctx)
{
    NTSTATUS status = RtlZombifyActivationContext(actctx);
    return NT_SUCCESS(status) ? TRUE : fail(status);
}

/* like windows, an activation frame holds no reference to the context. */
BOOL WINAPI ActivateActCtx(HANDLE actctx, ULONG_PTR *cookie)
{
    if (actctx == INVALID_HANDLE_VALUE)
        return fail(STATUS_INVALID_PARAMETER);
    NTSTATUS status = RtlActivateActivationContext(0, actctx, cookie);
    return NT_SUCCESS(status) ? TRUE : fail(status);
}

BOOL WINAPI DeactivateActCtx(DWORD flags, ULONG_PTR cookie)
{
    if (flags & ~DEACTIVATE_ACTCTX_FLAG_FORCE_EARLY_DEACTIVATION)
        return fail(STATUS_INVALID_PARAMETER);
    RtlDeactivateActivationContext(flags & DEACTIVATE_ACTCTX_FLAG_FORCE_EARLY_DEACTIVATION, cookie);
    return TRUE;
}

BOOL WINAPI GetCurrentActCtx(HANDLE *actctx)
{
    if (!actctx)
        return fail(STATUS_INVALID_PARAMETER);
    NTSTATUS status = RtlGetActiveActivationContext((PACTIVATION_CONTEXT *)actctx);
    return NT_SUCCESS(status) ? TRUE : fail(status);
}

/* checks the caller's structure, then has ntdll fill a full sized one that is copied back. */
static BOOL find_parameters(DWORD flags, PACTCTX_SECTION_KEYED_DATA data, ULONG *rtl_flags)
{
    *rtl_flags = 0;
    if (!data || data->cbSize < offsetof(ACTCTX_SECTION_KEYED_DATA, ulAssemblyRosterIndex) || (flags & ~7u)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (flags & FIND_ACTCTX_SECTION_KEY_RETURN_HACTCTX) {
        if (data->cbSize < MEMBER_END(ACTCTX_SECTION_KEYED_DATA, hActCtx))
            goto invalid;
        *rtl_flags |= FIND_ACTIVATION_CONTEXT_SECTION_KEY_RETURN_ACTIVATION_CONTEXT;
    }
    if (flags & FIND_ACTCTX_SECTION_KEY_RETURN_FLAGS) {
        if (data->cbSize < MEMBER_END(ACTCTX_SECTION_KEYED_DATA, ulFlags))
            goto invalid;
        *rtl_flags |= FIND_ACTIVATION_CONTEXT_SECTION_KEY_RETURN_FLAGS;
    }
    if (flags & FIND_ACTCTX_SECTION_KEY_RETURN_ASSEMBLY_METADATA) {
        if (data->cbSize < sizeof(ACTCTX_SECTION_KEYED_DATA))
            goto invalid;
        *rtl_flags |= FIND_ACTIVATION_CONTEXT_SECTION_KEY_RETURN_ASSEMBLY_METADATA;
    }
    return TRUE;
invalid:
    KbSetError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

static void copy_found(DWORD flags, PACTCTX_SECTION_KEYED_DATA data, const ACTIVATION_CONTEXT_SECTION_KEYED_DATA *found)
{
    data->ulDataFormatVersion = found->DataFormatVersion;
    data->lpData = found->Data;
    data->ulLength = found->Length;
    data->lpSectionGlobalData = found->SectionGlobalData;
    data->ulSectionGlobalDataLength = found->SectionGlobalDataLength;
    data->lpSectionBase = found->SectionBase;
    data->ulSectionTotalLength = found->SectionTotalLength;
    data->hActCtx = found->ActivationContext;
    if (data->cbSize >= MEMBER_END(ACTCTX_SECTION_KEYED_DATA, ulAssemblyRosterIndex))
        data->ulAssemblyRosterIndex = found->AssemblyRosterIndex;
    if (flags & FIND_ACTCTX_SECTION_KEY_RETURN_FLAGS)
        data->ulFlags = found->Flags;
    if (flags & FIND_ACTCTX_SECTION_KEY_RETURN_ASSEMBLY_METADATA) {
        data->AssemblyMetadata.lpInformation = found->AssemblyMetadata.Information;
        data->AssemblyMetadata.lpSectionBase = found->AssemblyMetadata.SectionBase;
        data->AssemblyMetadata.ulSectionLength = found->AssemblyMetadata.SectionLength;
        data->AssemblyMetadata.lpSectionGlobalDataBase = found->AssemblyMetadata.SectionGlobalDataBase;
        data->AssemblyMetadata.ulSectionGlobalDataLength = found->AssemblyMetadata.SectionGlobalDataLength;
    }
}

static BOOL find_string(DWORD flags, const GUID *extension, ULONG section, PCUNICODE_STRING key,
                        PACTCTX_SECTION_KEYED_DATA data)
{
    ULONG rtl_flags;
    if (!find_parameters(flags, data, &rtl_flags))
        return FALSE;
    ACTIVATION_CONTEXT_SECTION_KEYED_DATA found = {.Size = sizeof(found)};
    NTSTATUS status = RtlFindActivationContextSectionString(rtl_flags, extension, section, key, &found);
    if (!NT_SUCCESS(status))
        return fail(status);
    copy_found(flags, data, &found);
    return TRUE;
}

BOOL WINAPI FindActCtxSectionStringW(DWORD flags, const GUID *extension, ULONG section, LPCWSTR key,
                                     PACTCTX_SECTION_KEYED_DATA data)
{
    UNICODE_STRING us;
    if (key)
        RtlInitUnicodeString(&us, key);
    return find_string(flags, extension, section, key ? &us : nullptr, data);
}

BOOL WINAPI FindActCtxSectionStringA(DWORD flags, const GUID *extension, ULONG section, LPCSTR key,
                                     PACTCTX_SECTION_KEYED_DATA data)
{
    PWSTR wide = nullptr;
    UNICODE_STRING us;
    if (key) {
        if (!(wide = KbAnsiToWide(key)))
            return FALSE;
        RtlInitUnicodeString(&us, wide);
    }
    BOOL ok = find_string(flags, extension, section, key ? &us : nullptr, data);
    DWORD error = GetLastError();
    KbFree(wide);
    SetLastError(error);
    return ok;
}

BOOL WINAPI FindActCtxSectionGuid(DWORD flags, const GUID *extension, ULONG section, const GUID *key,
                                  PACTCTX_SECTION_KEYED_DATA data)
{
    ULONG rtl_flags;
    if (!find_parameters(flags, data, &rtl_flags))
        return FALSE;
    ACTIVATION_CONTEXT_SECTION_KEYED_DATA found = {.Size = sizeof(found)};
    NTSTATUS status = RtlFindActivationContextSectionGuid(rtl_flags, extension, section, key, &found);
    if (!NT_SUCCESS(status))
        return fail(status);
    copy_found(flags, data, &found);
    return TRUE;
}

BOOL WINAPI QueryActCtxW(DWORD flags, HANDLE actctx, PVOID subinstance, ULONG info_class, PVOID buffer, SIZE_T size,
                         SIZE_T *written)
{
    if (written)
        *written = 0;
    /* the low two bits are older spellings of the three context kinds. */
    switch (flags & 3) {
    case 1:
        flags |= QUERY_ACTCTX_FLAG_USE_ACTIVE_ACTCTX;
        break;
    case 2:
        flags |= QUERY_ACTCTX_FLAG_ACTCTX_IS_HMODULE;
        break;
    case 3:
        flags |= QUERY_ACTCTX_FLAG_ACTCTX_IS_ADDRESS;
        break;
    }
    if (flags & 0x7FFFFFE0)
        return fail(STATUS_INVALID_PARAMETER_1);
    switch (info_class) {
    case ActivationContextBasicInformation:
    case ActivationContextDetailedInformation:
        break;
    case AssemblyDetailedInformationInActivationContext:
    case FileInformationInAssemblyOfAssemblyInActivationContext:
        if (!subinstance)
            return fail(STATUS_INVALID_PARAMETER_3);
        break;
    case RunlevelInformationInActivationContext:
    case CompatibilityInformationInActivationContext:
        if (subinstance)
            return fail(STATUS_INVALID_PARAMETER_3);
        break;
    default:
        return fail(STATUS_INVALID_PARAMETER_2);
    }
    if (!buffer) {
        if (size)
            return fail(STATUS_INVALID_PARAMETER_4);
        if (!written)
            return fail(STATUS_INVALID_PARAMETER_5);
    }
    ULONG rtl_flags = 0;
    switch (flags & 0x1C) {
    case 0:
        break;
    case QUERY_ACTCTX_FLAG_USE_ACTIVE_ACTCTX:
        rtl_flags = RTL_QUERY_INFORMATION_ACTIVATION_CONTEXT_FLAG_USE_ACTIVE_ACTIVATION_CONTEXT;
        break;
    case QUERY_ACTCTX_FLAG_ACTCTX_IS_HMODULE:
        rtl_flags = RTL_QUERY_INFORMATION_ACTIVATION_CONTEXT_FLAG_ACTIVATION_CONTEXT_IS_MODULE;
        break;
    case QUERY_ACTCTX_FLAG_ACTCTX_IS_ADDRESS:
        rtl_flags = RTL_QUERY_INFORMATION_ACTIVATION_CONTEXT_FLAG_ACTIVATION_CONTEXT_IS_ADDRESS;
        break;
    default:
        return fail(STATUS_INVALID_PARAMETER_1);
    }
    if (flags & QUERY_ACTCTX_FLAG_NO_ADDREF)
        rtl_flags |= RTL_QUERY_INFORMATION_ACTIVATION_CONTEXT_FLAG_NO_ADDREF;
    NTSTATUS status = RtlQueryInformationActivationContext(rtl_flags, actctx, subinstance, info_class, buffer, size,
                                                           written);
    return NT_SUCCESS(status) ? TRUE : fail(status);
}

BOOL WINAPI QueryActCtxSettingsW(DWORD flags, HANDLE actctx, PCWSTR ns, PCWSTR name, PWSTR buffer, SIZE_T size,
                                 SIZE_T *written)
{
    static const PCWSTR namespaces[] = {
        L"http://schemas.microsoft.com/SMI/2005/WindowsSettings", L"http://schemas.microsoft.com/SMI/2011/WindowsSettings",
        L"http://schemas.microsoft.com/SMI/2013/WindowsSettings", L"http://schemas.microsoft.com/SMI/2014/WindowsSettings",
        L"http://schemas.microsoft.com/SMI/2016/WindowsSettings", L"http://schemas.microsoft.com/SMI/2017/WindowsSettings",
        L"http://schemas.microsoft.com/SMI/2019/WindowsSettings", L"http://schemas.microsoft.com/SMI/2020/WindowsSettings",
        L"http://schemas.microsoft.com/SMI/2024/WindowsSettings",
    };
    if (written)
        *written = 0;
    if (flags)
        return fail(STATUS_INVALID_PARAMETER_1);
    if (!name)
        return fail(STATUS_INVALID_PARAMETER_4);
    if (size && !buffer)
        return fail(STATUS_INVALID_PARAMETER);
    if (ns) {
        ULONG i = 0;
        while (i < RTL_NUMBER_OF(namespaces) && wcscmp(ns, namespaces[i]))
            i++;
        if (i == RTL_NUMBER_OF(namespaces))
            return fail(STATUS_INVALID_PARAMETER_3);
    }
    NTSTATUS status = RtlQueryActivationContextApplicationSettings(0, actctx, ns, name, buffer, size, written);
    return NT_SUCCESS(status) ? TRUE : fail(status);
}

/* ------------------------------------------------------------------ manifest settings */

/*
 * the version a program is told. windows picks a compatibility level from the subsystem version and the manifest's
 * supportedOS ids, a subsystem version above 9 meaning the newest: below windows 8.1 the answer is 6.2.9200, at 8.1
 * it is 6.3.9600, from 10 on the real version.
 */
void KbReportedVersion(PULONG major, PULONG minor, PULONG build)
{
    static LONG cached = -1;
    static const struct {
        GUID Id;
        ULONG Level;
    } systems[] = {
        {{0xe2011457, 0x1546, 0x43c5, {0xa5, 0xfe, 0x00, 0x8d, 0xee, 0xe3, 0xd3, 0xf0}}, 0x00060000},
        {{0x35138b9a, 0x5d96, 0x4fbd, {0x8e, 0x2d, 0xa2, 0x44, 0x02, 0x25, 0xf9, 0x3a}}, 0x00060001},
        {{0x4a2f28e3, 0x53b9, 0x4441, {0xba, 0x9c, 0xd6, 0x9d, 0x4a, 0x4a, 0x6e, 0x38}}, 0x00060002},
        {{0x1f676c76, 0x80e1, 0x4239, {0x95, 0xbb, 0x83, 0xd0, 0xf6, 0xd0, 0xda, 0x78}}, 0x00060003},
        {{0x8e0f7a12, 0xbfb3, 0x4fe8, {0xb9, 0xa5, 0x48, 0xfd, 0x50, 0xa1, 0x5a, 0x9a}}, 0x000A0000},
    };
    PPEB peb = KbPeb();
    *major = peb->OSMajorVersion;
    *minor = peb->OSMinorVersion;
    *build = peb->OSBuildNumber;
    LONG level = cached;
    if (level == -1) {
        level = (LONG)(peb->ImageSubsystemMajorVersion << 16 | (peb->ImageSubsystemMinorVersion & 0xFFFF));
        if (peb->ImageSubsystemMajorVersion > 9) {
            level = 0x000A0000;
        } else {
            UCHAR buffer[sizeof(ACTIVATION_CONTEXT_COMPATIBILITY_INFORMATION) + 32 * sizeof(COMPATIBILITY_CONTEXT_ELEMENT)];
            ACTIVATION_CONTEXT_COMPATIBILITY_INFORMATION *info = (void *)buffer;
            SIZE_T length = 0;
            if (NT_SUCCESS(RtlQueryInformationActivationContext(0, nullptr, nullptr,
                                                                CompatibilityInformationInActivationContext, buffer,
                                                                sizeof(buffer), &length)) &&
                length >= sizeof(*info)) {
                for (ULONG i = 0; i < info->ElementCount && i < 32; i++) {
                    if (info->Elements[i].Type != ACTCTX_COMPATIBILITY_ELEMENT_TYPE_OS)
                        continue;
                    for (ULONG k = 0; k < RTL_NUMBER_OF(systems); k++)
                        if (!memcmp(&info->Elements[i].Id, &systems[k].Id, sizeof(GUID)) && (LONG)systems[k].Level > level)
                            level = (LONG)systems[k].Level;
                }
            }
        }
        cached = level;
    }
    if (level < 0x00060003) {
        *major = 6, *minor = 2, *build = 9200;
    } else if (level < 0x000A0000) {
        *major = 6, *minor = 3, *build = 9600;
    }
}

/* long paths need the machine's LongPathsEnabled and the program's longPathAware. */
void KbInitializeLongPaths(void)
{
    WCHAR value[8];
    if (!NT_SUCCESS(RtlQueryActivationContextApplicationSettings(
            0, nullptr, L"http://schemas.microsoft.com/SMI/2016/WindowsSettings", L"longPathAware", value,
            RTL_NUMBER_OF(value), nullptr)))
        return;
    /* windows looks at the first letter only. */
    if ((value[0] & ~0x20) != 'T')
        return;
    HKEY key;
    DWORD enabled = 0, size = sizeof(enabled);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", 0, KEY_QUERY_VALUE, &key))
        return;
    if (!RegQueryValueExW(key, L"LongPathsEnabled", nullptr, nullptr, (LPBYTE)&enabled, &size) && enabled)
        KbPeb()->BitField |= 0x80; /* IsLongPathAwareProcess */
    RegCloseKey(key);
}

/* a new thread starts with its creator's context active, unless that manifest says noInherit. */
BOOL KbInheritActivationContext(HANDLE process, PACTIVATION_CONTEXT *context)
{
    *context = nullptr;
    if (process != NtCurrentProcess() && GetProcessId(process) != GetCurrentProcessId())
        return FALSE;
    ACTIVATION_CONTEXT_BASIC_INFORMATION info;
    if (!NT_SUCCESS(RtlQueryInformationActivationContext(
            RTL_QUERY_INFORMATION_ACTIVATION_CONTEXT_FLAG_USE_ACTIVE_ACTIVATION_CONTEXT, nullptr, nullptr,
            ActivationContextBasicInformation, &info, sizeof(info), nullptr)))
        return FALSE;
    if (!info.hActCtx)
        return FALSE;
    if (info.dwFlags & 1 /* ACTIVATION_CONTEXT_FLAG_NO_INHERIT */) {
        RtlReleaseActivationContext(info.hActCtx);
        return FALSE;
    }
    *context = info.hActCtx;
    return TRUE;
}
