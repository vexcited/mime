/*
 * object security by name and by handle: GetNamedSecurityInfo, SetSecurityInfo and friends. windows does this in
 * ntmarta.dll, loaded through advapi32's access provider interface; here it is one file. an object is opened with
 * the access its parts need, and its descriptor read or set through the kernel. for files and keys, a list change is
 * also propagated down, which windows does here and not in the kernel: every child that does not protect its lists
 * gets them rebuilt from its parent's new ones.
 */
#include "advapi32_private.h"

#ifndef ACCESS_SYSTEM_SECURITY
#define ACCESS_SYSTEM_SECURITY 0x01000000L
#endif
#define READ_CONTROL_ 0x00020000L
#define WRITE_DAC_ 0x00040000L
#define WRITE_OWNER_ 0x00080000L

#define SACL_PARTS 0x1F8 /* sacl, label, attribute, scope, trust label and access filter. */
#define LIST_PARTS 0x1FC /* the above plus the dacl. */

/* new lists use auto inheritance and skip the check of the caller's rights. */
#define INHERIT_FLAGS 0x101B

#define FILE_OPEN_FOR_BACKUP_INTENT_ 0x00004000
#define FILE_OPEN_REPARSE_POINT_ 0x00200000
#define FILE_ATTRIBUTE_DIRECTORY_ 0x10
#define FILE_ATTRIBUTE_RECALL_ON_OPEN_ 0x40000
WINBASEAPI HANDLE WINAPI OpenJobObjectW(DWORD access, BOOL inherit, LPCWSTR name);

#define ERROR_INSUFFICIENT_BUFFER_ 122L
#define ERROR_INVALID_NAME_ 123L
#define ERROR_NO_MORE_ITEMS_ 259L
#define STATUS_INVALID_PARAMETER_ ((NTSTATUS)0xC000000DL)
#define STATUS_NO_MORE_FILES_ ((NTSTATUS)0x80000006L)

typedef enum { READ_ACCESS, WRITE_ACCESS, MODIFY_ACCESS } OPEN_TYPE;

typedef struct OBJ {
    SE_OBJECT_TYPE Type;
    HANDLE Handle;
    BOOL Owned;  /* closed with the object. */
    BOOL Window; /* a window station, not a kernel handle. */
    PWSTR Name;  /* dos path of a file or registry path of a key, for parents and children. */
} OBJ;

/* a descriptor the rtl made, in its own process heap block. */
typedef PVOID MADE;

static void made_free(MADE sd)
{
    if (sd)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, sd);
}

/* generic mappings of the kinds lists propagate to, as windows has them. */
static const GENERIC_MAPPING g_file_mapping = {0x120089, 0x120116, 0x1200A0, 0x1F01FF};
static const GENERIC_MAPPING g_key_mapping = {0x20019, 0x20006, 0x20039, 0xF003F};

static BOOL is_registry(SE_OBJECT_TYPE type)
{
    return type == SE_REGISTRY_KEY || type == SE_REGISTRY_WOW64_32KEY || type == SE_REGISTRY_WOW64_64KEY;
}

/* kinds whose lists the client propagates to their children. */
static BOOL propagates(SE_OBJECT_TYPE type)
{
    return type == SE_FILE_OBJECT || is_registry(type);
}

static const GENERIC_MAPPING *mapping_of(SE_OBJECT_TYPE type)
{
    return is_registry(type) ? &g_key_mapping : &g_file_mapping;
}

/* ---------------------------------------------------------------- access an operation needs */

/*
 * the handle access the parts need for reading, writing or both. with extras, files and keys also get what lists
 * children and reads a file's attributes. the sacl parts add the system access right.
 */
static DWORD desired_access(SE_OBJECT_TYPE type, OPEN_TYPE open, BOOL extras, DWORD info)
{
    static const DWORD owner_rights[3] = {READ_CONTROL_, WRITE_OWNER_, READ_CONTROL_ | WRITE_OWNER_};
    static const DWORD dacl_rights[3] = {READ_CONTROL_, WRITE_DAC_, READ_CONTROL_ | WRITE_DAC_};
    static const DWORD scope_rights[3] = {READ_CONTROL_, ACCESS_SYSTEM_SECURITY,
                                          READ_CONTROL_ | ACCESS_SYSTEM_SECURITY};
    DWORD mask = 0;
    if (info & (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION))
        mask |= owner_rights[open];
    if (info & (DACL_SECURITY_INFORMATION | ATTRIBUTE_SECURITY_INFORMATION | PROCESS_TRUST_LABEL_SECURITY_INFORMATION |
                ACCESS_FILTER_SECURITY_INFORMATION))
        mask |= dacl_rights[open];
    if (info & SCOPE_SECURITY_INFORMATION)
        mask |= scope_rights[open];
    DWORD system = propagates(type) ? ACCESS_SYSTEM_SECURITY | READ_CONTROL_ : ACCESS_SYSTEM_SECURITY;
    if (info & SACL_SECURITY_INFORMATION)
        mask |= system;
    if ((info & SACL_PARTS) && (info & (PROTECTED_SACL_SECURITY_INFORMATION | UNPROTECTED_SACL_SECURITY_INFORMATION)))
        mask |= system;
    if (extras) {
        if (type == SE_FILE_OBJECT)
            mask |= READ_CONTROL_ | 0x80; /* plus FILE_READ_ATTRIBUTES. */
        else if (is_registry(type))
            mask |= 9; /* KEY_QUERY_VALUE and KEY_ENUMERATE_SUB_KEYS. */
    }
    return mask;
}

/* ---------------------------------------------------------------- objects */

static void *service_function(const char *name);

static void obj_close(OBJ *o)
{
    if (!o)
        return;
    if (o->Owned && o->Handle) {
        if (o->Window) {
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            BOOL(WINAPI * close_station)(HANDLE) = user32 ? (BOOL(WINAPI *)(HANDLE))GetProcAddress(user32, "CloseWindowStation") : nullptr;
            if (close_station)
                close_station(o->Handle);
        } else if (is_registry(o->Type)) {
            RegCloseKey(o->Handle);
        } else if (o->Type == SE_SERVICE) {
            BOOL(WINAPI * close_service)(HANDLE) = service_function("CloseServiceHandle");
            if (close_service)
                close_service(o->Handle);
        } else {
            CloseHandle(o->Handle);
        }
    }
    AdvFree(o->Name);
    AdvFree(o);
}

static OBJ *obj_new(SE_OBJECT_TYPE type, HANDLE handle, BOOL owned, PCWSTR name)
{
    OBJ *o = AdvAllocZero(sizeof(*o));
    if (!o)
        return nullptr;
    o->Type = type;
    o->Handle = handle;
    o->Owned = owned;
    if (name) {
        SIZE_T bytes = (wcslen(name) + 1) * sizeof(WCHAR);
        o->Name = AdvAlloc(bytes);
        if (!o->Name) {
            AdvFree(o);
            return nullptr;
        }
        memcpy(o->Name, name, bytes);
    }
    return o;
}

/* opens a file by dos name as a reparse point with backup intent, like windows. */
static DWORD open_file_name(PCWSTR name, DWORD access, OBJ **out)
{
    UNICODE_STRING nt;
    if (!name || !RtlDosPathNameToNtPathName_U(name, &nt, nullptr, nullptr))
        return ERROR_INVALID_NAME_;
    PWSTR buffer = nt.Buffer;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &nt, 0x40 /* OBJ_CASE_INSENSITIVE */, nullptr, nullptr);
    IO_STATUS_BLOCK io = {};
    HANDLE handle = nullptr;
    NTSTATUS status = NtOpenFile(&handle, access, &oa, &io, 7, FILE_OPEN_FOR_BACKUP_INTENT_ | FILE_OPEN_REPARSE_POINT_);
    if (status == STATUS_INVALID_PARAMETER_)
        status = NtOpenFile(&handle, access, &oa, &io, 7, FILE_OPEN_FOR_BACKUP_INTENT_);
    RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, buffer);
    if (!NT_SUCCESS(status))
        return RtlNtStatusToDosError(status);
    OBJ *o = obj_new(SE_FILE_OBJECT, handle, TRUE, name);
    if (!o) {
        CloseHandle(handle);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    *out = o;
    return ERROR_SUCCESS;
}

/* splits a registry name into its root key and the rest. */
static DWORD split_key_name(PCWSTR name, HKEY *root, PCWSTR *rest)
{
    static const struct {
        PCWSTR Name;
        HKEY Key;
    } roots[] = {
        {L"MACHINE", HKEY_LOCAL_MACHINE},     {L"USERS", HKEY_USERS},           {L"USER", HKEY_USERS},
        {L"CLASSES_ROOT", HKEY_CLASSES_ROOT}, {L"CURRENT_USER", HKEY_CURRENT_USER}, {L"CONFIG", HKEY_CURRENT_CONFIG},
    };
    /* a remote registry name needs the remote registry service, which is absent. */
    if (!name || (name[0] == '\\' && name[1] == '\\'))
        return ERROR_INVALID_PARAMETER;
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(roots); i++) {
        SIZE_T n = wcslen(roots[i].Name);
        if (_wcsnicmp(name, roots[i].Name, n) || (name[n] && name[n] != '\\'))
            continue;
        *root = roots[i].Key;
        const WCHAR *r = name + n;
        while (*r == '\\')
            r++;
        *rest = r;
        return ERROR_SUCCESS;
    }
    return ERROR_INVALID_PARAMETER;
}

static DWORD open_key_name(SE_OBJECT_TYPE type, PCWSTR name, DWORD access, OBJ **out)
{
    HKEY root;
    PCWSTR rest;
    DWORD error = split_key_name(name, &root, &rest);
    if (error)
        return error;
    if (type == SE_REGISTRY_WOW64_32KEY)
        access |= KEY_WOW64_32KEY;
    else if (type == SE_REGISTRY_WOW64_64KEY)
        access |= KEY_WOW64_64KEY;
    HKEY key = nullptr;
    error = RegOpenKeyExW(root, rest, 0, access, &key);
    if (error)
        return error;
    OBJ *o = obj_new(type, key, TRUE, name);
    if (!o) {
        RegCloseKey(key);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    *out = o;
    return ERROR_SUCCESS;
}

/* a named kernel object can be any of six kinds; a name of another kind is a bad handle. */
static DWORD open_kernel_name(PCWSTR name, DWORD access, OBJ **out)
{
    HANDLE h = OpenMutexW(access, FALSE, name);
    DWORD error = h ? 0 : GetLastError();
    if (!h && error == ERROR_INVALID_HANDLE) {
        h = OpenEventW(access, FALSE, name);
        error = h ? 0 : GetLastError();
    }
    if (!h && error == ERROR_INVALID_HANDLE) {
        h = OpenSemaphoreW(access, FALSE, name);
        error = h ? 0 : GetLastError();
    }
    if (!h && error == ERROR_INVALID_HANDLE) {
        h = OpenFileMappingW(access, FALSE, name);
        error = h ? 0 : GetLastError();
    }
    if (!h && error == ERROR_INVALID_HANDLE) {
        h = OpenJobObjectW(access, FALSE, name);
        error = h ? 0 : GetLastError();
    }
    if (!h && error == ERROR_INVALID_HANDLE) {
        h = OpenWaitableTimerW(access, FALSE, name);
        error = h ? 0 : GetLastError();
    }
    if (!h)
        return error ? error : ERROR_INTERNAL_ERROR;
    OBJ *o = obj_new(SE_KERNEL_OBJECT, h, TRUE, nullptr);
    if (!o) {
        CloseHandle(h);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    *out = o;
    return ERROR_SUCCESS;
}

/* window stations by name; user32 is loaded when needed. */
static DWORD open_window_name(PCWSTR name, DWORD access, OBJ **out)
{
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    HANDLE(WINAPI * open_station)(PCWSTR, BOOL, DWORD) =
        user32 ? (HANDLE(WINAPI *)(PCWSTR, BOOL, DWORD))GetProcAddress(user32, "OpenWindowStationW") : nullptr;
    if (!open_station)
        return 50 /* ERROR_NOT_SUPPORTED */;
    HANDLE h = open_station(name, FALSE, access);
    if (!h)
        return GetLastError();
    OBJ *o = obj_new(SE_WINDOW_OBJECT, h, TRUE, nullptr);
    if (!o)
        return ERROR_NOT_ENOUGH_MEMORY;
    o->Window = TRUE;
    *out = o;
    return ERROR_SUCCESS;
}

/* services belong to the service control manager, which answers the calls on a handle of its own. */
static void *service_function(const char *name)
{
    HMODULE self = GetModuleHandleW(L"advapi32.dll");
    return self ? (void *)GetProcAddress(self, name) : nullptr;
}

static DWORD open_service_name(PCWSTR name, DWORD access, OBJ **out)
{
    HANDLE(WINAPI * open_manager)(PCWSTR, PCWSTR, DWORD) = service_function("OpenSCManagerW");
    HANDLE(WINAPI * open_service)(HANDLE, PCWSTR, DWORD) = service_function("OpenServiceW");
    BOOL(WINAPI * close_service)(HANDLE) = service_function("CloseServiceHandle");
    if (!open_manager || !open_service || !close_service)
        return ERROR_CALL_NOT_IMPLEMENTED;
    HANDLE manager = open_manager(nullptr, nullptr, 1 /* SC_MANAGER_CONNECT */);
    if (!manager)
        return GetLastError();
    HANDLE service = open_service(manager, name, access);
    DWORD error = service ? ERROR_SUCCESS : GetLastError();
    close_service(manager);
    if (error)
        return error;
    OBJ *o = obj_new(SE_SERVICE, service, TRUE, name);
    if (!o) {
        close_service(service);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    *out = o;
    return ERROR_SUCCESS;
}

static DWORD obj_open_name(SE_OBJECT_TYPE type, PCWSTR name, DWORD access, OBJ **out)
{
    switch (type) {
    case SE_FILE_OBJECT:
        return open_file_name(name, access, out);
    case SE_REGISTRY_KEY:
    case SE_REGISTRY_WOW64_32KEY:
    case SE_REGISTRY_WOW64_64KEY:
        return open_key_name(type, name, access, out);
    case SE_KERNEL_OBJECT:
        return open_kernel_name(name, access, out);
    case SE_WINDOW_OBJECT:
        return open_window_name(name, access, out);
    case SE_SERVICE:
        return open_service_name(name, access, out);
    case SE_PRINTER:
        return 1801 /* ERROR_INVALID_PRINTER_NAME */;
    case SE_LMSHARE:
        return 2310 /* NERR_NetNameNotFound */;
    case SE_DS_OBJECT:
    case SE_DS_OBJECT_ALL:
        return 8453 /* ERROR_DS_UNAVAILABLE: there is no directory service. */;
    case SE_WMIGUID_OBJECT:
        return ERROR_FILE_NOT_FOUND;
    default:
        return ERROR_INVALID_PARAMETER;
    }
}

/* a handle is used as is, with its own rights; only a predefined key is not a real handle and gets opened. */
static DWORD obj_open_handle(SE_OBJECT_TYPE type, HANDLE handle, DWORD access, OBJ **out)
{
    HANDLE use = handle;
    BOOL owned = FALSE;
    if (is_registry(type) && (LONG_PTR)handle < 0) {
        HKEY key;
        if (handle == HKEY_PERFORMANCE_DATA)
            return ERROR_INVALID_HANDLE;
        DWORD error = RegDuplicateHKey(handle, &key);
        if (error)
            return error;
        use = key;
        owned = TRUE;
    } else if (is_registry(type) && access) {
        /* a key is opened again below itself with the rights the parts need, which asks the system again and honors the security privilege. */
        HKEY key;
        DWORD error = RegOpenKeyExW(handle, nullptr, 0, access, &key);
        if (error)
            return error;
        use = key;
        owned = TRUE;
    } else if (access && type != SE_WINDOW_OBJECT && !is_registry(type)) {
        /* any other handle that lacks the rights is duplicated with them. */
        ULONG basic[16] = {0};
        HANDLE more = nullptr;
        if (NT_SUCCESS(NtQueryObject(handle, 0, basic, sizeof(basic), nullptr)) && (basic[1] & access) != access &&
            DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &more, access, FALSE, 0)) {
            use = more;
            owned = TRUE;
        }
    }
    /* a file keeps its name to find its parent and the children its lists go to. */
    WCHAR path[1024];
    DWORD chars = type == SE_FILE_OBJECT ? GetFinalPathNameByHandleW(handle, path, RTL_NUMBER_OF(path), 0) : 0;
    BOOL named = chars && chars < RTL_NUMBER_OF(path);
    if (is_registry(type)) {
        /* a key's name is read from the kernel, as the registry path of the 64 bit view, so its parent can be found. */
        union {
            UNICODE_STRING Name;
            BYTE Bytes[sizeof(UNICODE_STRING) + 2048];
        } info;
        static const WCHAR prefix[] = L"\\REGISTRY\\";
        if (NT_SUCCESS(NtQueryObject(use, 1, &info, sizeof(info), nullptr)) && info.Name.Buffer &&
            info.Name.Length / sizeof(WCHAR) > RTL_NUMBER_OF(prefix) - 1 && info.Name.Length / sizeof(WCHAR) < RTL_NUMBER_OF(path) &&
            !_wcsnicmp(info.Name.Buffer, prefix, RTL_NUMBER_OF(prefix) - 1)) {
            SIZE_T length = info.Name.Length / sizeof(WCHAR) - (RTL_NUMBER_OF(prefix) - 1);
            memcpy(path, info.Name.Buffer + RTL_NUMBER_OF(prefix) - 1, length * sizeof(WCHAR));
            path[length] = 0;
            named = TRUE;
            type = SE_REGISTRY_WOW64_64KEY;
        }
    }
    OBJ *o = obj_new(type, use, owned, named ? path : nullptr);
    if (!o) {
        if (owned) {
            if (is_registry(type))
                RegCloseKey(use);
            else
                CloseHandle(use);
        }
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    o->Window = type == SE_WINDOW_OBJECT;
    *out = o;
    return ERROR_SUCCESS;
}

/* ---------------------------------------------------------------- reading and writing descriptors */

/* the object's descriptor, self relative, in a LocalAlloc block. */
static DWORD obj_get(OBJ *o, DWORD info, BYTE **sd)
{
    *sd = nullptr;
    DWORD needed = 0, error;
    if (o->Window) {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        BOOL(WINAPI * get)(HANDLE, PVOID, PVOID, DWORD, LPDWORD) =
            user32 ? (BOOL(WINAPI *)(HANDLE, PVOID, PVOID, DWORD, LPDWORD))GetProcAddress(user32, "GetUserObjectSecurity") : nullptr;
        if (!get)
            return 50;
        if (get(o->Handle, &info, nullptr, 0, &needed) || !needed)
            return GetLastError();
        BYTE *block = LocalAlloc(LMEM_FIXED, needed);
        if (!block)
            return ERROR_NOT_ENOUGH_MEMORY;
        if (!get(o->Handle, &info, block, needed, &needed)) {
            error = GetLastError();
            LocalFree(block);
            return error;
        }
        *sd = block;
        return ERROR_SUCCESS;
    }
    if (o->Type == SE_SERVICE) {
        BOOL(WINAPI * query)(HANDLE, DWORD, PVOID, DWORD, LPDWORD) = service_function("QueryServiceObjectSecurity");
        if (!query)
            return ERROR_CALL_NOT_IMPLEMENTED;
        if (query(o->Handle, info, nullptr, 0, &needed) || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            return GetLastError();
        BYTE *block = LocalAlloc(LMEM_FIXED, needed);
        if (!block)
            return ERROR_NOT_ENOUGH_MEMORY;
        if (!query(o->Handle, info, block, needed, &needed)) {
            error = GetLastError();
            LocalFree(block);
            return error;
        }
        *sd = block;
        return ERROR_SUCCESS;
    }
    /* files, keys and kernel objects go through the kernel, with room for most descriptors on the first try. */
    ULONG room = 1024;
    BYTE *block = LocalAlloc(LMEM_FIXED, room);
    if (!block)
        return ERROR_NOT_ENOUGH_MEMORY;
    NTSTATUS status = NtQuerySecurityObject(o->Handle, info, block, room, &room);
    if (status == STATUS_BUFFER_TOO_SMALL) {
        LocalFree(block);
        block = LocalAlloc(LMEM_FIXED, room);
        if (!block)
            return ERROR_NOT_ENOUGH_MEMORY;
        status = NtQuerySecurityObject(o->Handle, info, block, room, &room);
    }
    if (!NT_SUCCESS(status)) {
        LocalFree(block);
        return RtlNtStatusToDosError(status);
    }
    *sd = block;
    return ERROR_SUCCESS;
}

static DWORD obj_set(OBJ *o, DWORD info, PVOID sd)
{
    if (o->Window) {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        BOOL(WINAPI * set)(HANDLE, PVOID, PVOID) =
            user32 ? (BOOL(WINAPI *)(HANDLE, PVOID, PVOID))GetProcAddress(user32, "SetUserObjectSecurity") : nullptr;
        if (!set)
            return 50;
        return set(o->Handle, &info, sd) ? ERROR_SUCCESS : GetLastError();
    }
    if (o->Type == SE_SERVICE) {
        BOOL(WINAPI * set)(HANDLE, DWORD, PVOID) = service_function("SetServiceObjectSecurity");
        if (!set)
            return ERROR_CALL_NOT_IMPLEMENTED;
        /* the manager knows the parts, not the protection flags that ride on the same word. */
        return set(o->Handle, info & 0xFF, sd) ? ERROR_SUCCESS : GetLastError();
    }
    NTSTATUS status = NtSetSecurityObject(o->Handle, info, sd);
    return NT_SUCCESS(status) ? ERROR_SUCCESS : RtlNtStatusToDosError(status);
}

/* whether the object can contain others: a directory or a key. */
static BOOL obj_is_container(OBJ *o)
{
    if (is_registry(o->Type))
        return TRUE;
    if (o->Type != SE_FILE_OBJECT)
        return FALSE;
    struct {
        LARGE_INTEGER Times[4];
        ULONG Attributes;
        ULONG Pad;
    } basic = {};
    IO_STATUS_BLOCK io;
    if (!NT_SUCCESS(NtQueryInformationFile(o->Handle, &io, &basic, sizeof(basic), FileBasicInformation)))
        return FALSE;
    /* a link to a directory is a leaf: a walk over this tree does not follow it. */
    return (basic.Attributes & FILE_ATTRIBUTE_DIRECTORY_) && !(basic.Attributes & 0x400 /* FILE_ATTRIBUTE_REPARSE_POINT */);
}

/* ---------------------------------------------------------------- parents and children */

/* the parent's name in a new block, or NULL for a root. */
static PWSTR parent_name(PCWSTR name, BOOL registry)
{
    if (!name)
        return nullptr;
    SIZE_T length = wcslen(name);
    while (length > 1 && name[length - 1] == '\\')
        length--;
    if (registry) {
        SIZE_T cut = length;
        while (cut && name[cut - 1] != '\\')
            cut--;
        if (!cut)
            return nullptr; /* a root key. */
        cut--;
        while (cut && name[cut - 1] == '\\')
            cut--;
        PWSTR parent = AdvAlloc((cut + 1) * sizeof(WCHAR));
        if (parent) {
            memcpy(parent, name, cut * sizeof(WCHAR));
            parent[cut] = 0;
        }
        return parent;
    }
    /* a file: skip a drive "C:", a device prefix "\\?\" or a share "\\server\share" to find the volume root. */
    SIZE_T root = 0;
    if (length >= 4 && name[0] == '\\' && name[1] == '\\' && (name[2] == '?' || name[2] == '.') && name[3] == '\\')
        root = 4;
    if (length >= root + 2 && name[root + 1] == ':') {
        root += 2;
    } else if (!root && length >= 2 && name[0] == '\\' && name[1] == '\\') {
        SIZE_T slashes = 0;
        root = 2;
        while (root < length) {
            if (name[root] == '\\' && ++slashes == 2)
                break;
            root++;
        }
    }
    SIZE_T cut = length;
    while (cut > root + 1 && name[cut - 1] != '\\')
        cut--;
    if (cut <= root + 1)
        return nullptr; /* the root directory itself, or a name directly on a bare drive. */
    SIZE_T keep = cut - 1;
    if (keep <= root)
        keep = root + 1; /* "C:\" keeps its slash. */
    PWSTR parent = AdvAlloc((keep + 1) * sizeof(WCHAR));
    if (parent) {
        memcpy(parent, name, keep * sizeof(WCHAR));
        parent[keep] = 0;
    }
    return parent;
}

/* the object's parent, opened to read the parts. NULL without an error when there is none. */
static DWORD obj_parent(OBJ *o, DWORD info, OBJ **parent)
{
    *parent = nullptr;
    PWSTR name = parent_name(o->Name, is_registry(o->Type));
    if (!name)
        return ERROR_SUCCESS;
    DWORD error = obj_open_name(o->Type, name, desired_access(o->Type, READ_ACCESS, FALSE, info), parent);
    AdvFree(name);
    return error;
}

typedef struct NAMES {
    PWSTR *Item;
    DWORD Count, Capacity;
} NAMES;

static BOOL names_add(NAMES *n, PCWSTR name, SIZE_T chars)
{
    if (n->Count == n->Capacity) {
        DWORD capacity = n->Capacity ? n->Capacity * 2 : 32;
        PWSTR *grown = AdvAlloc(capacity * sizeof(PWSTR));
        if (!grown)
            return FALSE;
        if (n->Item)
            memcpy(grown, n->Item, n->Count * sizeof(PWSTR));
        AdvFree(n->Item);
        n->Item = grown;
        n->Capacity = capacity;
    }
    PWSTR copy = AdvAlloc((chars + 1) * sizeof(WCHAR));
    if (!copy)
        return FALSE;
    memcpy(copy, name, chars * sizeof(WCHAR));
    copy[chars] = 0;
    n->Item[n->Count++] = copy;
    return TRUE;
}

static void names_free(NAMES *n)
{
    for (DWORD i = 0; i < n->Count; i++)
        AdvFree(n->Item[i]);
    AdvFree(n->Item);
    memset(n, 0, sizeof(*n));
}

/* the names directly inside a container. */
static DWORD obj_children(OBJ *dir, NAMES *names)
{
    memset(names, 0, sizeof(*names));
    if (is_registry(dir->Type)) {
        DWORD count = 0, longest = 0;
        DWORD error = RegQueryInfoKeyW(dir->Handle, nullptr, nullptr, nullptr, &count, &longest, nullptr, nullptr,
                                       nullptr, nullptr, nullptr, nullptr);
        if (error)
            return error;
        PWSTR buffer = AdvAlloc((longest + 2) * sizeof(WCHAR));
        if (!buffer)
            return ERROR_NOT_ENOUGH_MEMORY;
        for (DWORD i = 0; i < count && !error; i++) {
            DWORD chars = longest + 1;
            error = RegEnumKeyExW(dir->Handle, i, buffer, &chars, nullptr, nullptr, nullptr, nullptr);
            if (error == ERROR_NO_MORE_ITEMS_) {
                error = 0;
                break;
            }
            if (!error && !names_add(names, buffer, chars))
                error = ERROR_NOT_ENOUGH_MEMORY;
        }
        AdvFree(buffer);
        if (error)
            names_free(names);
        return error;
    }
    /* a directory is listed through its own handle. */
    OBJ *listing;
    DWORD error = open_file_name(dir->Name, 0x100001 /* SYNCHRONIZE | FILE_LIST_DIRECTORY */, &listing);
    if (error)
        return error;
    BYTE *buffer = AdvAlloc(0x8000);
    if (!buffer) {
        obj_close(listing);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    BOOLEAN first = TRUE;
    for (;;) {
        IO_STATUS_BLOCK io = {};
        NTSTATUS status = NtQueryDirectoryFile(listing->Handle, nullptr, nullptr, nullptr, &io, buffer, 0x8000,
                                               FileDirectoryInformation, FALSE, nullptr, first);
        first = FALSE;
        if (!NT_SUCCESS(status)) {
            error = status == STATUS_NO_MORE_FILES_ || status == STATUS_NO_MORE_FILES ? 0 : RtlNtStatusToDosError(status);
            break;
        }
        for (BYTE *e = buffer;;) {
            /* entry layout: next offset, index, four times, two sizes, attributes, name length. */
            ULONG next = *(ULONG *)e, attributes = *(ULONG *)(e + 56), bytes = *(ULONG *)(e + 60);
            PCWSTR entry = (PCWSTR)(e + 64);
            BOOL dots = (bytes == 2 && entry[0] == '.') || (bytes == 4 && entry[0] == '.' && entry[1] == '.');
            if (!dots && !(attributes & FILE_ATTRIBUTE_RECALL_ON_OPEN_) && !names_add(names, entry, bytes / 2)) {
                error = ERROR_NOT_ENOUGH_MEMORY;
                break;
            }
            if (!next)
                break;
            e += next;
        }
        if (error)
            break;
    }
    AdvFree(buffer);
    obj_close(listing);
    if (error)
        names_free(names);
    return error;
}

/* opens one child by a name obj_children returned. */
static DWORD obj_open_child(OBJ *dir, PCWSTR name, DWORD access, OBJ **child)
{
    /* a key opened by handle has no name to build on, so its children are opened through the handle alone. */
    PWSTR joined = nullptr;
    if (dir->Name) {
        SIZE_T length = wcslen(dir->Name);
        joined = AdvAlloc((length + 1 + wcslen(name) + 1) * sizeof(WCHAR));
        if (!joined)
            return ERROR_NOT_ENOUGH_MEMORY;
        memcpy(joined, dir->Name, length * sizeof(WCHAR));
        if (!length || joined[length - 1] != '\\')
            joined[length++] = '\\';
        wcscpy(joined + length, name);
    }
    DWORD error;
    if (is_registry(dir->Type)) {
        DWORD wow = dir->Type == SE_REGISTRY_WOW64_32KEY ? KEY_WOW64_32KEY
                    : dir->Type == SE_REGISTRY_WOW64_64KEY ? KEY_WOW64_64KEY : 0;
        HKEY key;
        error = RegOpenKeyExW(dir->Handle, name, 0, access | wow, &key);
        if (!error) {
            *child = obj_new(dir->Type, key, TRUE, joined);
            if (!*child) {
                RegCloseKey(key);
                error = ERROR_NOT_ENOUGH_MEMORY;
            }
        }
    } else {
        error = joined ? open_file_name(joined, access, child) : ERROR_INVALID_NAME_;
    }
    AdvFree(joined);
    return error;
}

/* ---------------------------------------------------------------- rebuilding descriptors */

static HANDLE open_token(void)
{
    HANDLE token = nullptr;
    if (!NT_SUCCESS(NtOpenThreadToken(NtCurrentThread(), TOKEN_QUERY, TRUE, &token)) &&
        !NT_SUCCESS(NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &token)))
        return nullptr;
    return token;
}

static WORD control_of(const void *sd)
{
    return ((const SECURITY_DESCRIPTOR *)sd)->Control;
}

static void add_control(void *sd, WORD bits)
{
    ((SECURITY_DESCRIPTOR *)sd)->Control |= bits;
}

/*
 * builds an absolute descriptor from the caller's parts, like SetNamedSecurityInfo. a malformed part is refused with
 * its kind's error before anything is touched.
 */
static DWORD build_absolute(SECURITY_DESCRIPTOR *sd, DWORD info, PSID owner, PSID group, PACL dacl, PACL sacl)
{
    RtlCreateSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION);
    if (info & OWNER_SECURITY_INFORMATION) {
        if (!owner)
            return ERROR_INVALID_PARAMETER;
        if (!RtlValidSid(owner))
            return ERROR_INVALID_OWNER;
        RtlSetOwnerSecurityDescriptor(sd, owner, FALSE);
    }
    if (info & GROUP_SECURITY_INFORMATION) {
        if (!group)
            return ERROR_INVALID_PARAMETER;
        if (!RtlValidSid(group))
            return 1308 /* ERROR_INVALID_PRIMARY_GROUP */;
        RtlSetGroupSecurityDescriptor(sd, group, FALSE);
    }
    if (info & DACL_SECURITY_INFORMATION) {
        /* a missing list is a null one, which nothing can inherit into, so it starts protected. */
        if (dacl) {
            if (!RtlValidAcl(dacl))
                return ERROR_INVALID_ACL;
            RtlSetDaclSecurityDescriptor(sd, TRUE, dacl, FALSE);
            if (info & PROTECTED_DACL_SECURITY_INFORMATION)
                add_control(sd, SE_DACL_PROTECTED);
        } else {
            add_control(sd, SE_DACL_PROTECTED);
        }
    }
    if (info & SACL_PARTS) {
        if (sacl) {
            if (!RtlValidAcl(sacl))
                return ERROR_INVALID_ACL;
            RtlSetSaclSecurityDescriptor(sd, TRUE, sacl, FALSE);
        }
        if (!sacl || (info & PROTECTED_SACL_SECURITY_INFORMATION))
            add_control(sd, SE_SACL_PROTECTED);
    }
    return ERROR_SUCCESS;
}

/* a new descriptor from the creator's parts and what the parent passes down. */
static DWORD derive(const void *parent, const void *creator, BOOL container, HANDLE token, const GENERIC_MAPPING *mapping,
                    MADE *out)
{
    *out = nullptr;
    NTSTATUS status = RtlNewSecurityObjectEx((PSECURITY_DESCRIPTOR)parent, (PSECURITY_DESCRIPTOR)creator,
                                             (PSECURITY_DESCRIPTOR *)out, nullptr, container, INHERIT_FLAGS, token,
                                             (PGENERIC_MAPPING)mapping);
    return NT_SUCCESS(status) ? ERROR_SUCCESS : RtlNtStatusToDosError(status);
}

static BOOL list_equal(const ACL *a, const ACL *b)
{
    if (!a || !b)
        return a == b;
    return a->AclSize == b->AclSize && a->AceCount == b->AceCount &&
           !memcmp(a + 1, b + 1, a->AclSize - sizeof(ACL));
}

/* whether the new descriptor matches the current one for the named parts. */
static BOOL unchanged(const void *current, const void *replacement, DWORD info)
{
    BOOLEAN defaulted, cp, np;
    PSID co = nullptr, cg = nullptr, no = nullptr, ng = nullptr;
    PACL cd = nullptr, cs = nullptr, nd = nullptr, ns = nullptr;
    RtlGetOwnerSecurityDescriptor((PSECURITY_DESCRIPTOR)current, &co, &defaulted);
    RtlGetOwnerSecurityDescriptor((PSECURITY_DESCRIPTOR)replacement, &no, &defaulted);
    RtlGetGroupSecurityDescriptor((PSECURITY_DESCRIPTOR)current, &cg, &defaulted);
    RtlGetGroupSecurityDescriptor((PSECURITY_DESCRIPTOR)replacement, &ng, &defaulted);
    WORD cc = control_of(current), nc = control_of(replacement);
    if ((info & OWNER_SECURITY_INFORMATION) && !((!co && !no) || (co && no && RtlEqualSid(co, no))))
        return FALSE;
    if ((info & GROUP_SECURITY_INFORMATION) && !((!cg && !ng) || (cg && ng && RtlEqualSid(cg, ng))))
        return FALSE;
    if (info & DACL_SECURITY_INFORMATION) {
        RtlGetDaclSecurityDescriptor((PSECURITY_DESCRIPTOR)current, &cp, &cd, &defaulted);
        RtlGetDaclSecurityDescriptor((PSECURITY_DESCRIPTOR)replacement, &np, &nd, &defaulted);
        if (cp != np || !list_equal(cd, nd) || ((cc ^ nc) & (SE_DACL_PROTECTED | SE_DACL_AUTO_INHERITED)))
            return FALSE;
    }
    if (info & SACL_PARTS) {
        RtlGetSaclSecurityDescriptor((PSECURITY_DESCRIPTOR)current, &cp, &cs, &defaulted);
        RtlGetSaclSecurityDescriptor((PSECURITY_DESCRIPTOR)replacement, &np, &ns, &defaulted);
        if (cp != np || !list_equal(cs, ns) || ((cc ^ nc) & (SE_SACL_PROTECTED | SE_SACL_AUTO_INHERITED)))
            return FALSE;
    }
    return TRUE;
}

/* ---------------------------------------------------------------- propagation */

#define PROGRESS_INVOKE_NEVER 1
#define PROGRESS_INVOKE_EVERY_OBJECT 2
#define PROGRESS_INVOKE_ON_ERROR 3
#define PROGRESS_CANCEL_OPERATION 4
#define PROGRESS_RETRY_OPERATION 5
#define PROGRESS_INVOKE_PRE_POST_ERROR 6

typedef VOID(CALLBACK *PROGRESS_FN)(PWSTR name, DWORD status, PDWORD setting, PVOID args, BOOL set);

typedef struct TREE {
    HANDLE Token;
    const GENERIC_MAPPING *Mapping;
    PROGRESS_FN Progress; /* NULL for a plain change. */
    DWORD Setting;
    PVOID Args;
    BOOL Reset;    /* children lose their own entries and their protection. */
    BOOL Unprotect; /* children lose their protection. */
    BOOL Cancelled;
    DWORD Failed;  /* the first child error, which the tree functions report. */
    DWORD Owners;  /* owner and group, which a tree operation sets on every object in it. */
    PSID Owner, Group;
} TREE;

/* calls the progress routine for one object; returns whether it asked to retry the object. */
static BOOL report(TREE *t, PCWSTR name, DWORD error, BOOL set)
{
    if (!t || !t->Progress || t->Cancelled || t->Setting == PROGRESS_INVOKE_NEVER)
        return FALSE;
    if (t->Setting == PROGRESS_INVOKE_ON_ERROR && !error)
        return FALSE;
    DWORD setting = t->Setting;
    t->Progress((PWSTR)name, error, &setting, t->Args, set);
    t->Setting = setting;
    if (setting == PROGRESS_CANCEL_OPERATION)
        t->Cancelled = TRUE;
    return setting == PROGRESS_RETRY_OPERATION;
}

static void update_children(OBJ *dir, DWORD info, const void *new_parent, TREE *tree);

/* a descriptor holding only the parts a tree sets on everything in it. */
static DWORD owners_only(TREE *tree, SECURITY_DESCRIPTOR *sd)
{
    RtlCreateSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION);
    if (tree->Owners & OWNER_SECURITY_INFORMATION)
        RtlSetOwnerSecurityDescriptor(sd, tree->Owner, FALSE);
    if (tree->Owners & GROUP_SECURITY_INFORMATION)
        RtlSetGroupSecurityDescriptor(sd, tree->Group, FALSE);
    return tree->Owners;
}

/*
 * rebuilds one child's lists from its parent's new ones, keeping its explicit aces, sets them when they changed and
 * recurses into the child. a child that protects its lists is skipped with everything below it. owner and group are
 * left alone unless a tree operation sets them on every object.
 */
static void update_child(OBJ *dir, PCWSTR name, DWORD info, const void *new_parent, TREE *tree)
{
    DWORD change = info & LIST_PARTS;
    DWORD owners = tree ? tree->Owners : 0;
    DWORD read_info = change | OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION;
    DWORD access = desired_access(dir->Type, MODIFY_ACCESS, TRUE, read_info) |
                   desired_access(dir->Type, WRITE_ACCESS, FALSE, change | owners);
    for (;;) {
        OBJ *child = nullptr;
        DWORD error = obj_open_child(dir, name, access, &child);
        BOOL set = FALSE;
        if (!error) {
            BYTE *current = nullptr;
            error = obj_get(child, read_info, &current);
            if (!error) {
                WORD control = control_of(current);
                BOOL ignore = tree && tree->Unprotect;
                DWORD apply = ((change & DACL_SECURITY_INFORMATION) && (ignore || !(control & SE_DACL_PROTECTED)) ? DACL_SECURITY_INFORMATION : 0) |
                              ((change & SACL_PARTS) && (ignore || !(control & SE_SACL_PROTECTED)) ? change & SACL_PARTS : 0);
                BOOL container = obj_is_container(child);
                MADE made = nullptr;
                if (new_parent && apply) {
                    SECURITY_DESCRIPTOR stripped;
                    BYTE empty[8];
                    PVOID creator = current;
                    if (tree && tree->Reset) {
                        /* no explicit entries of its own: owner and group stay, the lists are empty. */
                        PSID owner = nullptr, group = nullptr;
                        BOOLEAN defaulted;
                        RtlCreateSecurityDescriptor(&stripped, SECURITY_DESCRIPTOR_REVISION);
                        RtlGetOwnerSecurityDescriptor(current, &owner, &defaulted);
                        RtlGetGroupSecurityDescriptor(current, &group, &defaulted);
                        RtlSetOwnerSecurityDescriptor(&stripped, owner, FALSE);
                        RtlSetGroupSecurityDescriptor(&stripped, group, FALSE);
                        RtlCreateAcl((PACL)empty, sizeof(empty), ACL_REVISION);
                        if (apply & DACL_SECURITY_INFORMATION)
                            RtlSetDaclSecurityDescriptor(&stripped, TRUE, (PACL)empty, FALSE);
                        if (apply & SACL_PARTS)
                            RtlSetSaclSecurityDescriptor(&stripped, TRUE, (PACL)empty, FALSE);
                        creator = &stripped;
                    } else if (ignore) {
                        /* drop the protection from the child's current descriptor. */
                        ((SECURITY_DESCRIPTOR_RELATIVE *)current)->Control &= ~(SE_DACL_PROTECTED | SE_SACL_PROTECTED);
                    }
                    error = derive(new_parent, creator, container, tree ? tree->Token : nullptr,
                                   tree && tree->Mapping ? tree->Mapping : mapping_of(dir->Type), &made);
                }
                if (!error && container && ((new_parent && apply) || owners))
                    update_children(child, (apply ? change : 0) | owners, made, tree);
                BOOL force = ignore && (control & (SE_DACL_PROTECTED | SE_SACL_PROTECTED));
                if (!error && made && (force || !unchanged(current, made, apply))) {
                    add_control(made, ((apply & DACL_SECURITY_INFORMATION) ? SE_DACL_AUTO_INHERIT_REQ : 0) |
                                          ((apply & SACL_PARTS) ? SE_SACL_AUTO_INHERIT_REQ : 0));
                    DWORD flags = ignore ? ((apply & DACL_SECURITY_INFORMATION ? UNPROTECTED_DACL_SECURITY_INFORMATION : 0) |
                                            (apply & SACL_PARTS ? UNPROTECTED_SACL_SECURITY_INFORMATION : 0))
                                         : 0;
                    error = obj_set(child, apply | flags, made);
                    set = !error;
                }
                if (!error && owners) {
                    SECURITY_DESCRIPTOR given;
                    error = obj_set(child, owners_only(tree, &given), &given);
                    set = set || !error;
                }
                made_free(made);
            }
            if (current)
                LocalFree(current);
            if (error && tree && !tree->Failed)
                tree->Failed = error;
            BOOL retry = report(tree, child->Name ? child->Name : name, error, set);
            obj_close(child);
            if (!retry || tree->Cancelled)
                break;
        } else {
            if (tree && !tree->Failed)
                tree->Failed = error;
            if (!report(tree, name, error, FALSE) || tree->Cancelled)
                break;
        }
    }
}

static void update_children(OBJ *dir, DWORD info, const void *new_parent, TREE *tree)
{
    NAMES names;
    if (obj_children(dir, &names))
        return;
    for (DWORD i = 0; i < names.Count && !(tree && tree->Cancelled); i++)
        update_child(dir, names.Item[i], info, new_parent, tree);
    names_free(&names);
}

/*
 * sets the object's parts and, when a file's or key's lists change, rebuilds the lists below it. sd is absolute, with
 * the parts and protection the caller chose.
 */
static DWORD set_object(OBJ *o, DWORD info, SECURITY_DESCRIPTOR *sd, BOOL keep_protection, TREE *tree)
{
    DWORD parts = info & 0xFFFF;
    if (!propagates(o->Type) || !(info & LIST_PARTS)) {
        if (tree && tree->Owners && propagates(o->Type) && obj_is_container(o))
            update_children(o, tree->Owners, nullptr, tree);
        /* no generic mapping is known here: the kernel maps generic rights, and the lists are marked processed. */
        add_control(sd, ((info & DACL_SECURITY_INFORMATION) ? SE_DACL_AUTO_INHERIT_REQ | SE_DACL_AUTO_INHERITED : 0) |
                            ((info & SACL_PARTS) ? SE_SACL_AUTO_INHERIT_REQ | SE_SACL_AUTO_INHERITED : 0));
        return obj_set(o, info, sd);
    }
    BOOL container = obj_is_container(o);
    HANDLE token = open_token();
    DWORD error = ERROR_SUCCESS;
    OBJ *parent = nullptr;
    BYTE *parent_sd = nullptr, *current = nullptr;
    MADE made = nullptr;
    WORD control = control_of(sd);
    /* what the parent passes down, when either list inherits. */
    if (((info & DACL_SECURITY_INFORMATION) && !(control & SE_DACL_PROTECTED)) ||
        ((parts & SACL_PARTS) && !(control & SE_SACL_PROTECTED))) {
        error = obj_parent(o, parts & LIST_PARTS, &parent);
        if (!error && parent) {
            error = obj_get(parent, parts & LIST_PARTS, &parent_sd);
            if (!error)
                add_control(parent_sd, ((info & DACL_SECURITY_INFORMATION) ? SE_DACL_AUTO_INHERIT_REQ : 0) |
                                           ((info & SACL_PARTS) ? SE_SACL_AUTO_INHERIT_REQ : 0));
            obj_close(parent);
        }
    }
    /* current owner, group and protection, for what the caller left out. */
    PSID owner = nullptr, group = nullptr;
    BOOLEAN defaulted;
    RtlGetOwnerSecurityDescriptor(sd, &owner, &defaulted);
    RtlGetGroupSecurityDescriptor(sd, &group, &defaulted);
    if (!error) {
        DWORD read = parts | (owner ? 0 : OWNER_SECURITY_INFORMATION) | (group ? 0 : GROUP_SECURITY_INFORMATION);
        error = obj_get(o, read, &current);
    }
    if (!error) {
        if (!owner) {
            RtlGetOwnerSecurityDescriptor(current, &owner, &defaulted);
            RtlSetOwnerSecurityDescriptor(sd, owner, FALSE);
        }
        if (!group) {
            RtlGetGroupSecurityDescriptor(current, &group, &defaulted);
            RtlSetGroupSecurityDescriptor(sd, group, FALSE);
        }
        if (keep_protection) {
            if ((info & (DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION)) == DACL_SECURITY_INFORMATION)
                add_control(sd, control_of(current) & SE_DACL_PROTECTED);
            if ((parts & SACL_PARTS) && !(info & (PROTECTED_SACL_SECURITY_INFORMATION | UNPROTECTED_SACL_SECURITY_INFORMATION)))
                add_control(sd, control_of(current) & SE_SACL_PROTECTED);
        }
        error = derive(parent_sd, sd, container, token, mapping_of(o->Type), &made);
    }
    if (!error) {
        if (container) {
            TREE local = {token, mapping_of(o->Type)};
            TREE *use = tree ? tree : &local;
            use->Token = token;
            use->Mapping = mapping_of(o->Type);
            update_children(o, info, made, use);
        }
        add_control(made, ((info & DACL_SECURITY_INFORMATION) ? SE_DACL_AUTO_INHERIT_REQ : 0) |
                              ((info & SACL_PARTS) ? SE_SACL_AUTO_INHERIT_REQ : 0));
        error = obj_set(o, info, made);
    }
    made_free(made);
    if (current)
        LocalFree(current);
    if (parent_sd)
        LocalFree(parent_sd);
    if (token)
        CloseHandle(token);
    return error;
}

/* ---------------------------------------------------------------- reading the parts */

/* returns the descriptor and the pointers into it, as GetNamedSecurityInfo does. */
static DWORD give_parts(BYTE *sd, DWORD info, PSID *owner, PSID *group, PACL *dacl, PACL *sacl, PSECURITY_DESCRIPTOR *result)
{
    SECURITY_DESCRIPTOR_RELATIVE *r = (SECURITY_DESCRIPTOR_RELATIVE *)sd;
    if ((info & OWNER_SECURITY_INFORMATION) && owner)
        *owner = r->Owner ? (PSID)(sd + r->Owner) : nullptr;
    if ((info & GROUP_SECURITY_INFORMATION) && group)
        *group = r->Group ? (PSID)(sd + r->Group) : nullptr;
    if ((info & DACL_SECURITY_INFORMATION) && dacl)
        *dacl = (r->Control & SE_DACL_PRESENT) && r->Dacl ? (PACL)(sd + r->Dacl) : nullptr;
    if ((info & SACL_PARTS) && sacl)
        *sacl = (r->Control & SE_SACL_PRESENT) && r->Sacl ? (PACL)(sd + r->Sacl) : nullptr;
    if (result)
        *result = sd;
    else
        LocalFree(sd);
    return ERROR_SUCCESS;
}

/* checks and clears the arguments of both readers, in the order windows does. */
static DWORD prepare_output(DWORD info, PSID *owner, PSID *group, PACL *dacl, PACL *sacl, PSECURITY_DESCRIPTOR *result)
{
    if (info & OWNER_SECURITY_INFORMATION) {
        if (owner)
            *owner = nullptr;
        else if (!result)
            return ERROR_INVALID_PARAMETER;
    }
    if (info & GROUP_SECURITY_INFORMATION) {
        if (group)
            *group = nullptr;
        else if (!result)
            return ERROR_INVALID_PARAMETER;
    }
    if (info & DACL_SECURITY_INFORMATION) {
        if (dacl)
            *dacl = nullptr;
        else if (!result)
            return ERROR_INVALID_PARAMETER;
    }
    if (info & SACL_PARTS) {
        if (sacl)
            *sacl = nullptr;
        else if (!result)
            return ERROR_INVALID_PARAMETER;
    }
    if (result)
        *result = nullptr;
    return ERROR_SUCCESS;
}

static BOOL valid_name_type(SE_OBJECT_TYPE type)
{
    return (type >= SE_FILE_OBJECT && type <= SE_DS_OBJECT_ALL) || (type >= SE_WMIGUID_OBJECT && type <= SE_REGISTRY_WOW64_64KEY);
}

static BOOL valid_handle_type(SE_OBJECT_TYPE type)
{
    return (type >= SE_FILE_OBJECT && type <= SE_WINDOW_OBJECT) || type == SE_WMIGUID_OBJECT;
}

/* opens with the access the parts need, trying the extras first since the object may refuse them. */
static DWORD open_to_read(SE_OBJECT_TYPE type, PCWSTR name, HANDLE handle, DWORD info, OBJ **o)
{
    DWORD error;
    for (int attempt = 0; attempt < 2; attempt++) {
        DWORD access = desired_access(type, READ_ACCESS, attempt == 0, info);
        error = name ? obj_open_name(type, name, access, o) : obj_open_handle(type, handle, access, o);
        if (!error)
            return ERROR_SUCCESS;
        /* a missing name stays missing without the extras too. */
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_INVALID_NAME_)
            break;
    }
    return error;
}

static DWORD get_security(SE_OBJECT_TYPE type, PCWSTR name, HANDLE handle, DWORD info, PSID *owner, PSID *group, PACL *dacl,
                          PACL *sacl, PSECURITY_DESCRIPTOR *result)
{
    DWORD error = prepare_output(info, owner, group, dacl, sacl, result);
    if (error)
        return error;
    info &= 0xFFFFFFF;
    OBJ *o = nullptr;
    error = open_to_read(type, name, handle, info, &o);
    if (error)
        return error;
    BYTE *sd = nullptr;
    error = obj_get(o, info, &sd);
    obj_close(o);
    if (error)
        return error;
    if (!sd)
        return ERROR_INTERNAL_ERROR;
    return give_parts(sd, info, owner, group, dacl, sacl, result);
}

ADVAPI DWORD WINAPI GetNamedSecurityInfoW(LPCWSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID *owner,
                                          PSID *group, PACL *dacl, PACL *sacl, PSECURITY_DESCRIPTOR *sd)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    DWORD error = prepare_output(info, owner, group, dacl, sacl, sd);
    if (error)
        return error;
    if (!valid_name_type(type))
        return ERROR_INVALID_PARAMETER;
    return get_security(type, name, nullptr, info, owner, group, dacl, sacl, sd);
}

ADVAPI DWORD WINAPI GetNamedSecurityInfoA(LPCSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID *owner,
                                          PSID *group, PACL *dacl, PACL *sacl, PSECURITY_DESCRIPTOR *sd)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    PWSTR wide = AdvAnsiToWide(name);
    if (!wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD error = GetNamedSecurityInfoW(wide, type, info, owner, group, dacl, sacl, sd);
    AdvFree(wide);
    return error;
}

ADVAPI DWORD WINAPI GetSecurityInfo(HANDLE handle, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID *owner,
                                    PSID *group, PACL *dacl, PACL *sacl, PSECURITY_DESCRIPTOR *sd)
{
    if (!handle)
        return ERROR_INVALID_HANDLE;
    DWORD error = prepare_output(info, owner, group, dacl, sacl, sd);
    if (error)
        return error;
    if (!valid_handle_type(type))
        return ERROR_INVALID_PARAMETER;
    return get_security(type, nullptr, handle, info, owner, group, dacl, sacl, sd);
}

/* ---------------------------------------------------------------- setting the parts */

/* checks the user's parts of a change by name or handle and puts them in a descriptor. */
static DWORD set_security(SE_OBJECT_TYPE type, PCWSTR name, HANDLE handle, DWORD info, PSID owner, PSID group, PACL dacl,
                          PACL sacl)
{
    SECURITY_DESCRIPTOR sd;
    DWORD error = build_absolute(&sd, info, owner, group, dacl, sacl);
    if (error)
        return error;
    /* a list's protection is kept when the caller says nothing about it. */
    BOOL keep = ((info & DACL_SECURITY_INFORMATION) && !(info & (PROTECTED_DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION))) ||
                ((info & SACL_PARTS) && !(info & (PROTECTED_SACL_SECURITY_INFORMATION | UNPROTECTED_SACL_SECURITY_INFORMATION)));
    OBJ *o = nullptr;
    DWORD access = desired_access(type, propagates(type) && (info & LIST_PARTS) ? MODIFY_ACCESS : WRITE_ACCESS, TRUE, info);
    if (!propagates(type) || !(info & LIST_PARTS))
        access = desired_access(type, WRITE_ACCESS, FALSE, info);
    error = name ? obj_open_name(type, name, access, &o) : obj_open_handle(type, handle, access, &o);
    /* the extras are a convenience: the object may still open without them. */
    if (error && propagates(type) && (info & LIST_PARTS) && error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
        access = desired_access(type, MODIFY_ACCESS, FALSE, info);
        error = name ? obj_open_name(type, name, access, &o) : obj_open_handle(type, handle, access, &o);
    }
    if (error)
        return error;
    error = set_object(o, info, &sd, keep, nullptr);
    obj_close(o);
    return error;
}

ADVAPI DWORD WINAPI SetNamedSecurityInfoW(LPWSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID owner,
                                          PSID group, PACL dacl, PACL sacl)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    if (!valid_name_type(type))
        return ERROR_INVALID_PARAMETER;
    return set_security(type, name, nullptr, info, owner, group, dacl, sacl);
}

ADVAPI DWORD WINAPI SetNamedSecurityInfoA(LPSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID owner,
                                          PSID group, PACL dacl, PACL sacl)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    PWSTR wide = AdvAnsiToWide(name);
    if (!wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD error = SetNamedSecurityInfoW(wide, type, info, owner, group, dacl, sacl);
    AdvFree(wide);
    return error;
}

ADVAPI DWORD WINAPI SetSecurityInfo(HANDLE handle, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID owner, PSID group,
                                    PACL dacl, PACL sacl)
{
    if (!handle)
        return ERROR_INVALID_HANDLE;
    if (!valid_handle_type(type))
        return ERROR_INVALID_PARAMETER;
    return set_security(type, nullptr, handle, info, owner, group, dacl, sacl);
}

/* ---------------------------------------------------------------- older direct functions */

/* handle access for a plain read or write of the parts, as GetFileSecurity and RegGetKeySecurity request it. */
static DWORD plain_access(DWORD info, BOOL write)
{
    DWORD mask = 0;
    if (info & (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION))
        mask |= write ? WRITE_OWNER_ : READ_CONTROL_;
    if (info & (DACL_SECURITY_INFORMATION | ATTRIBUTE_SECURITY_INFORMATION | PROCESS_TRUST_LABEL_SECURITY_INFORMATION |
                ACCESS_FILTER_SECURITY_INFORMATION))
        mask |= write ? WRITE_DAC_ : READ_CONTROL_;
    if (info & (SACL_SECURITY_INFORMATION | SCOPE_SECURITY_INFORMATION))
        mask |= ACCESS_SYSTEM_SECURITY;
    if ((info & SACL_PARTS) && (info & (PROTECTED_SACL_SECURITY_INFORMATION | UNPROTECTED_SACL_SECURITY_INFORMATION)))
        mask |= ACCESS_SYSTEM_SECURITY;
    return mask;
}

static HANDLE open_plain_file(PCWSTR name, DWORD access, DWORD *error)
{
    OBJ *o = nullptr;
    *error = open_file_name(name, access, &o);
    if (*error)
        return nullptr;
    HANDLE h = o->Handle;
    o->Owned = FALSE;
    obj_close(o);
    return h;
}

ADVAPI BOOL WINAPI GetFileSecurityW(LPCWSTR name, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd, DWORD length,
                                    LPDWORD needed)
{
    if (!name || !needed) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD error;
    HANDLE h = open_plain_file(name, plain_access(info, FALSE), &error);
    if (!h) {
        AdvSetError(error);
        return FALSE;
    }
    NTSTATUS status = NtQuerySecurityObject(h, info, sd, length, needed);
    NtClose(h);
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

ADVAPI BOOL WINAPI GetFileSecurityA(LPCSTR name, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd, DWORD length,
                                    LPDWORD needed)
{
    if (!name) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR wide = AdvAnsiToWide(name);
    if (!wide) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    BOOL ok = GetFileSecurityW(wide, info, sd, length, needed);
    DWORD error = GetLastError();
    AdvFree(wide);
    if (!ok)
        AdvSetError(error);
    return ok;
}

ADVAPI BOOL WINAPI SetFileSecurityW(LPCWSTR name, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd)
{
    if (!name) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD error;
    HANDLE h = open_plain_file(name, plain_access(info, TRUE), &error);
    if (!h) {
        AdvSetError(error);
        return FALSE;
    }
    NTSTATUS status = NtSetSecurityObject(h, info, sd);
    NtClose(h);
    return NT_SUCCESS(status) ? TRUE : AdvSetStatus(status);
}

ADVAPI BOOL WINAPI SetFileSecurityA(LPCSTR name, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd)
{
    if (!name) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR wide = AdvAnsiToWide(name);
    if (!wide) {
        AdvSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    BOOL ok = SetFileSecurityW(wide, info, sd);
    DWORD error = GetLastError();
    AdvFree(wide);
    if (!ok)
        AdvSetError(error);
    return ok;
}

/* a predefined key stands for a handle the registry keeps, so its security is read through a key opened on it. */
static LONG key_for_security(HKEY key, DWORD access, HKEY *real, BOOL *opened)
{
    *opened = FALSE;
    if ((LONG_PTR)key >= 0) {
        *real = key;
        return ERROR_SUCCESS;
    }
    if (key == HKEY_PERFORMANCE_DATA)
        return ERROR_INVALID_HANDLE;
    LONG error = RegDuplicateHKey(key, real);
    *opened = error == ERROR_SUCCESS;
    return error;
}

ADVAPI LSTATUS WINAPI RegSetKeySecurity(HKEY key, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd)
{
    if (!sd || !RtlValidSecurityDescriptor(sd))
        return ERROR_INVALID_PARAMETER;
    HKEY real;
    BOOL opened;
    LONG error = key_for_security(key, plain_access(info, TRUE), &real, &opened);
    if (error)
        return error;
    NTSTATUS status = NtSetSecurityObject(real, info, sd);
    if (opened)
        RegCloseKey(real);
    return NT_SUCCESS(status) ? ERROR_SUCCESS : RtlNtStatusToDosError(status);
}

ADVAPI LSTATUS WINAPI RegGetKeySecurity(HKEY key, SECURITY_INFORMATION info, PSECURITY_DESCRIPTOR sd, LPDWORD size)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    HKEY real;
    BOOL opened;
    LONG error = key_for_security(key, plain_access(info, FALSE), &real, &opened);
    if (error)
        return error;
    ULONG needed = 0;
    NTSTATUS status = NtQuerySecurityObject(real, info, sd, *size, &needed);
    if (opened)
        RegCloseKey(real);
    if (NT_SUCCESS(status))
        return ERROR_SUCCESS;
    if (status == STATUS_BUFFER_TOO_SMALL) {
        *size = needed;
        return ERROR_INSUFFICIENT_BUFFER_;
    }
    return RtlNtStatusToDosError(status);
}

/* ---------------------------------------------------------------- whole trees */

#define TREE_SEC_INFO_SET 1
#define TREE_SEC_INFO_RESET 2
#define TREE_SEC_INFO_RESET_KEEP_EXPLICIT 3

static DWORD tree_operation(PWSTR name, SE_OBJECT_TYPE type, DWORD info, PSID owner, PSID group, PACL dacl, PACL sacl,
                            DWORD action, PROGRESS_FN progress, DWORD setting, PVOID args)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    /* only file and key trees exist here. */
    if (type != SE_FILE_OBJECT && !is_registry(type))
        return ERROR_INVALID_PARAMETER;
    if (action < TREE_SEC_INFO_SET || action > TREE_SEC_INFO_RESET_KEEP_EXPLICIT)
        return ERROR_INVALID_PARAMETER;
    if (progress && (setting < PROGRESS_INVOKE_NEVER || setting > PROGRESS_INVOKE_PRE_POST_ERROR))
        return ERROR_INVALID_PARAMETER;
    SECURITY_DESCRIPTOR sd;
    DWORD error = build_absolute(&sd, info, owner, group, dacl, sacl);
    if (error)
        return error;
    BOOL keep = ((info & DACL_SECURITY_INFORMATION) && !(info & (PROTECTED_DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION))) ||
                ((info & SACL_PARTS) && !(info & (PROTECTED_SACL_SECURITY_INFORMATION | UNPROTECTED_SACL_SECURITY_INFORMATION)));
    OBJ *o = nullptr;
    DWORD access = desired_access(type, MODIFY_ACCESS, TRUE, info & 0xFFFF);
    error = obj_open_name(type, name, access, &o);
    if (error && error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
        access = desired_access(type, MODIFY_ACCESS, FALSE, info & 0xFFFF);
        error = obj_open_name(type, name, access, &o);
    }
    if (error)
        return error;
    TREE tree = {};
    tree.Progress = progress;
    tree.Setting = setting;
    tree.Args = args;
    tree.Reset = action == TREE_SEC_INFO_RESET;
    tree.Unprotect = action != TREE_SEC_INFO_SET;
    tree.Owners = info & (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION);
    tree.Owner = owner;
    tree.Group = group;
    do {
        tree.Failed = 0;
        error = set_object(o, info, &sd, keep, &tree);
    } while (report(&tree, name, error, !error) && !tree.Cancelled);
    obj_close(o);
    return error ? error : tree.Failed;
}

ADVAPI DWORD WINAPI TreeSetNamedSecurityInfoW(LPWSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID owner,
                                              PSID group, PACL dacl, PACL sacl, DWORD action, PROGRESS_FN progress,
                                              DWORD setting, PVOID args)
{
    return tree_operation(name, type, info, owner, group, dacl, sacl, action, progress, setting, args);
}

ADVAPI DWORD WINAPI TreeResetNamedSecurityInfoW(LPWSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID owner,
                                                PSID group, PACL dacl, PACL sacl, BOOL keep_explicit,
                                                PROGRESS_FN progress, DWORD setting, PVOID args)
{
    return tree_operation(name, type, info, owner, group, dacl, sacl,
                          keep_explicit ? TREE_SEC_INFO_RESET_KEEP_EXPLICIT : TREE_SEC_INFO_RESET, progress, setting, args);
}

/* the ansi functions' callback gets ansi names too. */
typedef VOID(CALLBACK *PROGRESS_FN_A)(PSTR name, DWORD status, PDWORD setting, PVOID args, BOOL set);

typedef struct ANSI_PROGRESS {
    PROGRESS_FN_A Progress;
    PVOID Args;
} ANSI_PROGRESS;

static VOID CALLBACK ansi_progress(PWSTR name, DWORD status, PDWORD setting, PVOID args, BOOL set)
{
    ANSI_PROGRESS *a = args;
    int bytes = WideCharToMultiByte(CP_ACP, 0, name, -1, nullptr, 0, nullptr, nullptr);
    PSTR narrow = bytes > 0 ? AdvAlloc(bytes) : nullptr;
    if (!narrow)
        return;
    WideCharToMultiByte(CP_ACP, 0, name, -1, narrow, bytes, nullptr, nullptr);
    a->Progress(narrow, status, setting, a->Args, set);
    AdvFree(narrow);
}

ADVAPI DWORD WINAPI TreeSetNamedSecurityInfoA(LPSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID owner,
                                              PSID group, PACL dacl, PACL sacl, DWORD action, PROGRESS_FN_A progress,
                                              DWORD setting, PVOID args)
{
    if (!name)
        return ERROR_INVALID_PARAMETER;
    PWSTR wide = AdvAnsiToWide(name);
    if (!wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    ANSI_PROGRESS a = {progress, args};
    DWORD error = tree_operation(wide, type, info, owner, group, dacl, sacl, action, progress ? ansi_progress : nullptr,
                                 setting, &a);
    AdvFree(wide);
    return error;
}

ADVAPI DWORD WINAPI TreeResetNamedSecurityInfoA(LPSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, PSID owner,
                                                PSID group, PACL dacl, PACL sacl, BOOL keep_explicit,
                                                PROGRESS_FN_A progress, DWORD setting, PVOID args)
{
    return TreeSetNamedSecurityInfoA(name, type, info, owner, group, dacl, sacl,
                                     keep_explicit ? TREE_SEC_INFO_RESET_KEEP_EXPLICIT : TREE_SEC_INFO_RESET, progress,
                                     setting, args);
}

/* ---------------------------------------------------------------- where inherited entries come from */

typedef struct INHERITED_FROMW {
    LONG GenerationGap;
    PWSTR AncestorName;
} INHERITED_FROMW;

typedef struct INHERITED_FROMA {
    LONG GenerationGap;
    PSTR AncestorName;
} INHERITED_FROMA;

/* an entry's sid wherever its type puts it, NULL for types without one. */
static PSID ace_sid(const ACE_HEADER *ace)
{
    if (ace->AceType <= SYSTEM_ALARM_ACE_TYPE || (ace->AceType >= ACCESS_ALLOWED_CALLBACK_ACE_TYPE && ace->AceType <= ACCESS_DENIED_CALLBACK_ACE_TYPE) ||
        ace->AceType == SYSTEM_AUDIT_CALLBACK_ACE_TYPE || ace->AceType == SYSTEM_ALARM_CALLBACK_ACE_TYPE ||
        (ace->AceType >= SYSTEM_MANDATORY_LABEL_ACE_TYPE && ace->AceType <= SYSTEM_ACCESS_FILTER_ACE_TYPE))
        return (PSID)((BYTE *)ace + 8);
    if (ace->AceType == ACCESS_ALLOWED_OBJECT_ACE_TYPE || ace->AceType == ACCESS_DENIED_OBJECT_ACE_TYPE ||
        ace->AceType == SYSTEM_AUDIT_OBJECT_ACE_TYPE || ace->AceType == SYSTEM_ALARM_OBJECT_ACE_TYPE ||
        ace->AceType == ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE || ace->AceType == ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE ||
        ace->AceType == SYSTEM_AUDIT_CALLBACK_OBJECT_ACE_TYPE || ace->AceType == SYSTEM_ALARM_CALLBACK_OBJECT_ACE_TYPE) {
        DWORD flags = *(DWORD *)((BYTE *)ace + 8);
        return (PSID)((BYTE *)ace + 12 + ((flags & ACE_OBJECT_TYPE_PRESENT) ? 16 : 0) +
                      ((flags & ACE_INHERITED_OBJECT_TYPE_PRESENT) ? 16 : 0));
    }
    return nullptr;
}

static BOOL same_entry(const ACE_HEADER *a, ACCESS_MASK a_mask, const ACE_HEADER *b, ACCESS_MASK b_mask)
{
    PSID sa = ace_sid(a), sb = ace_sid(b);
    return a->AceType == b->AceType && a_mask == b_mask && sa && sb && RtlEqualSid(sa, sb);
}

static DWORD get_inheritance_source(PWSTR name, SE_OBJECT_TYPE type, DWORD info, BOOL container, PACL acl,
                                    PGENERIC_MAPPING mapping, INHERITED_FROMW *out)
{
    if (!name || !acl || !out)
        return ERROR_INVALID_PARAMETER;
    if ((info & (DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION)) != DACL_SECURITY_INFORMATION &&
        (info & (DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION)) != SACL_SECURITY_INFORMATION)
        return ERROR_INVALID_PARAMETER;
    if (type != SE_FILE_OBJECT && !is_registry(type))
        return ERROR_INVALID_PARAMETER;
    if (!RtlValidAcl(acl))
        return ERROR_INVALID_ACL;
    DWORD count = acl->AceCount, pending = 0;
    BYTE *matched = AdvAllocZero(count ? count : 1);
    if (!matched)
        return ERROR_NOT_ENOUGH_MEMORY;
    for (DWORD i = 0; i < count; i++) {
        PACE_HEADER ace;
        RtlGetAce(acl, i, (PVOID *)&ace);
        out[i].GenerationGap = 0;
        out[i].AncestorName = nullptr;
        if (ace->AceFlags & INHERITED_ACE)
            pending++;
        else
            matched[i] = 1;
    }
    const GENERIC_MAPPING *generic = mapping ? mapping : mapping_of(type);
    PWSTR current = AdvAlloc((wcslen(name) + 1) * sizeof(WCHAR));
    if (!current) {
        AdvFree(matched);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    wcscpy(current, name);
    DWORD error = ERROR_SUCCESS;
    for (LONG level = 1; pending && !error; level++) {
        PWSTR up = parent_name(current, is_registry(type));
        AdvFree(current);
        current = up;
        if (!current)
            break;
        OBJ *ancestor;
        DWORD wanted = info & (DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION);
        if (obj_open_name(type, current, desired_access(type, READ_ACCESS, FALSE, wanted), &ancestor))
            break;
        BYTE *sd;
        DWORD got = obj_get(ancestor, wanted, &sd);
        obj_close(ancestor);
        if (got)
            break;
        SECURITY_DESCRIPTOR_RELATIVE *r = (SECURITY_DESCRIPTOR_RELATIVE *)sd;
        DWORD offset = (info & DACL_SECURITY_INFORMATION) ? r->Dacl : r->Sacl;
        ACL *list = offset ? (ACL *)(sd + offset) : nullptr;
        for (DWORD a = 0; list && a < list->AceCount && pending; a++) {
            PACE_HEADER source;
            RtlGetAce(list, a, (PVOID *)&source);
            BYTE flags = source->AceFlags;
            if (flags & INHERITED_ACE)
                continue;
            /* what reaches this depth: nothing past a no propagate, and for a leaf only what applies to leaves. */
            if (!(flags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) || (level > 1 && (flags & NO_PROPAGATE_INHERIT_ACE)))
                continue;
            if (level == 1 && !container && !(flags & OBJECT_INHERIT_ACE))
                continue;
            ACCESS_MASK mask = ((ACCESS_ALLOWED_ACE *)source)->Mask;
            ACCESS_MASK mapped = mask;
            RtlMapGenericMask(&mapped, generic);
            for (DWORD i = 0; i < count; i++) {
                if (matched[i])
                    continue;
                PACE_HEADER ace;
                RtlGetAce(acl, i, (PVOID *)&ace);
                ACCESS_MASK own = ((ACCESS_ALLOWED_ACE *)ace)->Mask;
                if (!same_entry(ace, own, source, mapped) && !same_entry(ace, own, source, mask))
                    continue;
                PWSTR copy = LocalAlloc(LMEM_FIXED, (wcslen(current) + 1) * sizeof(WCHAR));
                if (!copy) {
                    error = ERROR_NOT_ENOUGH_MEMORY;
                    break;
                }
                wcscpy(copy, current);
                out[i].GenerationGap = level;
                out[i].AncestorName = copy;
                matched[i] = 1;
                pending--;
                break;
            }
        }
        LocalFree(sd);
    }
    /* an entry no ancestor explains has no source to name. */
    for (DWORD i = 0; i < count; i++)
        if (!matched[i])
            out[i].GenerationGap = -1;
    AdvFree(current);
    AdvFree(matched);
    if (error) {
        for (DWORD i = 0; i < count; i++) {
            if (out[i].AncestorName)
                LocalFree(out[i].AncestorName);
            out[i].AncestorName = nullptr;
        }
    }
    return error;
}

ADVAPI DWORD WINAPI GetInheritanceSourceW(LPWSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, BOOL container,
                                          GUID **guids, DWORD guid_count, PACL acl, PVOID functions,
                                          PGENERIC_MAPPING mapping, INHERITED_FROMW *array)
{
    return get_inheritance_source(name, type, info, container, acl, mapping, array);
}

ADVAPI DWORD WINAPI GetInheritanceSourceA(LPSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, BOOL container,
                                          GUID **guids, DWORD guid_count, PACL acl, PVOID functions,
                                          PGENERIC_MAPPING mapping, INHERITED_FROMA *array)
{
    if (!name || !acl || !array)
        return ERROR_INVALID_PARAMETER;
    PWSTR wide = AdvAnsiToWide(name);
    if (!wide)
        return ERROR_NOT_ENOUGH_MEMORY;
    INHERITED_FROMW *list = AdvAllocZero((acl->AceCount ? acl->AceCount : 1) * sizeof(*list));
    if (!list) {
        AdvFree(wide);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    DWORD error = get_inheritance_source(wide, type, info, container, acl, mapping, list);
    for (DWORD i = 0; !error && i < acl->AceCount; i++) {
        array[i].GenerationGap = list[i].GenerationGap;
        array[i].AncestorName = nullptr;
        if (list[i].AncestorName) {
            int bytes = WideCharToMultiByte(CP_ACP, 0, list[i].AncestorName, -1, nullptr, 0, nullptr, nullptr);
            PSTR narrow = bytes > 0 ? LocalAlloc(LMEM_FIXED, bytes) : nullptr;
            if (narrow)
                WideCharToMultiByte(CP_ACP, 0, list[i].AncestorName, -1, narrow, bytes, nullptr, nullptr);
            else
                error = ERROR_NOT_ENOUGH_MEMORY;
            array[i].AncestorName = narrow;
        }
    }
    for (DWORD i = 0; i < acl->AceCount; i++)
        if (list[i].AncestorName)
            LocalFree(list[i].AncestorName);
    AdvFree(list);
    AdvFree(wide);
    return error;
}

ADVAPI DWORD WINAPI FreeInheritedFromArray(INHERITED_FROMW *array, USHORT count, PVOID functions)
{
    if (!array)
        return ERROR_SUCCESS;
    for (USHORT i = 0; i < count; i++) {
        if (array[i].AncestorName)
            LocalFree(array[i].AncestorName);
        array[i].AncestorName = nullptr;
    }
    return ERROR_SUCCESS;
}

/* ---------------------------------------------------------------- lists as entries */

#define ACTRL_ACCESS_ALLOWED 0x00000001
#define ACTRL_ACCESS_DENIED 0x00000002
#define ACTRL_AUDIT_SUCCESS 0x00000004
#define ACTRL_AUDIT_FAILURE 0x00000008

typedef struct ACTRL_ENTRY {
    TRUSTEE_W Trustee;
    ULONG Flags;
    ULONG Access;
    ULONG ProvSpecificAccess;
    ULONG Inheritance;
    PWSTR InheritProperty;
} ACTRL_ENTRY;

typedef struct ACTRL_LIST {
    ULONG Count;
    ACTRL_ENTRY *Entries;
} ACTRL_LIST;

typedef struct ACTRL_PROPERTY {
    PWSTR Property;
    ACTRL_LIST *List;
    ULONG Flags;
} ACTRL_PROPERTY;

typedef struct ACTRL {
    ULONG Count;
    ACTRL_PROPERTY *Properties;
} ACTRL;

/* a list's entries in one local memory block, each naming its trustee by sid. */
static DWORD entries_from_acl(const ACL *acl, BOOL audit, ACTRL **out)
{
    *out = nullptr;
    if (!acl)
        return ERROR_SUCCESS;
    DWORD count = 0, sid_bytes = 0;
    const BYTE *ace = (const BYTE *)(acl + 1);
    for (DWORD i = 0; i < acl->AceCount; i++, ace += *(const WORD *)(ace + 2)) {
        PSID sid = ace_sid((const ACE_HEADER *)ace);
        BOOL wanted = audit ? ace[0] == SYSTEM_AUDIT_ACE_TYPE : ace[0] <= ACCESS_DENIED_ACE_TYPE;
        if (wanted && sid) {
            count++;
            sid_bytes += (RtlLengthSid(sid) + 7) & ~7u;
        }
    }
    SIZE_T size = sizeof(ACTRL) + sizeof(ACTRL_PROPERTY) + sizeof(ACTRL_LIST) + count * sizeof(ACTRL_ENTRY) + sid_bytes;
    BYTE *block = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, size);
    if (!block)
        return ERROR_NOT_ENOUGH_MEMORY;
    ACTRL *list = (ACTRL *)block;
    ACTRL_PROPERTY *property = (ACTRL_PROPERTY *)(list + 1);
    ACTRL_LIST *entries = (ACTRL_LIST *)(property + 1);
    ACTRL_ENTRY *entry = (ACTRL_ENTRY *)(entries + 1);
    BYTE *sids = (BYTE *)(entry + count);
    list->Count = 1;
    list->Properties = property;
    property->List = entries;
    entries->Count = count;
    entries->Entries = entry;
    ace = (const BYTE *)(acl + 1);
    for (DWORD i = 0; i < acl->AceCount; i++, ace += *(const WORD *)(ace + 2)) {
        PSID sid = ace_sid((const ACE_HEADER *)ace);
        BOOL wanted = audit ? ace[0] == SYSTEM_AUDIT_ACE_TYPE : ace[0] <= ACCESS_DENIED_ACE_TYPE;
        if (!wanted || !sid)
            continue;
        DWORD length = RtlLengthSid(sid);
        memcpy(sids, sid, length);
        entry->Trustee.TrusteeForm = TRUSTEE_IS_SID;
        entry->Trustee.ptstrName = (PWSTR)sids;
        sids += (length + 7) & ~7u;
        entry->Flags = audit ? ((ace[1] & SUCCESSFUL_ACCESS_ACE_FLAG ? ACTRL_AUDIT_SUCCESS : 0) |
                                (ace[1] & FAILED_ACCESS_ACE_FLAG ? ACTRL_AUDIT_FAILURE : 0))
                             : (ace[0] == ACCESS_DENIED_ACE_TYPE ? ACTRL_ACCESS_DENIED : ACTRL_ACCESS_ALLOWED);
        entry->Access = ((const ACCESS_ALLOWED_ACE *)ace)->Mask;
        entry->Inheritance = ace[1] & 0x1F;
        entry++;
    }
    *out = list;
    return ERROR_SUCCESS;
}

/* a sid's account as DOMAIN\name, else its text form, in local memory. */
static PWSTR account_text(PSID sid)
{
    WCHAR name[256], domain[256];
    DWORD name_size = RTL_NUMBER_OF(name), domain_size = RTL_NUMBER_OF(domain);
    SID_NAME_USE use;
    WCHAR text[600];
    if (LookupAccountSidW(nullptr, sid, name, &name_size, domain, &domain_size, &use))
        _snwprintf(text, RTL_NUMBER_OF(text), domain[0] ? L"%ls\\%ls" : L"%ls", domain[0] ? domain : name, name);
    else {
        UNICODE_STRING us;
        if (!sid || !NT_SUCCESS(RtlConvertSidToUnicodeString(&us, sid, TRUE)))
            return nullptr;
        _snwprintf(text, RTL_NUMBER_OF(text), L"%ls", us.Buffer);
        RtlFreeUnicodeString(&us);
    }
    text[RTL_NUMBER_OF(text) - 1] = 0;
    PWSTR copy = LocalAlloc(LMEM_FIXED, (wcslen(text) + 1) * sizeof(WCHAR));
    if (copy)
        wcscpy(copy, text);
    return copy;
}

static DWORD get_security_ex(SE_OBJECT_TYPE type, PCWSTR name, HANDLE handle, DWORD info, ACTRL **access, ACTRL **audit,
                             PWSTR *owner_name, PWSTR *group_name)
{
    PSID owner = nullptr, group = nullptr;
    PACL dacl = nullptr, sacl = nullptr;
    PVOID sd = nullptr;
    info &= OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION;
    DWORD error = get_security(type, name, handle, info, &owner, &group, &dacl, &sacl, &sd);
    if (error)
        return error;
    if (access)
        *access = nullptr;
    if (audit)
        *audit = nullptr;
    if (owner_name)
        *owner_name = nullptr;
    if (group_name)
        *group_name = nullptr;
    if ((info & DACL_SECURITY_INFORMATION) && access)
        error = entries_from_acl(dacl, FALSE, access);
    if (!error && (info & SACL_SECURITY_INFORMATION) && audit)
        error = entries_from_acl(sacl, TRUE, audit);
    if (!error && (info & OWNER_SECURITY_INFORMATION) && owner_name && owner)
        error = (*owner_name = account_text(owner)) ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    if (!error && (info & GROUP_SECURITY_INFORMATION) && group_name && group)
        error = (*group_name = account_text(group)) ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    LocalFree(sd);
    if (error) {
        if (access && *access)
            LocalFree(*access);
        if (audit && *audit)
            LocalFree(*audit);
        if (owner_name && *owner_name)
            LocalFree(*owner_name);
        if (group_name && *group_name)
            LocalFree(*group_name);
        if (access)
            *access = nullptr;
        if (audit)
            *audit = nullptr;
        if (owner_name)
            *owner_name = nullptr;
        if (group_name)
            *group_name = nullptr;
    }
    return error;
}

/* builds a list from entries via the explicit access merge, grants and denies before audits. */
static DWORD acl_from_entries(const ACTRL *list, BOOL audit, PACL *acl)
{
    *acl = nullptr;
    if (!list)
        return ERROR_SUCCESS;
    DWORD count = 0;
    for (ULONG p = 0; p < list->Count; p++)
        if (!list->Properties[p].Property && list->Properties[p].List)
            count += list->Properties[p].List->Count * 2;
    EXPLICIT_ACCESS_W *explicit_entries = count ? AdvAllocZero(count * sizeof(EXPLICIT_ACCESS_W)) : nullptr;
    if (count && !explicit_entries)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD used = 0;
    for (ULONG p = 0; p < list->Count; p++) {
        const ACTRL_LIST *entries = list->Properties[p].Property ? nullptr : list->Properties[p].List;
        for (ULONG i = 0; entries && i < entries->Count; i++) {
            const ACTRL_ENTRY *e = &entries->Entries[i];
            DWORD modes[2], modes_used = 0;
            if (audit) {
                if (e->Flags & ACTRL_AUDIT_SUCCESS)
                    modes[modes_used++] = SET_AUDIT_SUCCESS;
                if (e->Flags & ACTRL_AUDIT_FAILURE)
                    modes[modes_used++] = SET_AUDIT_FAILURE;
            } else if (e->Flags & ACTRL_ACCESS_DENIED) {
                modes[modes_used++] = DENY_ACCESS;
            } else if (e->Flags & ACTRL_ACCESS_ALLOWED) {
                modes[modes_used++] = GRANT_ACCESS;
            }
            for (DWORD m = 0; m < modes_used; m++, used++) {
                explicit_entries[used].grfAccessPermissions = e->Access | e->ProvSpecificAccess;
                explicit_entries[used].grfAccessMode = modes[m];
                explicit_entries[used].grfInheritance = e->Inheritance;
                explicit_entries[used].Trustee = e->Trustee;
            }
        }
    }
    DWORD error = SetEntriesInAclW(used, explicit_entries, nullptr, acl);
    AdvFree(explicit_entries);
    return error;
}

static DWORD account_sid(PCWSTR name, PSID *sid, BYTE *storage)
{
    *sid = nullptr;
    if (!name)
        return ERROR_SUCCESS;
    WCHAR domain[256];
    DWORD sid_size = SECURITY_MAX_SID_SIZE, domain_size = RTL_NUMBER_OF(domain);
    SID_NAME_USE use;
    if (!LookupAccountNameW(nullptr, name, storage, &sid_size, domain, &domain_size, &use))
        return GetLastError();
    *sid = storage;
    return ERROR_SUCCESS;
}

static DWORD set_security_ex(SE_OBJECT_TYPE type, PCWSTR name, HANDLE handle, DWORD info, const ACTRL *access,
                             const ACTRL *audit, PCWSTR owner_name, PCWSTR group_name)
{
    BYTE owner_storage[SECURITY_MAX_SID_SIZE], group_storage[SECURITY_MAX_SID_SIZE];
    PSID owner, group;
    PACL dacl = nullptr, sacl = nullptr;
    DWORD error = ERROR_SUCCESS;
    if (info & OWNER_SECURITY_INFORMATION)
        error = account_sid(owner_name, &owner, owner_storage);
    if (!error && (info & GROUP_SECURITY_INFORMATION))
        error = account_sid(group_name, &group, group_storage);
    if (!error && (info & DACL_SECURITY_INFORMATION))
        error = acl_from_entries(access, FALSE, &dacl);
    if (!error && (info & SACL_SECURITY_INFORMATION))
        error = acl_from_entries(audit, TRUE, &sacl);
    if (!error) {
        info &= OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
                SACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION |
                PROTECTED_SACL_SECURITY_INFORMATION | UNPROTECTED_SACL_SECURITY_INFORMATION;
        if (!(info & OWNER_SECURITY_INFORMATION))
            owner = nullptr;
        if (!(info & GROUP_SECURITY_INFORMATION))
            group = nullptr;
        error = set_security(type, name, handle, info, owner, group, dacl, sacl);
    }
    if (dacl)
        LocalFree(dacl);
    if (sacl)
        LocalFree(sacl);
    return error;
}

ADVAPI DWORD WINAPI GetNamedSecurityInfoExW(LPCWSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, LPCWSTR provider,
                                            LPCWSTR property, ACTRL **access, ACTRL **audit, LPWSTR *owner, LPWSTR *group)
{
    if (!name || !valid_name_type(type))
        return ERROR_INVALID_PARAMETER;
    return get_security_ex(type, name, nullptr, info, access, audit, owner, group);
}

ADVAPI DWORD WINAPI GetSecurityInfoExW(HANDLE handle, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, LPCWSTR provider,
                                       LPCWSTR property, ACTRL **access, ACTRL **audit, LPWSTR *owner, LPWSTR *group)
{
    if (!handle)
        return ERROR_INVALID_HANDLE;
    if (!valid_handle_type(type))
        return ERROR_INVALID_PARAMETER;
    return get_security_ex(type, nullptr, handle, info, access, audit, owner, group);
}

ADVAPI DWORD WINAPI SetNamedSecurityInfoExW(LPCWSTR name, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, LPCWSTR provider,
                                            ACTRL *access, ACTRL *audit, LPWSTR owner, LPWSTR group, ACTRL *old_list)
{
    if (!name || !valid_name_type(type))
        return ERROR_INVALID_PARAMETER;
    return set_security_ex(type, name, nullptr, info, access, audit, owner, group);
}

ADVAPI DWORD WINAPI SetSecurityInfoExW(HANDLE handle, SE_OBJECT_TYPE type, SECURITY_INFORMATION info, LPCWSTR provider,
                                       ACTRL *access, ACTRL *audit, LPWSTR owner, LPWSTR group, ACTRL *old_list)
{
    if (!handle)
        return ERROR_INVALID_HANDLE;
    if (!valid_handle_type(type))
        return ERROR_INVALID_PARAMETER;
    return set_security_ex(type, nullptr, handle, info, access, audit, owner, group);
}
