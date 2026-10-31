/*
 * the sources of security events: a program that audits (a database server, for one) installs its source below the
 * Security log of the event log service, with the files that hold the text of its events, and registers it to report
 * events. the events themselves go nowhere, since this system keeps no event log: reporting one is refused.
 */
#include <mime/win32/winbase.h>
#include <rtl.h>

#define SOURCES L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Security"
#define AUTHZ_EXPORT DECLSPEC_EXPORT

typedef struct OBJECT_NAME_OFFSET {
    PWSTR Name;
    DWORD Offset;
} OBJECT_NAME_OFFSET;

typedef struct SOURCE_REGISTRATION {
    DWORD Flags;
    PWSTR Name;
    PWSTR MessageFile;
    PWSTR XmlSchemaFile;
    PWSTR AccessStringsFile;
    PWSTR ExecutableImagePath;
    PVOID Reserved;
    DWORD ObjectTypeNameCount;
    OBJECT_NAME_OFFSET ObjectTypeNames[1];
} SOURCE_REGISTRATION;

#define AUTHZ_SOURCE_PROVIDER_GUID 2
#define SOURCE_HANDLE_MAGIC 0x45535A41

typedef struct SOURCE_HANDLE {
    DWORD Magic;
    WCHAR Name[256];
} SOURCE_HANDLE;

static LONG set_text(HKEY key, const WCHAR *name, DWORD type, const WCHAR *text)
{
    return RegSetValueExW(key, name, 0, type, (const BYTE *)text, (DWORD)((wcslen(text) + 1) * sizeof(WCHAR)));
}

static void guid_text(const GUID *g, WCHAR *out, SIZE_T chars)
{
    _snwprintf(out, chars, L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g->Data1, g->Data2, g->Data3, g->Data4[0],
               g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

static BOOL fail(DWORD error)
{
    SetLastError(error);
    return FALSE;
}

AUTHZ_EXPORT BOOL WINAPI AuthzInstallSecurityEventSource(DWORD flags, SOURCE_REGISTRATION *registration)
{
    if (!registration || !registration->Name || !_wcsicmp(registration->Name, L"Microsoft-Windows-Security-Auditing") ||
        !_wcsicmp(registration->Name, L"Security"))
        return fail(ERROR_INVALID_PARAMETER);
    HKEY log = nullptr, source = nullptr, names = nullptr;
    DWORD disposition = 0;
    LONG status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, SOURCES, 0, KEY_ALL_ACCESS, &log);
    if (!status)
        status = RegCreateKeyExW(log, registration->Name, 0, nullptr, 0, KEY_SET_VALUE | KEY_CREATE_SUB_KEY, nullptr, &source,
                                 &disposition);
    /* a source that is there is not installed again. */
    if (!status && disposition == REG_OPENED_EXISTING_KEY)
        status = 5010;
    if (!status)
        status = RegSetValueExW(source, L"EventSourceFlags", 0, REG_DWORD, (const BYTE *)&registration->Flags, sizeof(DWORD));
    if (!status && (registration->Flags & AUTHZ_SOURCE_PROVIDER_GUID)) {
        WCHAR guid[48];
        if (!registration->Reserved)
            status = ERROR_INVALID_PARAMETER;
        else {
            guid_text(registration->Reserved, guid, RTL_NUMBER_OF(guid));
            status = set_text(source, L"ProviderGuid", REG_EXPAND_SZ, guid);
        }
    }
    if (!status && registration->MessageFile)
        status = set_text(source, L"EventMessageFile", REG_EXPAND_SZ, registration->MessageFile);
    if (!status && registration->AccessStringsFile)
        status = set_text(source, L"ParameterMessageFile", REG_EXPAND_SZ, registration->AccessStringsFile);
    if (!status && registration->ExecutableImagePath)
        status = set_text(source, L"ExecutableImagePath", REG_MULTI_SZ, registration->ExecutableImagePath);
    if (!status && registration->XmlSchemaFile)
        status = set_text(source, L"XmlSchemaFile", REG_SZ, registration->XmlSchemaFile);
    if (!status && registration->ObjectTypeNameCount) {
        status = RegCreateKeyExW(source, L"ObjectNames", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &names, &disposition);
        for (DWORD i = 0; !status && i < registration->ObjectTypeNameCount; i++)
            status = RegSetValueExW(names, registration->ObjectTypeNames[i].Name, 0, REG_DWORD,
                                    (const BYTE *)&registration->ObjectTypeNames[i].Offset, sizeof(DWORD));
    }
    if (names)
        RegCloseKey(names);
    if (source)
        RegCloseKey(source);
    if (log)
        RegCloseKey(log);
    return status ? fail((DWORD)status) : TRUE;
}

AUTHZ_EXPORT BOOL WINAPI AuthzUninstallSecurityEventSource(DWORD flags, PCWSTR name)
{
    if (!name)
        return fail(ERROR_INVALID_PARAMETER);
    HKEY log;
    LONG status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, SOURCES, 0, KEY_ALL_ACCESS, &log);
    if (!status) {
        status = RegDeleteTreeW(log, name);
        RegCloseKey(log);
    }
    return status ? fail((DWORD)status) : TRUE;
}

AUTHZ_EXPORT BOOL WINAPI AuthzRegisterSecurityEventSource(DWORD flags, PCWSTR name, PVOID *provider)
{
    if (!name || !provider || flags)
        return fail(ERROR_INVALID_PARAMETER);
    WCHAR path[400];
    _snwprintf(path, RTL_NUMBER_OF(path), SOURCES L"\\%ls", name);
    HKEY key;
    LONG status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE, &key);
    if (status)
        return fail((DWORD)status);
    RegCloseKey(key);
    SOURCE_HANDLE *handle = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*handle));
    if (!handle)
        return fail(ERROR_NOT_ENOUGH_MEMORY);
    handle->Magic = SOURCE_HANDLE_MAGIC;
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(handle->Name) - 1 && name[i]; i++)
        handle->Name[i] = name[i];
    *provider = handle;
    return TRUE;
}

AUTHZ_EXPORT BOOL WINAPI AuthzUnregisterSecurityEventSource(DWORD flags, PVOID *provider)
{
    if (!provider || !*provider || ((SOURCE_HANDLE *)*provider)->Magic != SOURCE_HANDLE_MAGIC)
        return fail(ERROR_INVALID_HANDLE);
    ((SOURCE_HANDLE *)*provider)->Magic = 0;
    HeapFree(GetProcessHeap(), 0, *provider);
    *provider = nullptr;
    return TRUE;
}

/* an event has no log to go to. */
AUTHZ_EXPORT BOOL WINAPI AuthzReportSecurityEventFromParams(DWORD flags, PVOID provider, DWORD audit_id, PVOID user_sid, PVOID parameters)
{
    if (!provider || ((SOURCE_HANDLE *)provider)->Magic != SOURCE_HANDLE_MAGIC)
        return fail(ERROR_INVALID_HANDLE);
    return fail(ERROR_NOT_SUPPORTED);
}

AUTHZ_EXPORT BOOL __cdecl AuthzReportSecurityEvent(DWORD flags, PVOID provider, DWORD audit_id, PVOID user_sid, DWORD count, ...)
{
    if (!provider || ((SOURCE_HANDLE *)provider)->Magic != SOURCE_HANDLE_MAGIC)
        return fail(ERROR_INVALID_HANDLE);
    return fail(ERROR_NOT_SUPPORTED);
}

/* the sources below the log, each as the registration it was installed with, in one block. */
AUTHZ_EXPORT BOOL WINAPI AuthzEnumerateSecurityEventSources(DWORD flags, SOURCE_REGISTRATION *buffer, PDWORD count, PDWORD length)
{
    if (!count || !length)
        return fail(ERROR_INVALID_PARAMETER);
    HKEY log;
    LONG status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, SOURCES, 0, KEY_READ, &log);
    if (status)
        return fail((DWORD)status);
    DWORD capacity = *length, needed = 0, found = 0;
    BYTE *at = (BYTE *)buffer;
    for (DWORD i = 0;; i++) {
        WCHAR name[256];
        DWORD n = RTL_NUMBER_OF(name);
        if (RegEnumKeyExW(log, i, name, &n, nullptr, nullptr, nullptr, nullptr))
            break;
        /* the entries of the system are not registrations of programs. */
        if (!_wcsicmp(name, L"Security") || !_wcsicmp(name, L"Microsoft-Windows-Security-Auditing"))
            continue;
        HKEY source;
        if (RegOpenKeyExW(log, name, 0, KEY_READ, &source))
            continue;
        SOURCE_REGISTRATION r = {};
        DWORD size = sizeof(DWORD), type;
        RegQueryValueExW(source, L"EventSourceFlags", nullptr, &type, (BYTE *)&r.Flags, &size);
        r.Flags &= ~AUTHZ_SOURCE_PROVIDER_GUID;
        WCHAR message[MAX_PATH] = L"", strings[MAX_PATH] = L"", image[MAX_PATH] = L"", schema[MAX_PATH] = L"";
        const struct {
            const WCHAR *Value;
            WCHAR *Text;
        } fields[] = {{L"EventMessageFile", message}, {L"ParameterMessageFile", strings}, {L"ExecutableImagePath", image},
                      {L"XmlSchemaFile", schema}};
        for (int f = 0; f < 4; f++) {
            size = (MAX_PATH - 1) * sizeof(WCHAR);
            if (RegQueryValueExW(source, fields[f].Value, nullptr, &type, (BYTE *)fields[f].Text, &size))
                fields[f].Text[0] = 0;
        }
        SIZE_T text_bytes = (wcslen(name) + wcslen(message) + wcslen(strings) + wcslen(image) + wcslen(schema) + 5) * sizeof(WCHAR);
        DWORD entry = (DWORD)((sizeof(SOURCE_REGISTRATION) + text_bytes + 7) & ~(SIZE_T)7);
        if (capacity >= needed + entry && buffer) {
            SOURCE_REGISTRATION *out = (SOURCE_REGISTRATION *)(at + needed);
            memset(out, 0, entry);
            out->Flags = r.Flags;
            WCHAR *text = (WCHAR *)(out + 1);
            PWSTR *targets[] = {&out->Name, &out->MessageFile, &out->AccessStringsFile, &out->ExecutableImagePath,
                                &out->XmlSchemaFile};
            const WCHAR *values[] = {name, message, strings, image, schema};
            for (int t = 0; t < 5; t++) {
                if (!*values[t] && t) {
                    *targets[t] = nullptr;
                    continue;
                }
                *targets[t] = text;
                wcscpy(text, values[t]);
                text += wcslen(values[t]) + 1;
            }
        }
        needed += entry;
        found++;
        RegCloseKey(source);
    }
    RegCloseKey(log);
    *count = found;
    if (capacity < needed) {
        *length = needed;
        return fail(ERROR_INSUFFICIENT_BUFFER);
    }
    *length = needed;
    return TRUE;
}
