/*
 * modules: dll loading, handles, file names and export lookup.
 */
#include "kernelbase_private.h"

#define LDR_ADDREF_DLL_PIN 0x00000001

/* ------------------------------------------------------------------ loader list helpers */

typedef BOOL (*MODULE_VISITOR)(PLDR_DATA_TABLE_ENTRY entry, void *context);

static void visit_modules(MODULE_VISITOR visit, void *context)
{
    ULONG_PTR cookie = 0;
    LdrLockLoaderLock(0, nullptr, &cookie);
    PLIST_ENTRY head = &KbPeb()->Ldr->InLoadOrderModuleList;
    for (PLIST_ENTRY e = head->Flink; e != head; e = e->Flink) {
        PLDR_DATA_TABLE_ENTRY entry = CONTAINING_RECORD(e, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (!visit(entry, context))
            break;
    }
    LdrUnlockLoaderLock(0, cookie);
}

typedef struct NAME_QUERY {
    PVOID Base;
    PWSTR Buffer;
    DWORD Size;
    BOOL Base_name;
    DWORD Result;
    BOOL Found;
    BOOL Truncated;
} NAME_QUERY;

static BOOL copy_module_name(PLDR_DATA_TABLE_ENTRY entry, void *context)
{
    NAME_QUERY *q = context;
    if (entry->DllBase != q->Base)
        return TRUE;
    PCUNICODE_STRING name = q->Base_name ? &entry->BaseDllName : &entry->FullDllName;
    DWORD n = name->Length / sizeof(WCHAR);
    q->Found = TRUE;
    if (!q->Size) {
        q->Result = 0;
        q->Truncated = TRUE;
        return FALSE;
    }
    if (n >= q->Size) {
        memcpy(q->Buffer, name->Buffer, (q->Size - 1) * sizeof(WCHAR));
        q->Buffer[q->Size - 1] = 0;
        q->Result = q->Size;
        q->Truncated = TRUE;
    } else {
        memcpy(q->Buffer, name->Buffer, n * sizeof(WCHAR));
        q->Buffer[n] = 0;
        q->Result = n;
    }
    return FALSE;
}

static PVOID module_base(HMODULE module)
{
    return module ? (PVOID)module : KbPeb()->ImageBaseAddress;
}

static BOOL is_datafile(HMODULE module)
{
    return ((ULONG_PTR)module & 3) != 0;
}

static DWORD module_name(HMODULE module, LPWSTR buffer, DWORD size, BOOL base_name)
{
    NAME_QUERY q = {module_base(module), buffer, size, base_name, 0, FALSE, FALSE};
    if (is_datafile(module)) {
        KbSetError(ERROR_MOD_NOT_FOUND);
        return 0;
    }
    visit_modules(copy_module_name, &q);
    if (!q.Found) {
        KbSetError(ERROR_MOD_NOT_FOUND);
        return 0;
    }
    if (q.Truncated)
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
    return q.Result;
}

/* ------------------------------------------------------------------ handles */

HMODULE WINAPI GetModuleHandleW(LPCWSTR name)
{
    if (!name)
        return KbPeb()->ImageBaseAddress;
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, name);
    PVOID base;
    NTSTATUS status = LdrGetDllHandle(nullptr, nullptr, &us, &base);
    if (!NT_SUCCESS(status)) {
        KbSetError(ERROR_MOD_NOT_FOUND);
        return nullptr;
    }
    return base;
}

HMODULE WINAPI GetModuleHandleA(LPCSTR name)
{
    if (!name)
        return KbPeb()->ImageBaseAddress;
    PWSTR w = KbAnsiToWide(name);
    if (!w)
        return nullptr;
    HMODULE h = GetModuleHandleW(w);
    KbFree(w);
    return h;
}

BOOL WINAPI GetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE *module)
{
    if (!module) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *module = nullptr;
    if ((flags & ~(DWORD)(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT |
                          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)) ||
        ((flags & GET_MODULE_HANDLE_EX_FLAG_PIN) && (flags & GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT))) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PVOID base;
    if (flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) {
        PLDR_DATA_TABLE_ENTRY entry;
        if (!NT_SUCCESS(LdrFindEntryForAddress((PVOID)name, &entry))) {
            KbSetError(ERROR_MOD_NOT_FOUND);
            return FALSE;
        }
        base = entry->DllBase;
    } else if (!name) {
        base = KbPeb()->ImageBaseAddress;
    } else {
        base = GetModuleHandleW(name);
        if (!base)
            return FALSE;
    }
    if (flags & GET_MODULE_HANDLE_EX_FLAG_PIN)
        LdrAddRefDll(LDR_ADDREF_DLL_PIN, base);
    else if (!(flags & GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT))
        LdrAddRefDll(0, base);
    *module = base;
    return TRUE;
}

BOOL WINAPI GetModuleHandleExA(DWORD flags, LPCSTR name, HMODULE *module)
{
    if ((flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) || !name)
        return GetModuleHandleExW(flags, (LPCWSTR)name, module);
    PWSTR w = KbAnsiToWide(name);
    if (!w) {
        if (module)
            *module = nullptr;
        return FALSE;
    }
    BOOL ok = GetModuleHandleExW(flags, w, module);
    KbFree(w);
    return ok;
}

DWORD WINAPI GetModuleFileNameW(HMODULE module, LPWSTR buffer, DWORD size)
{
    return module_name(module, buffer, size, FALSE);
}

DWORD WINAPI GetModuleFileNameA(HMODULE module, LPSTR buffer, DWORD size)
{
    WCHAR w[MAX_PATH * 4];
    DWORD n = module_name(module, w, RTL_NUMBER_OF(w), FALSE);
    if (!n)
        return 0;
    if (!size) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    UINT cp = KbFileApiCodePage();
    int need = WideCharToMultiByte(cp, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if ((DWORD)need <= size) {
        WideCharToMultiByte(cp, 0, w, -1, buffer, (int)size, nullptr, nullptr);
        return (DWORD)need - 1;
    }
    /* truncate on a character boundary and terminate, like windows. */
    int written = 0;
    for (DWORD i = 0; w[i]; i++) {
        char tmp[8];
        int len = WideCharToMultiByte(cp, 0, &w[i], 1, tmp, sizeof(tmp), nullptr, nullptr);
        if ((DWORD)(written + len) > size - 1)
            break;
        memcpy(buffer + written, tmp, (SIZE_T)len);
        written += len;
    }
    buffer[written] = 0;
    KbSetError(ERROR_INSUFFICIENT_BUFFER);
    return size;
}

/* ------------------------------------------------------------------ dll directories */

typedef struct DLL_DIRECTORY {
    struct DLL_DIRECTORY *Next;
    WCHAR Path[1];
} DLL_DIRECTORY;

static DLL_DIRECTORY *g_user_dirs;
static PWSTR g_dll_directory;
static DWORD g_default_dll_flags;
static RTL_SRWLOCK g_dll_dir_lock;

DLL_DIRECTORY_COOKIE WINAPI AddDllDirectory(LPCWSTR directory)
{
    if (!directory || RtlDetermineDosPathNameType_U(directory) == RtlPathTypeRelative ||
        RtlDetermineDosPathNameType_U(directory) == RtlPathTypeDriveRelative) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    SIZE_T n = wcslen(directory);
    DLL_DIRECTORY *d = KbAlloc(offsetof(DLL_DIRECTORY, Path) + (n + 1) * sizeof(WCHAR));
    if (!d) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    memcpy(d->Path, directory, (n + 1) * sizeof(WCHAR));
    RtlAcquireSRWLockExclusive(&g_dll_dir_lock);
    d->Next = g_user_dirs;
    g_user_dirs = d;
    RtlReleaseSRWLockExclusive(&g_dll_dir_lock);
    return d;
}

BOOL WINAPI RemoveDllDirectory(DLL_DIRECTORY_COOKIE cookie)
{
    RtlAcquireSRWLockExclusive(&g_dll_dir_lock);
    for (DLL_DIRECTORY **p = &g_user_dirs; *p; p = &(*p)->Next) {
        if (*p == cookie) {
            DLL_DIRECTORY *d = *p;
            *p = d->Next;
            RtlReleaseSRWLockExclusive(&g_dll_dir_lock);
            KbFree(d);
            return TRUE;
        }
    }
    RtlReleaseSRWLockExclusive(&g_dll_dir_lock);
    KbSetError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

BOOL WINAPI SetDefaultDllDirectories(DWORD flags)
{
    DWORD valid = LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_USER_DIRS | LOAD_LIBRARY_SEARCH_SYSTEM32 |
                  LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
    if (!flags || (flags & ~valid)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    g_default_dll_flags = flags;
    return TRUE;
}

BOOL WINAPI SetDllDirectoryW(LPCWSTR directory)
{
    PWSTR copy = nullptr;
    if (directory) {
        SIZE_T n = wcslen(directory);
        copy = KbAlloc((n + 1) * sizeof(WCHAR));
        if (!copy) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        memcpy(copy, directory, (n + 1) * sizeof(WCHAR));
    }
    RtlAcquireSRWLockExclusive(&g_dll_dir_lock);
    PWSTR old = g_dll_directory;
    g_dll_directory = copy;
    RtlReleaseSRWLockExclusive(&g_dll_dir_lock);
    KbFree(old);
    return TRUE;
}

BOOL WINAPI SetDllDirectoryA(LPCSTR directory)
{
    PWSTR w = KbAnsiToWide(directory);
    if (directory && !w)
        return FALSE;
    BOOL ok = SetDllDirectoryW(w);
    KbFree(w);
    return ok;
}

DWORD WINAPI GetDllDirectoryW(DWORD size, LPWSTR buffer)
{
    RtlAcquireSRWLockShared(&g_dll_dir_lock);
    PCWSTR dir = g_dll_directory ? g_dll_directory : L"";
    DWORD n = (DWORD)wcslen(dir);
    DWORD result;
    if (n >= size || !buffer) {
        result = n + 1;
        if (buffer && size)
            buffer[0] = 0;
    } else {
        memcpy(buffer, dir, (n + 1) * sizeof(WCHAR));
        result = n;
    }
    RtlReleaseSRWLockShared(&g_dll_dir_lock);
    return result;
}

DWORD WINAPI GetDllDirectoryA(DWORD size, LPSTR buffer)
{
    WCHAR w[MAX_PATH * 2];
    DWORD n = GetDllDirectoryW(RTL_NUMBER_OF(w), w);
    if (n >= RTL_NUMBER_OF(w))
        return 0;
    return KbWideResultToAnsi(w, buffer, size);
}

static void append_dir(PWSTR list, SIZE_T *n, SIZE_T capacity, PCWSTR dir, SIZE_T len)
{
    if (!len || *n + len + 2 >= capacity)
        return;
    memcpy(list + *n, dir, len * sizeof(WCHAR));
    *n += len;
    list[(*n)++] = ';';
    list[*n] = 0;
}

/* builds the search path LdrLoadDll receives, or NULL to use the loader default. */
static PWSTR build_search_path(LPCWSTR name, DWORD flags)
{
    DWORD search = flags & (LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                            LOAD_LIBRARY_SEARCH_USER_DIRS | LOAD_LIBRARY_SEARCH_SYSTEM32 |
                            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    BOOL altered = (flags & LOAD_WITH_ALTERED_SEARCH_PATH) && wcschr(name, '\\');
    if (!search && !(flags & LOAD_WITH_ALTERED_SEARCH_PATH))
        search = g_default_dll_flags;
    if (!search && !altered && !g_dll_directory && !g_user_dirs)
        return nullptr;
    if (search & LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)
        search |= LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_USER_DIRS | LOAD_LIBRARY_SEARCH_SYSTEM32;
    SIZE_T capacity = 8192;
    PWSTR list = KbAlloc(capacity * sizeof(WCHAR));
    if (!list)
        return nullptr;
    SIZE_T n = 0;
    list[0] = 0;
    PRTL_USER_PROCESS_PARAMETERS pp = KbParams();
    SIZE_T app = pp->ImagePathName.Length / sizeof(WCHAR);
    while (app && pp->ImagePathName.Buffer[app - 1] != '\\')
        app--;
    if (altered || (search & LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR)) {
        PCWSTR end = name + wcslen(name);
        while (end > name && end[-1] != '\\' && end[-1] != '/')
            end--;
        append_dir(list, &n, capacity, name, (SIZE_T)(end - name));
    }
    if (!search || (search & LOAD_LIBRARY_SEARCH_APPLICATION_DIR))
        append_dir(list, &n, capacity, pp->ImagePathName.Buffer, app);
    RtlAcquireSRWLockShared(&g_dll_dir_lock);
    if (!search || (search & LOAD_LIBRARY_SEARCH_USER_DIRS)) {
        for (DLL_DIRECTORY *d = g_user_dirs; d; d = d->Next)
            append_dir(list, &n, capacity, d->Path, wcslen(d->Path));
        if (g_dll_directory)
            append_dir(list, &n, capacity, g_dll_directory, wcslen(g_dll_directory));
    }
    RtlReleaseSRWLockShared(&g_dll_dir_lock);
    if (search & LOAD_LIBRARY_SEARCH_SYSTEM32) {
        WCHAR sys[MAX_PATH + 16];
        UINT len = GetSystemDirectoryW(sys, RTL_NUMBER_OF(sys));
        append_dir(list, &n, capacity, sys, len);
    }
    if (!n) {
        KbFree(list);
        return nullptr;
    }
    return list;
}

/* ------------------------------------------------------------------ loading */

static void set_load_error(NTSTATUS status)
{
    switch (status) {
    case STATUS_DLL_NOT_FOUND:
    case STATUS_OBJECT_NAME_NOT_FOUND:
    case STATUS_OBJECT_PATH_NOT_FOUND:
    case STATUS_OBJECT_PATH_SYNTAX_BAD:
    case STATUS_NO_SUCH_FILE:
        KbSetError(ERROR_MOD_NOT_FOUND);
        break;
    case STATUS_INVALID_IMAGE_FORMAT:
    case STATUS_INVALID_IMAGE_NOT_MZ:
    case STATUS_SECTION_NOT_IMAGE:
        KbSetError(ERROR_BAD_EXE_FORMAT);
        break;
    case STATUS_ENTRYPOINT_NOT_FOUND:
    case STATUS_PROCEDURE_NOT_FOUND:
        KbSetError(ERROR_PROC_NOT_FOUND);
        break;
    case STATUS_ORDINAL_NOT_FOUND:
        KbSetError(ERROR_INVALID_ORDINAL);
        break;
    default:
        KbSetStatus(status);
        break;
    }
}

static HMODULE load_datafile(LPCWSTR name, DWORD flags)
{
    if (!(flags & LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE)) {
        HMODULE loaded;
        if (GetModuleHandleExW(0, name, &loaded))
            return loaded;
    }
    WCHAR path[MAX_PATH * 2];
    if (!SearchPathW(nullptr, name, wcschr(name, '.') ? nullptr : L".dll", RTL_NUMBER_OF(path), path, nullptr)) {
        /* a name with a path is a file that is not there, a bare name a module that is not found. */
        KbSetError(wcschr(name, '\\') || wcschr(name, '/') ? ERROR_FILE_NOT_FOUND : ERROR_MOD_NOT_FOUND);
        return nullptr;
    }
    HANDLE file = CreateFileW(path, GENERIC_READ,
                              (flags & LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE) ? 0 : FILE_SHARE_READ | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return nullptr;
    HANDLE section;
    NTSTATUS status =
        NtCreateSection(&section, SECTION_MAP_READ | SECTION_QUERY, nullptr, nullptr, PAGE_READONLY, SEC_COMMIT, file);
    NtClose(file);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return nullptr;
    }
    PVOID base = nullptr;
    SIZE_T size = 0;
    status = NtMapViewOfSection(section, NtCurrentProcess(), &base, 0, 0, nullptr, &size, ViewShare, 0, PAGE_READONLY);
    NtClose(section);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return nullptr;
    }
    if (!KbImageNtHeader(base)) {
        NtUnmapViewOfSection(NtCurrentProcess(), base);
        KbSetError(ERROR_BAD_EXE_FORMAT);
        return nullptr;
    }
    /* ntdll's resource functions read handles with a low bit set as raw file mappings. */
    return (HMODULE)((ULONG_PTR)base | 1);
}

HMODULE WINAPI LoadLibraryExW(LPCWSTR name, HANDLE file, DWORD flags)
{
    if (!name || file) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (!*name) {
        KbSetError(ERROR_MOD_NOT_FOUND);
        return nullptr;
    }
    if (flags & (LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE))
        return load_datafile(name, flags);
    PWSTR search = build_search_path(name, flags);
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, name);
    ULONG ldr_flags = (flags & DONT_RESOLVE_DLL_REFERENCES) ? 2 : 0;
    PVOID base = nullptr;
    NTSTATUS status = LdrLoadDll(search, &ldr_flags, &us, &base);
    KbFree(search);
    if (!NT_SUCCESS(status)) {
        set_load_error(status);
        return nullptr;
    }
    return base;
}

HMODULE WINAPI LoadLibraryW(LPCWSTR name)
{
    return LoadLibraryExW(name, nullptr, 0);
}

HMODULE WINAPI LoadLibraryExA(LPCSTR name, HANDLE file, DWORD flags)
{
    if (!name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    PWSTR w = KbAnsiToWide(name);
    if (!w)
        return nullptr;
    HMODULE h = LoadLibraryExW(w, file, flags);
    KbFree(w);
    return h;
}

HMODULE WINAPI LoadLibraryA(LPCSTR name)
{
    return LoadLibraryExA(name, nullptr, 0);
}

HMODULE WINAPI LoadPackagedLibrary(LPCWSTR name, DWORD reserved)
{
    KbSetError(APPMODEL_ERROR_NO_PACKAGE);
    return nullptr;
}

BOOL WINAPI FreeLibrary(HMODULE module)
{
    if (!module) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    NTSTATUS status;
    if (is_datafile(module))
        status = NtUnmapViewOfSection(NtCurrentProcess(), (PVOID)((ULONG_PTR)module & ~(ULONG_PTR)3));
    else
        status = LdrUnloadDll(module);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

void WINAPI FreeLibraryAndExitThread(HMODULE module, DWORD code)
{
    FreeLibrary(module);
    ExitThread(code);
}

FARPROC WINAPI GetProcAddress(HMODULE module, LPCSTR name)
{
    PVOID base = module_base(module);
    if (is_datafile(module)) {
        KbSetError(ERROR_PROC_NOT_FOUND);
        return nullptr;
    }
    PVOID address = nullptr;
    NTSTATUS status;
    if (IS_INTRESOURCE(name)) {
        status = LdrGetProcedureAddress(base, nullptr, (ULONG)(ULONG_PTR)name, &address);
    } else {
        ANSI_STRING as;
        RtlInitAnsiString(&as, name);
        status = LdrGetProcedureAddress(base, &as, 0, &address);
    }
    if (!NT_SUCCESS(status)) {
        KbSetError(status == STATUS_DLL_NOT_FOUND ? ERROR_MOD_NOT_FOUND : ERROR_PROC_NOT_FOUND);
        return nullptr;
    }
    return (FARPROC)address;
}

BOOL WINAPI DisableThreadLibraryCalls(HMODULE module)
{
    NTSTATUS status = LdrDisableThreadCalloutsForDll(module);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

/* ------------------------------------------------------------------ process status api */

static BOOL is_current_process(HANDLE process)
{
    if (process == NtCurrentProcess())
        return TRUE;
    PROCESS_BASIC_INFORMATION info;
    if (!NT_SUCCESS(NtQueryInformationProcess(process, ProcessBasicInformation, &info, sizeof(info), nullptr)))
        return FALSE;
    return info.UniqueProcessId == GetCurrentProcessId();
}

typedef struct ENUM_QUERY {
    HMODULE *Modules;
    DWORD Capacity;
    DWORD Count;
} ENUM_QUERY;

static BOOL collect_module(PLDR_DATA_TABLE_ENTRY entry, void *context)
{
    ENUM_QUERY *q = context;
    if (q->Count < q->Capacity)
        q->Modules[q->Count] = entry->DllBase;
    q->Count++;
    return TRUE;
}

BOOL WINAPI K32EnumProcessModulesEx(HANDLE process, HMODULE *modules, DWORD size, LPDWORD needed, DWORD filter)
{
    if (!is_current_process(process)) {
        KbSetError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    ENUM_QUERY q = {modules, size / sizeof(HMODULE), 0};
    visit_modules(collect_module, &q);
    if (needed)
        *needed = q.Count * sizeof(HMODULE);
    return TRUE;
}

BOOL WINAPI K32EnumProcessModules(HANDLE process, HMODULE *modules, DWORD size, LPDWORD needed)
{
    return K32EnumProcessModulesEx(process, modules, size, needed, 3);
}

typedef struct INFO_QUERY {
    PVOID Base;
    LPMODULEINFO Info;
    BOOL Found;
} INFO_QUERY;

static BOOL module_info(PLDR_DATA_TABLE_ENTRY entry, void *context)
{
    INFO_QUERY *q = context;
    if (entry->DllBase != q->Base)
        return TRUE;
    q->Info->lpBaseOfDll = entry->DllBase;
    q->Info->SizeOfImage = entry->SizeOfImage;
    q->Info->EntryPoint = entry->EntryPoint;
    q->Found = TRUE;
    return FALSE;
}

BOOL WINAPI K32GetModuleInformation(HANDLE process, HMODULE module, LPMODULEINFO info, DWORD size)
{
    if (size < sizeof(MODULEINFO) || !info) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (!is_current_process(process)) {
        KbSetError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    INFO_QUERY q = {module_base(module), info, FALSE};
    visit_modules(module_info, &q);
    if (!q.Found) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

DWORD WINAPI K32GetModuleBaseNameW(HANDLE process, HMODULE module, LPWSTR name, DWORD size)
{
    if (!is_current_process(process)) {
        KbSetError(ERROR_ACCESS_DENIED);
        return 0;
    }
    DWORD n = module_name(module, name, size, TRUE);
    return n == size && GetLastError() == ERROR_INSUFFICIENT_BUFFER ? size - 1 : n;
}

DWORD WINAPI K32GetModuleBaseNameA(HANDLE process, HMODULE module, LPSTR name, DWORD size)
{
    WCHAR w[MAX_PATH];
    DWORD n = K32GetModuleBaseNameW(process, module, w, RTL_NUMBER_OF(w));
    if (!n || !size)
        return 0;
    int got = WideCharToMultiByte(CP_ACP, 0, w, -1, name, (int)size, nullptr, nullptr);
    if (!got) {
        name[size - 1] = 0;
        return size - 1;
    }
    return (DWORD)got - 1;
}

DWORD WINAPI K32GetModuleFileNameExW(HANDLE process, HMODULE module, LPWSTR name, DWORD size)
{
    if (!is_current_process(process)) {
        KbSetError(ERROR_ACCESS_DENIED);
        return 0;
    }
    DWORD n = module_name(module, name, size, FALSE);
    return n == size && GetLastError() == ERROR_INSUFFICIENT_BUFFER ? size - 1 : n;
}

DWORD WINAPI K32GetModuleFileNameExA(HANDLE process, HMODULE module, LPSTR name, DWORD size)
{
    WCHAR w[MAX_PATH * 2];
    DWORD n = K32GetModuleFileNameExW(process, module, w, RTL_NUMBER_OF(w));
    if (!n || !size)
        return 0;
    int got = WideCharToMultiByte(CP_ACP, 0, w, -1, name, (int)size, nullptr, nullptr);
    if (!got) {
        name[size - 1] = 0;
        return size - 1;
    }
    return (DWORD)got - 1;
}

static BOOL process_image_name(HANDLE process, PWSTR out, DWORD *chars)
{
    UCHAR buf[sizeof(UNICODE_STRING) + MAX_PATH * 4 * sizeof(WCHAR)];
    NTSTATUS status = NtQueryInformationProcess(process, ProcessImageFileNameWin32, buf, sizeof(buf), nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    UNICODE_STRING *us = (UNICODE_STRING *)buf;
    DWORD n = us->Length / sizeof(WCHAR);
    if (n >= *chars) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(out, us->Buffer, n * sizeof(WCHAR));
    out[n] = 0;
    *chars = n;
    return TRUE;
}

DWORD WINAPI K32GetProcessImageFileNameW(HANDLE process, LPWSTR name, DWORD size)
{
    DWORD chars = size;
    if (!process_image_name(process, name, &chars))
        return 0;
    return chars;
}

/* the name in the active code page, without best fit characters. The terminator is not counted. */
DWORD WINAPI K32GetProcessImageFileNameA(HANDLE process, LPSTR name, DWORD size)
{
    WCHAR wide[MAX_PATH * 4];
    DWORD chars = RTL_NUMBER_OF(wide);
    if (!process_image_name(process, wide, &chars))
        return 0;
    DWORD got = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide, (int)chars, name, (int)size, nullptr, nullptr);
    if (got && got < size)
        name[got] = 0;
    return got;
}

BOOL WINAPI QueryFullProcessImageNameW(HANDLE process, DWORD flags, LPWSTR name, PDWORD size)
{
    if (!name || !size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return process_image_name(process, name, size);
}

BOOL WINAPI QueryFullProcessImageNameA(HANDLE process, DWORD flags, LPSTR name, PDWORD size)
{
    if (!name || !size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR w[MAX_PATH * 4];
    DWORD chars = RTL_NUMBER_OF(w);
    if (!process_image_name(process, w, &chars))
        return FALSE;
    int need = WideCharToMultiByte(CP_ACP, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if ((DWORD)need > *size) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    WideCharToMultiByte(CP_ACP, 0, w, -1, name, (int)*size, nullptr, nullptr);
    *size = (DWORD)need - 1;
    return TRUE;
}
