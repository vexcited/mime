/*
 * the identity providers of the machine, kept by windows in the registry below Software\Microsoft\IdentityStore\Providers
 * and cached by sechost.dll. every call reads the registry again, so the answers follow it.
 */
#define MIME_BUILD_ADVAPI32 1
#include "advapi32_private.h"

#define IsEqualGUID(a, b) (!memcmp((a), (b), sizeof(GUID)))
#define IDP_KEY L"Software\\Microsoft\\IdentityStore\\Providers"
#define IDP_MAGIC 0x43495344
#define IDP_CONNECTED 1
#define IDP_NAME_CHARS 127

/* the record programs get a copy of: its size, the name, the identity and whether the provider is connected. */
typedef struct IDP_INFO {
    DWORD Size;
    PWSTR Name;
    GUID Id;
    DWORD Flags;
} IDP_INFO;

typedef struct IDP_ENTRY {
    DWORD Magic;
    BOOL Default;
    struct IDP_SNAPSHOT *Snapshot;
    IDP_INFO Info;
    WCHAR Name[IDP_NAME_CHARS + 1];
} IDP_ENTRY;

typedef struct IDP_SNAPSHOT {
    DWORD Count;
    IDP_ENTRY Entries[1];
} IDP_SNAPSHOT;

static DWORD info_size(const IDP_ENTRY *entry)
{
    return (DWORD)((sizeof(IDP_INFO) + (wcslen(entry->Name) + 1) * sizeof(WCHAR) + 7) & ~(SIZE_T)7);
}

/* a value of the wanted type, 0 when it is missing and not required. */
static DWORD read_value(HKEY key, PCWSTR name, DWORD type, BOOL required, BYTE *buffer, DWORD size)
{
    DWORD actual = 0, length = size;
    LONG error = RegQueryValueExW(key, name, nullptr, &actual, buffer, &length);
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return required ? ERROR_NOT_FOUND : ERROR_SUCCESS;
    if (error)
        return (DWORD)error;
    if (actual != type || length > size)
        return ERROR_INVALID_DATA;
    if (type == REG_SZ && ((length & 1) || ((WCHAR *)buffer)[length / 2 - 1]))
        return ERROR_INVALID_DATA;
    return ERROR_SUCCESS;
}

static BOOL guid_of(PCWSTR text, GUID *guid)
{
    UNICODE_STRING string;
    RtlInitUnicodeString(&string, text);
    return NT_SUCCESS(RtlGUIDFromString(&string, guid));
}

/* reads the providers into a snapshot. ERROR_NOT_SUPPORTED when the machine has no identity store. */
static DWORD load_snapshot(IDP_SNAPSHOT **result)
{
    HKEY root;
    LONG error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, IDP_KEY, 0, KEY_READ, &root);
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return ERROR_NOT_SUPPORTED;
    if (error)
        return (DWORD)error;
    DWORD keys = 0;
    RegQueryInfoKeyW(root, nullptr, nullptr, nullptr, &keys, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                     nullptr);
    IDP_SNAPSHOT *snapshot = LocalAlloc(LMEM_ZEROINIT, sizeof(IDP_SNAPSHOT) + (SIZE_T)keys * sizeof(IDP_ENTRY));
    if (!snapshot) {
        RegCloseKey(root);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    for (DWORD index = 0;; index++) {
        WCHAR key_name[64];
        DWORD length = 64;
        GUID id;
        error = RegEnumKeyExW(root, index, key_name, &length, nullptr, nullptr, nullptr, nullptr);
        if (error == ERROR_NO_MORE_ITEMS)
            break;
        if (error || !guid_of(key_name, &id))
            continue;
        if (snapshot->Count >= keys)
            break;
        HKEY key;
        if (RegOpenKeyExW(root, key_name, 0, KEY_READ, &key))
            continue;
        IDP_ENTRY *entry = &snapshot->Entries[snapshot->Count];
        DWORD connected = 0;
        if (!read_value(key, L"Name", REG_SZ, TRUE, (BYTE *)entry->Name, IDP_NAME_CHARS * sizeof(WCHAR)) &&
            !read_value(key, L"Connected", REG_DWORD, FALSE, (BYTE *)&connected, sizeof(connected))) {
            entry->Magic = IDP_MAGIC;
            entry->Snapshot = snapshot;
            entry->Info.Size = sizeof(IDP_INFO);
            entry->Info.Name = entry->Name;
            entry->Info.Id = id;
            entry->Info.Flags = connected ? 3 : 2;
            snapshot->Count++;
        } else {
            memset(entry, 0, sizeof(*entry));
        }
        RegCloseKey(key);
    }
    /* the default is the named one if it is connected, else the last connected one, else the first. */
    WCHAR text[64];
    GUID wanted = {};
    BOOL named = !read_value(root, L"DefaultProvider", REG_SZ, TRUE, (BYTE *)text, sizeof(text)) && guid_of(text, &wanted);
    IDP_ENTRY *chosen = nullptr, *last_connected = nullptr;
    for (DWORD i = 0; i < snapshot->Count; i++) {
        IDP_ENTRY *entry = &snapshot->Entries[i];
        if (!(entry->Info.Flags & IDP_CONNECTED))
            continue;
        last_connected = entry;
        if (named && IsEqualGUID(&entry->Info.Id, &wanted)) {
            chosen = entry;
            break;
        }
    }
    if (!chosen)
        chosen = last_connected ? last_connected : (snapshot->Count ? &snapshot->Entries[0] : nullptr);
    if (chosen)
        chosen->Default = TRUE;
    RegCloseKey(root);
    *result = snapshot;
    return ERROR_SUCCESS;
}

/* an enumeration context is a pointer to an entry with the wanted flags in its two low bits. */
static IDP_ENTRY *entry_of(ULONG_PTR context)
{
    IDP_ENTRY *entry = (IDP_ENTRY *)(context & ~(ULONG_PTR)3);
    return entry && entry->Magic == IDP_MAGIC ? entry : nullptr;
}

ADVAPI DWORD WINAPI EnumerateIdentityProviders(ULONG_PTR *context, DWORD flags, GUID *id)
{
    if (!context || !id || (flags & 3) != flags)
        return ERROR_INVALID_PARAMETER;
    IDP_SNAPSHOT *snapshot;
    DWORD index;
    if (!*context) {
        DWORD error = load_snapshot(&snapshot);
        if (error)
            return error == ERROR_NOT_SUPPORTED ? ERROR_NO_MORE_ITEMS : error;
        index = 0;
    } else {
        IDP_ENTRY *current = entry_of(*context);
        if (!current || flags != (*context & 3))
            return ERROR_INVALID_PARAMETER;
        snapshot = current->Snapshot;
        index = (DWORD)(current - snapshot->Entries) + 1;
    }
    for (; index < snapshot->Count; index++) {
        IDP_ENTRY *entry = &snapshot->Entries[index];
        if ((flags & entry->Info.Flags) == flags) {
            *context = (ULONG_PTR)entry | flags;
            *id = entry->Info.Id;
            return ERROR_SUCCESS;
        }
    }
    if (!*context)
        LocalFree(snapshot);
    return ERROR_NO_MORE_ITEMS;
}

ADVAPI void WINAPI ReleaseIdentityProviderEnumContext(ULONG_PTR context)
{
    IDP_ENTRY *entry = entry_of(context);
    if (entry)
        LocalFree(entry->Snapshot);
}

ADVAPI DWORD WINAPI GetDefaultIdentityProvider(GUID *id)
{
    IDP_SNAPSHOT *snapshot;
    DWORD error = load_snapshot(&snapshot);
    if (error)
        return error;
    error = ERROR_NOT_FOUND;
    for (DWORD i = 0; i < snapshot->Count; i++) {
        if (snapshot->Entries[i].Default) {
            *id = snapshot->Entries[i].Info.Id;
            error = ERROR_SUCCESS;
            break;
        }
    }
    LocalFree(snapshot);
    return error;
}

static DWORD copy_info(const IDP_ENTRY *entry, PVOID buffer, DWORD *size)
{
    DWORD needed = info_size(entry);
    if (needed > *size) {
        *size = needed;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    IDP_INFO *info = buffer;
    *info = entry->Info;
    info->Name = (PWSTR)((BYTE *)buffer + sizeof(IDP_INFO));
    wcscpy(info->Name, entry->Name);
    return ERROR_SUCCESS;
}

ADVAPI DWORD WINAPI GetIdentityProviderInfoByGUID(const GUID *id, PVOID buffer, DWORD *size)
{
    if (!size || !id || (!buffer && *size))
        return ERROR_INVALID_PARAMETER;
    IDP_SNAPSHOT *snapshot;
    DWORD error = load_snapshot(&snapshot);
    if (error)
        return error;
    error = ERROR_NO_MATCH;
    for (DWORD i = 0; i < snapshot->Count; i++) {
        if (IsEqualGUID(&snapshot->Entries[i].Info.Id, id)) {
            error = copy_info(&snapshot->Entries[i], buffer, size);
            break;
        }
    }
    LocalFree(snapshot);
    return error;
}

ADVAPI DWORD WINAPI GetIdentityProviderInfoByName(PCWSTR name, PVOID buffer, DWORD *size)
{
    if (!size || !name || wcslen(name) > IDP_NAME_CHARS || (!buffer && *size))
        return ERROR_INVALID_PARAMETER;
    IDP_SNAPSHOT *snapshot;
    DWORD error = load_snapshot(&snapshot);
    if (error)
        return error;
    error = ERROR_NO_MATCH;
    for (DWORD i = 0; i < snapshot->Count; i++) {
        if (!_wcsicmp(snapshot->Entries[i].Name, name)) {
            error = copy_info(&snapshot->Entries[i], buffer, size);
            break;
        }
    }
    LocalFree(snapshot);
    return error;
}
