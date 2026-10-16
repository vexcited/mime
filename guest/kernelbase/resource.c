/*
 * resources: lookup, loading and enumeration of module resources.
 */
#include "kernelbase_private.h"

static void set_resource_error(NTSTATUS status)
{
    switch (status) {
    case STATUS_RESOURCE_TYPE_NOT_FOUND:
        KbSetError(ERROR_RESOURCE_TYPE_NOT_FOUND);
        break;
    case STATUS_RESOURCE_NAME_NOT_FOUND:
        KbSetError(ERROR_RESOURCE_NAME_NOT_FOUND);
        break;
    case STATUS_RESOURCE_LANG_NOT_FOUND:
        KbSetError(ERROR_RESOURCE_LANG_NOT_FOUND);
        break;
    case STATUS_RESOURCE_DATA_NOT_FOUND:
        KbSetError(ERROR_RESOURCE_DATA_NOT_FOUND);
        break;
    default:
        KbSetStatus(status);
        break;
    }
}

static HMODULE resolve_module(HMODULE module)
{
    return module ? module : (HMODULE)KbPeb()->ImageBaseAddress;
}

/* converts an ansi resource name, keeping integer ids as they are. */
static LPWSTR name_to_wide(LPCSTR name, BOOL *allocated)
{
    *allocated = FALSE;
    if (IS_INTRESOURCE(name))
        return (LPWSTR)name;
    LPWSTR w = KbAnsiToWideCp(CP_ACP, name);
    if (w)
        *allocated = TRUE;
    return w;
}

HRSRC WINAPI FindResourceExW(HMODULE module, LPCWSTR type, LPCWSTR name, WORD language)
{
    if (!type || !name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    LDR_RESOURCE_INFO info = {(ULONG_PTR)type, (ULONG_PTR)name, language};
    IMAGE_RESOURCE_DATA_ENTRY *entry = nullptr;
    NTSTATUS status = LdrFindResource_U(resolve_module(module), &info, 3, &entry);
    if (!NT_SUCCESS(status)) {
        set_resource_error(status);
        return nullptr;
    }
    return (HRSRC)entry;
}

HRSRC WINAPI FindResourceW(HMODULE module, LPCWSTR name, LPCWSTR type)
{
    return FindResourceExW(module, type, name, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
}

HRSRC WINAPI FindResourceExA(HMODULE module, LPCSTR type, LPCSTR name, WORD language)
{
    BOOL free_type, free_name;
    LPWSTR wtype = name_to_wide(type, &free_type);
    LPWSTR wname = name_to_wide(name, &free_name);
    HRSRC r = nullptr;
    if (wtype && wname)
        r = FindResourceExW(module, wtype, wname, language);
    else
        KbSetError(ERROR_INVALID_PARAMETER);
    if (free_type)
        KbFree(wtype);
    if (free_name)
        KbFree(wname);
    return r;
}

HRSRC WINAPI FindResourceA(HMODULE module, LPCSTR name, LPCSTR type)
{
    return FindResourceExA(module, type, name, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
}

HGLOBAL WINAPI LoadResource(HMODULE module, HRSRC resource)
{
    if (!resource) {
        KbSetError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    PVOID data = nullptr;
    NTSTATUS status = LdrAccessResource(resolve_module(module), (IMAGE_RESOURCE_DATA_ENTRY *)resource, &data, nullptr);
    if (!NT_SUCCESS(status)) {
        set_resource_error(status);
        return nullptr;
    }
    return data;
}

LPVOID WINAPI LockResource(HGLOBAL data)
{
    return data;
}

DWORD WINAPI SizeofResource(HMODULE module, HRSRC resource)
{
    if (!resource) {
        KbSetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    return ((IMAGE_RESOURCE_DATA_ENTRY *)resource)->Size;
}

BOOL WINAPI FreeResource(HGLOBAL data)
{
    return FALSE;
}

/* ------------------------------------------------------------------ enumeration */

/* the name of a directory entry: an integer id, or a terminated copy of its string. */
static LPWSTR entry_name(const IMAGE_RESOURCE_DIRECTORY *root, const IMAGE_RESOURCE_DIRECTORY_ENTRY *e, PWSTR buffer,
                         SIZE_T chars)
{
    if (!(e->Name & 0x80000000))
        return MAKEINTRESOURCEW(e->Name & 0xFFFF);
    const IMAGE_RESOURCE_DIR_STRING_U *s = (const IMAGE_RESOURCE_DIR_STRING_U *)((PUCHAR)root + (e->Name & 0x7FFFFFFF));
    SIZE_T n = s->Length < chars ? s->Length : chars - 1;
    memcpy(buffer, s->NameString, n * sizeof(WCHAR));
    buffer[n] = 0;
    return buffer;
}

static IMAGE_RESOURCE_DIRECTORY *find_directory(HMODULE module, LPCWSTR type, LPCWSTR name, ULONG level,
                                                IMAGE_RESOURCE_DIRECTORY **root)
{
    LDR_RESOURCE_INFO info = {(ULONG_PTR)type, (ULONG_PTR)name, 0};
    NTSTATUS status = LdrFindResourceDirectory_U(module, &info, 0, root);
    if (!NT_SUCCESS(status)) {
        set_resource_error(status == STATUS_RESOURCE_DATA_NOT_FOUND ? STATUS_RESOURCE_DATA_NOT_FOUND : status);
        return nullptr;
    }
    if (!level)
        return *root;
    IMAGE_RESOURCE_DIRECTORY *dir = nullptr;
    status = LdrFindResourceDirectory_U(module, &info, level, &dir);
    if (!NT_SUCCESS(status)) {
        set_resource_error(status);
        return nullptr;
    }
    return dir;
}

BOOL WINAPI EnumResourceTypesExW(HMODULE module, ENUMRESTYPEPROCW proc, LONG_PTR param, DWORD flags, LANGID lang)
{
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    module = resolve_module(module);
    IMAGE_RESOURCE_DIRECTORY *root;
    IMAGE_RESOURCE_DIRECTORY *dir = find_directory(module, nullptr, nullptr, 0, &root);
    if (!dir)
        return FALSE;
    IMAGE_RESOURCE_DIRECTORY_ENTRY *e = (IMAGE_RESOURCE_DIRECTORY_ENTRY *)(dir + 1);
    ULONG count = dir->NumberOfNamedEntries + dir->NumberOfIdEntries;
    for (ULONG i = 0; i < count; i++) {
        WCHAR buffer[256];
        if (!proc(module, entry_name(root, &e[i], buffer, RTL_NUMBER_OF(buffer)), param)) {
            KbSetError(ERROR_RESOURCE_ENUM_USER_STOP);
            return FALSE;
        }
    }
    return TRUE;
}

BOOL WINAPI EnumResourceTypesW(HMODULE module, ENUMRESTYPEPROCW proc, LONG_PTR param)
{
    return EnumResourceTypesExW(module, proc, param, 0, 0);
}

BOOL WINAPI EnumResourceNamesExW(HMODULE module, LPCWSTR type, ENUMRESNAMEPROCW proc, LONG_PTR param, DWORD flags,
                                 LANGID lang)
{
    if (!proc || !type) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    module = resolve_module(module);
    IMAGE_RESOURCE_DIRECTORY *root;
    IMAGE_RESOURCE_DIRECTORY *dir = find_directory(module, type, nullptr, 1, &root);
    if (!dir)
        return FALSE;
    IMAGE_RESOURCE_DIRECTORY_ENTRY *e = (IMAGE_RESOURCE_DIRECTORY_ENTRY *)(dir + 1);
    ULONG count = dir->NumberOfNamedEntries + dir->NumberOfIdEntries;
    for (ULONG i = 0; i < count; i++) {
        WCHAR buffer[256];
        if (!proc(module, type, entry_name(root, &e[i], buffer, RTL_NUMBER_OF(buffer)), param)) {
            KbSetError(ERROR_RESOURCE_ENUM_USER_STOP);
            return FALSE;
        }
    }
    return TRUE;
}

BOOL WINAPI EnumResourceNamesW(HMODULE module, LPCWSTR type, ENUMRESNAMEPROCW proc, LONG_PTR param)
{
    return EnumResourceNamesExW(module, type, proc, param, 0, 0);
}

BOOL WINAPI EnumResourceLanguagesExW(HMODULE module, LPCWSTR type, LPCWSTR name, ENUMRESLANGPROCW proc, LONG_PTR param,
                                     DWORD flags, LANGID lang)
{
    if (!proc || !type || !name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    module = resolve_module(module);
    IMAGE_RESOURCE_DIRECTORY *root;
    IMAGE_RESOURCE_DIRECTORY *dir = find_directory(module, type, name, 2, &root);
    if (!dir)
        return FALSE;
    IMAGE_RESOURCE_DIRECTORY_ENTRY *e = (IMAGE_RESOURCE_DIRECTORY_ENTRY *)(dir + 1);
    ULONG count = dir->NumberOfNamedEntries + dir->NumberOfIdEntries;
    for (ULONG i = 0; i < count; i++) {
        if (!proc(module, type, name, (WORD)(e[i].Name & 0xFFFF), param)) {
            KbSetError(ERROR_RESOURCE_ENUM_USER_STOP);
            return FALSE;
        }
    }
    return TRUE;
}

BOOL WINAPI EnumResourceLanguagesW(HMODULE module, LPCWSTR type, LPCWSTR name, ENUMRESLANGPROCW proc, LONG_PTR param)
{
    return EnumResourceLanguagesExW(module, type, name, proc, param, 0, 0);
}

/* ansi enumeration: the callbacks receive converted names. */
typedef struct ANSI_ENUM {
    LONG_PTR Param;
    union {
        ENUMRESTYPEPROCA Types;
        ENUMRESNAMEPROCA Names;
        ENUMRESLANGPROCA Languages;
    };
    LPCSTR Type;
    LPCSTR Name;
} ANSI_ENUM;

static LPSTR wide_name_to_ansi(LPCWSTR name, PSTR buffer, int size)
{
    if (IS_INTRESOURCE(name))
        return (LPSTR)name;
    if (!WideCharToMultiByte(CP_ACP, 0, name, -1, buffer, size, nullptr, nullptr))
        buffer[size - 1] = 0;
    return buffer;
}

static BOOL CALLBACK ansi_type_proc(HMODULE module, LPWSTR type, LONG_PTR param)
{
    ANSI_ENUM *e = (ANSI_ENUM *)param;
    char t[512];
    return e->Types(module, wide_name_to_ansi(type, t, sizeof(t)), e->Param);
}

static BOOL CALLBACK ansi_name_proc(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR param)
{
    ANSI_ENUM *e = (ANSI_ENUM *)param;
    char n[512];
    return e->Names(module, e->Type, wide_name_to_ansi(name, n, sizeof(n)), e->Param);
}

static BOOL CALLBACK ansi_language_proc(HMODULE module, LPCWSTR type, LPCWSTR name, WORD language, LONG_PTR param)
{
    ANSI_ENUM *e = (ANSI_ENUM *)param;
    return e->Languages(module, e->Type, e->Name, language, e->Param);
}

BOOL WINAPI EnumResourceTypesA(HMODULE module, ENUMRESTYPEPROCA proc, LONG_PTR param)
{
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ANSI_ENUM e = {param};
    e.Types = proc;
    return EnumResourceTypesW(module, ansi_type_proc, (LONG_PTR)&e);
}

BOOL WINAPI EnumResourceNamesA(HMODULE module, LPCSTR type, ENUMRESNAMEPROCA proc, LONG_PTR param)
{
    if (!proc || !type) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL free_type;
    LPWSTR wtype = name_to_wide(type, &free_type);
    if (!wtype)
        return FALSE;
    ANSI_ENUM e = {param};
    e.Names = proc;
    e.Type = type;
    BOOL ok = EnumResourceNamesW(module, wtype, ansi_name_proc, (LONG_PTR)&e);
    if (free_type)
        KbFree(wtype);
    return ok;
}

BOOL WINAPI EnumResourceLanguagesA(HMODULE module, LPCSTR type, LPCSTR name, ENUMRESLANGPROCA proc, LONG_PTR param)
{
    if (!proc || !type || !name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL free_type, free_name;
    LPWSTR wtype = name_to_wide(type, &free_type);
    LPWSTR wname = name_to_wide(name, &free_name);
    BOOL ok = FALSE;
    if (wtype && wname) {
        ANSI_ENUM e = {param};
        e.Languages = proc;
        e.Type = type;
        e.Name = name;
        ok = EnumResourceLanguagesW(module, wtype, wname, ansi_language_proc, (LONG_PTR)&e);
    }
    if (free_type)
        KbFree(wtype);
    if (free_name)
        KbFree(wname);
    return ok;
}

/* ------------------------------------------------------------------ updating */

HANDLE WINAPI BeginUpdateResourceW(LPCWSTR file, BOOL delete_existing)
{
    KbSetError(ERROR_CALL_NOT_IMPLEMENTED);
    return nullptr;
}

BOOL WINAPI UpdateResourceW(HANDLE update, LPCWSTR type, LPCWSTR name, WORD language, LPVOID data, DWORD size)
{
    KbSetError(ERROR_INVALID_HANDLE);
    return FALSE;
}

BOOL WINAPI EndUpdateResourceW(HANDLE update, BOOL discard)
{
    KbSetError(ERROR_INVALID_HANDLE);
    return FALSE;
}

/* ------------------------------------------------------------------ message tables */

typedef struct MESSAGE_RESOURCE_BLOCK {
    ULONG LowId;
    ULONG HighId;
    ULONG OffsetToEntries;
} MESSAGE_RESOURCE_BLOCK;

typedef struct MESSAGE_RESOURCE_DATA {
    ULONG NumberOfBlocks;
    MESSAGE_RESOURCE_BLOCK Blocks[1];
} MESSAGE_RESOURCE_DATA;

typedef struct MESSAGE_RESOURCE_ENTRY {
    USHORT Length;
    USHORT Flags;
    UCHAR Text[1];
} MESSAGE_RESOURCE_ENTRY;

#define MESSAGE_RESOURCE_UNICODE 0x0001

/* finds a message in a module's message table; the text is wide, allocated when converted. */
BOOL KbFindMessage(HMODULE module, DWORD id, DWORD lang, LPWSTR *text, BOOL *allocated)
{
    *allocated = FALSE;
    LDR_RESOURCE_INFO info = {11 /* RT_MESSAGETABLE. */, 1, lang};
    IMAGE_RESOURCE_DATA_ENTRY *entry;
    NTSTATUS status = LdrFindResource_U(module, &info, 3, &entry);
    if (!NT_SUCCESS(status)) {
        set_resource_error(status);
        return FALSE;
    }
    PVOID data;
    ULONG size;
    status = LdrAccessResource(module, entry, &data, &size);
    if (!NT_SUCCESS(status)) {
        set_resource_error(status);
        return FALSE;
    }
    const MESSAGE_RESOURCE_DATA *table = data;
    for (ULONG b = 0; b < table->NumberOfBlocks; b++) {
        const MESSAGE_RESOURCE_BLOCK *block = &table->Blocks[b];
        if (id < block->LowId || id > block->HighId)
            continue;
        const MESSAGE_RESOURCE_ENTRY *m = (const MESSAGE_RESOURCE_ENTRY *)((PUCHAR)data + block->OffsetToEntries);
        for (ULONG i = block->LowId; i < id; i++)
            m = (const MESSAGE_RESOURCE_ENTRY *)((PUCHAR)m + m->Length);
        if (m->Flags & MESSAGE_RESOURCE_UNICODE) {
            *text = (LPWSTR)m->Text;
            return TRUE;
        }
        int n = MultiByteToWideChar(CP_ACP, 0, (LPCCH)m->Text, -1, nullptr, 0);
        LPWSTR w = n > 0 ? KbAlloc((SIZE_T)n * sizeof(WCHAR)) : nullptr;
        if (!w) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        MultiByteToWideChar(CP_ACP, 0, (LPCCH)m->Text, -1, w, n);
        *text = w;
        *allocated = TRUE;
        return TRUE;
    }
    KbSetError(ERROR_MR_MID_NOT_FOUND);
    return FALSE;
}
