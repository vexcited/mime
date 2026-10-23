/*
 * the loader's side of activation contexts: the process and system default
 * contexts built at process start, dll manifests and dll name redirection.
 */
#include "sxs.h"

static PLDR_MANIFEST_PROBER_ROUTINE g_prober;

/* windows builds the system default context from a manifest in winsxs, Mime's has a root assembly and no
   redirection. */
static const char g_system_default_manifest[] =
    "<?xml version='1.0' encoding='UTF-8' standalone='yes'?>"
    "<assembly xmlns='urn:schemas-microsoft-com:asm.v1' manifestVersion='1.0'>"
    "<assemblyIdentity type='win32' name='Microsoft.Windows.SystemCompatible' version='10.0.26300.0' "
    "processorArchitecture='*' publicKeyToken='6595b64144ccf1df'/>"
    "</assembly>";

/* kernel32 registers its prober here, until then the loader probes by itself. */
NTDLL_EXPORT void NTAPI LdrSetDllManifestProber(PLDR_MANIFEST_PROBER_ROUTINE prober,
                                                PLDR_ACTCTX_LANGUAGE_ROURINE language,
                                                PLDR_RELEASE_ACTCTX_ROUTINE release)
{
    g_prober = prober;
}

static void NTAPI data_notification(ULONG type, PACTIVATION_CONTEXT context, const void *data,
                                    PVOID notification_context, PVOID notification_data, PBOOLEAN disable)
{
    if (type == ACTIVATION_CONTEXT_NOTIFICATION_DESTROY)
        MimeSxsFreeActivationContextData((PVOID)data);
    else if (disable)
        *disable = TRUE;
}

/* statuses that just mean the dll has no manifest. */
static BOOLEAN probe_status_ignored(NTSTATUS status)
{
    return status == STATUS_NO_SUCH_FILE || status == STATUS_RESOURCE_DATA_NOT_FOUND ||
        status == STATUS_RESOURCE_TYPE_NOT_FOUND || status == STATUS_RESOURCE_NAME_NOT_FOUND ||
        status == STATUS_NOT_SUPPORTED || status == STATUS_NOT_IMPLEMENTED || status == (NTSTATUS)0xC0000304;
}

NTSTATUS LdrpSxsProbeDll(PVOID base, PCWSTR full_path, PACTIVATION_CONTEXT *context)
{
    *context = nullptr;
    NTSTATUS status;
    if (g_prober) {
        status = g_prober(base, full_path, context);
    } else {
        /* only a dll with an RT_MANIFEST resource 2 has a context of its own. */
        LDR_RESOURCE_INFO info = {24, 2, 0};
        IMAGE_RESOURCE_DIRECTORY *dir;
        status = LdrFindResourceDirectory_U(base, &info, 2, &dir);
        if (NT_SUCCESS(status)) {
            MIME_SXS_GENERATE request = {.Flags = 0x88, .Source = full_path, .ResourceName = (PCWSTR)2, .Module = base};
            PVOID data;
            status = MimeSxsGenerateActivationContext(&request, &data);
            if (NT_SUCCESS(status)) {
                status = RtlCreateActivationContext(0, data, 0, data_notification, nullptr, context);
                if (!NT_SUCCESS(status))
                    MimeSxsFreeActivationContextData(data);
            }
        }
    }
    if (probe_status_ignored(status))
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(status))
        DbgPrint("ntdll: the manifest of %ls cannot be activated (%08x)\n", full_path, status);
    return status;
}

/* ------------------------------------------------------------------ redirection */

static BOOLEAN has_separator(PCWSTR name, SIZE_T chars)
{
    for (SIZE_T i = 0; i < chars; i++)
        if (name[i] == '\\' || name[i] == '/' || name[i] == ':')
            return TRUE;
    return FALSE;
}

/*
 * the result is the assembly's storage root when the entry omits it, the
 * path segments, then the requested name unless the segments already end
 * with it.
 */
static NTSTATUS redirect(PCUNICODE_STRING key, PWSTR out, SIZE_T out_chars, PULONG out_length)
{
    ACTIVATION_CONTEXT_SECTION_KEYED_DATA found = {.Size = sizeof(found)};
    NTSTATUS status = RtlFindActivationContextSectionString(
        FIND_ACTIVATION_CONTEXT_SECTION_KEY_RETURN_FLAGS | FIND_ACTIVATION_CONTEXT_SECTION_KEY_RETURN_ASSEMBLY_METADATA,
        nullptr, ACTIVATION_CONTEXT_SECTION_DLL_REDIRECTION, key, &found);
    if (status == STATUS_SXS_SECTION_NOT_FOUND)
        return STATUS_SXS_KEY_NOT_FOUND;
    if (!NT_SUCCESS(status))
        return status;
    const ACTCTX_DLL_REDIRECTION *r = found.Data;
    if (found.Length < sizeof(*r) || found.DataFormatVersion != 1)
        return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
    const UCHAR *section = found.SectionBase;
    SIZE_T n = 0;
    out[0] = 0;
    if (r->Flags & ACTCTX_DLL_OMITS_ASSEMBLY_ROOT) {
        const ACTCTX_ASSEMBLY_INFORMATION *info = found.AssemblyMetadata.Information;
        const UCHAR *info_section = found.AssemblyMetadata.SectionBase;
        if (!info || !info_section)
            return STATUS_SXS_INVALID_ACTCTXDATA_FORMAT;
        if (info->Flags & ACTCTX_ASSEMBLY_PRIVATE) {
            /* a private assembly's root is the directory of its manifest. */
            PCWSTR manifest = (PCWSTR)(info_section + info->ManifestPathOffset);
            SIZE_T chars = info->ManifestPathLength / sizeof(WCHAR);
            while (chars && manifest[chars - 1] != '\\')
                chars--;
            if (!chars || chars >= out_chars)
                return STATUS_INTERNAL_ERROR;
            memcpy(out, manifest, chars * sizeof(WCHAR));
            n = chars;
        } else {
            /* shared assemblies live in winsxs, which Mime does not have. */
            static const WCHAR winsxs[] = L"C:\\Windows\\WinSxS\\";
            SIZE_T chars = RTL_NUMBER_OF(winsxs) - 1, dir = info->AssemblyDirectoryNameLength / sizeof(WCHAR);
            if (chars + dir + 1 >= out_chars)
                return STATUS_NAME_TOO_LONG;
            memcpy(out, winsxs, chars * sizeof(WCHAR));
            memcpy(out + chars, info_section + info->AssemblyDirectoryNameOffset, dir * sizeof(WCHAR));
            n = chars + dir;
            out[n++] = '\\';
        }
    }
    const ACTCTX_DLL_PATH_SEGMENT *segments = (const void *)(section + r->PathSegmentOffset);
    for (ULONG i = 0; i < r->PathSegmentCount; i++) {
        SIZE_T chars = segments[i].Length / sizeof(WCHAR);
        if (n + chars >= out_chars)
            return STATUS_NAME_TOO_LONG;
        memcpy(out + n, section + segments[i].Offset, segments[i].Length);
        n += chars;
    }
    if (!(r->Flags & ACTCTX_DLL_PATH_INCLUDES_BASE_NAME)) {
        PCWSTR name = key->Buffer;
        SIZE_T chars = key->Length / sizeof(WCHAR);
        if (r->Flags & ACTCTX_DLL_SYSTEM32) {
            SIZE_T start = chars;
            while (start && name[start - 1] != '\\' && name[start - 1] != '/')
                start--;
            name += start;
            chars -= start;
        }
        if (n + chars >= out_chars)
            return STATUS_NAME_TOO_LONG;
        memcpy(out + n, name, chars * sizeof(WCHAR));
        n += chars;
    }
    out[n] = 0;
    if (r->Flags & ACTCTX_DLL_EXPAND) {
        WCHAR expanded[MAX_PATH];
        UNICODE_STRING src = {(USHORT)(n * sizeof(WCHAR)), (USHORT)((n + 1) * sizeof(WCHAR)), out};
        UNICODE_STRING dst = {0, sizeof(expanded) - sizeof(WCHAR), expanded};
        status = RtlExpandEnvironmentStrings_U(nullptr, &src, &dst, nullptr);
        if (!NT_SUCCESS(status))
            return status;
        n = dst.Length / sizeof(WCHAR);
        if (n >= out_chars)
            return STATUS_NAME_TOO_LONG;
        memcpy(out, expanded, n * sizeof(WCHAR));
        out[n] = 0;
    }
    if (out_length)
        *out_length = (ULONG)n;
    return STATUS_SUCCESS;
}

/* the name with ".dll" added when it has no extension, the key the loader looks up. */
static BOOLEAN dll_key(PCWSTR name, PWSTR buffer, SIZE_T chars, PUNICODE_STRING key)
{
    SIZE_T n = wcslen(name);
    BOOLEAN dot = wcschr(name, '.') != nullptr;
    if (n + 5 > chars)
        return FALSE;
    memcpy(buffer, name, n * sizeof(WCHAR));
    if (!dot) {
        memcpy(buffer + n, L".dll", 4 * sizeof(WCHAR));
        n += 4;
    }
    buffer[n] = 0;
    key->Buffer = buffer;
    key->Length = (USHORT)(n * sizeof(WCHAR));
    key->MaximumLength = (USHORT)((n + 1) * sizeof(WCHAR));
    return TRUE;
}

NTSTATUS LdrpSxsRedirectDll(PCWSTR name, PWSTR out, SIZE_T out_chars)
{
    SIZE_T n = wcslen(name);
    if (!n || has_separator(name, n))
        return STATUS_SXS_KEY_NOT_FOUND;
    WCHAR buffer[MAX_PATH];
    UNICODE_STRING key;
    if (!dll_key(name, buffer, RTL_NUMBER_OF(buffer), &key))
        return STATUS_SXS_KEY_NOT_FOUND;
    return redirect(&key, out, out_chars, nullptr);
}

NTDLL_EXPORT NTSTATUS NTAPI RtlDosApplyFileIsolationRedirection_Ustr(ULONG flags, PCUNICODE_STRING name,
                                                                     PCUNICODE_STRING extension,
                                                                     PUNICODE_STRING static_buf,
                                                                     PUNICODE_STRING dynamic_buf,
                                                                     PUNICODE_STRING *new_name, PULONG new_flags,
                                                                     PSIZE_T file_name_offset, PSIZE_T required)
{
    if (new_flags)
        *new_flags = 0;
    if (new_name)
        *new_name = nullptr;
    if (required)
        *required = 0;
    if ((flags & ~1u) || !name)
        return STATUS_INVALID_PARAMETER;
    if (static_buf ? dynamic_buf && !new_name : !dynamic_buf && new_name)
        return STATUS_INVALID_PARAMETER;
    SIZE_T chars = name->Length / sizeof(WCHAR);
    if (!chars || has_separator(name->Buffer, chars))
        return STATUS_SXS_KEY_NOT_FOUND;
    WCHAR buffer[MAX_PATH];
    if (chars + 1 + (extension ? extension->Length / sizeof(WCHAR) : 0) > RTL_NUMBER_OF(buffer))
        return STATUS_NAME_TOO_LONG;
    memcpy(buffer, name->Buffer, chars * sizeof(WCHAR));
    BOOLEAN dot = FALSE;
    for (SIZE_T i = 0; i < chars; i++)
        dot |= name->Buffer[i] == '.';
    if (!dot && extension && extension->Length) {
        memcpy(buffer + chars, extension->Buffer, extension->Length);
        chars += extension->Length / sizeof(WCHAR);
    }
    buffer[chars] = 0;
    UNICODE_STRING key = {(USHORT)(chars * sizeof(WCHAR)), (USHORT)((chars + 1) * sizeof(WCHAR)), buffer};
    WCHAR path[MAX_PATH];
    ULONG length;
    NTSTATUS status = redirect(&key, path, RTL_NUMBER_OF(path), &length);
    if (!NT_SUCCESS(status))
        return status;
    SIZE_T bytes = length * sizeof(WCHAR);
    if (required)
        *required = bytes + sizeof(WCHAR);
    PUNICODE_STRING target = nullptr;
    if (static_buf && static_buf->MaximumLength >= bytes + sizeof(WCHAR)) {
        target = static_buf;
    } else if (dynamic_buf) {
        if (!RtlCreateUnicodeString(dynamic_buf, path))
            return STATUS_NO_MEMORY;
        target = dynamic_buf;
    } else if (static_buf) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (target == static_buf && target) {
        memcpy(target->Buffer, path, bytes + sizeof(WCHAR));
        target->Length = (USHORT)bytes;
    }
    if (new_name)
        *new_name = target;
    if (file_name_offset) {
        SIZE_T start = length;
        while (start && path[start - 1] != '\\')
            start--;
        *file_name_offset = start * sizeof(WCHAR);
    }
    if (new_flags)
        *new_flags |= 2;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ process start */

static void apply_code_pages(void)
{
    WCHAR value[15];
    if (!NT_SUCCESS(RtlQueryActivationContextApplicationSettings(
            0, nullptr, L"http://schemas.microsoft.com/SMI/2019/WindowsSettings", L"activeCodePage", value,
            RTL_NUMBER_OF(value), nullptr)))
        return;
    /*
     * "Legacy" keeps the system code pages and a locale name takes that
     * locale's. Mime keeps the system's for both, its ntdll has no locale table.
     */
    if (!wcscmp(value, L"UTF-8")) {
        PPEB peb = NtCurrentPeb();
        peb->ActiveCodePage = 65001;
        peb->OemCodePage = 65001;
    }
}

NTSTATUS LdrpSxsInitializeProcess(PVOID image, PCWSTR image_path)
{
    PPEB peb = NtCurrentPeb();
    PVOID data;
    WCHAR system_path[MAX_PATH];
    wcscpy(system_path, SxspSystemDirectory());
    wcscat(system_path, L"ntdll.dll");
    if (!peb->SystemDefaultActivationContextData &&
        NT_SUCCESS(SxspGenerateFromText(g_system_default_manifest, sizeof(g_system_default_manifest) - 1, system_path,
                                        &data)))
        peb->SystemDefaultActivationContextData = data;
    if (peb->ActivationContextData || !image_path)
        return STATUS_SUCCESS;
    /* the program's manifest: resource 1, or <program>.manifest beside it. */
    MIME_SXS_GENERATE request = {.Source = image_path, .ResourceName = (PCWSTR)1, .Module = image};
    NTSTATUS status = MimeSxsGenerateActivationContext(&request, &data);
    if (status == STATUS_RESOURCE_DATA_NOT_FOUND || status == STATUS_RESOURCE_TYPE_NOT_FOUND ||
        status == STATUS_RESOURCE_NAME_NOT_FOUND || status == STATUS_RESOURCE_LANG_NOT_FOUND)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(status))
        return status;
    peb->ActivationContextData = data;
    apply_code_pages();
    return STATUS_SUCCESS;
}
