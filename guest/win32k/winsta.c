/*
 * window stations and desktops as objects of the namespace: window
 * stations live in \Sessions\1\Windows\WindowStations, each with its
 * desktops. a process has a window station and each thread a desktop,
 * taken from the winsta\desktop its creator named, WinSta0\Default when
 * none. mime draws every desktop onto the one host display, so a desktop
 * only matters for the names, handles and rules programs observe.
 */
#include "win32k.h"

#define WINSTA_ROOT L"\\Sessions\\1\\Windows\\WindowStations\\"
#ifndef WINSTA_ALL_ACCESS
#define WINSTA_ALL_ACCESS 0x37F
#endif
#ifndef DESKTOP_ALL_ACCESS
#define DESKTOP_ALL_ACCESS 0x1FF
#endif
#ifndef CWF_CREATE_ONLY
#define CWF_CREATE_ONLY 0x1
#endif
#ifndef WSF_VISIBLE
#define WSF_VISIBLE 0x1
#endif
#ifndef UOI_FLAGS
#define UOI_FLAGS 1
#endif
#ifndef UOI_NAME
#define UOI_NAME 2
#endif
#ifndef UOI_TYPE
#define UOI_TYPE 3
#endif
#ifndef UOI_USER_SID
#define UOI_USER_SID 4
#endif
#ifndef UOI_HEAPSIZE
#define UOI_HEAPSIZE 5
#endif
#ifndef UOI_IO
#define UOI_IO 6
#endif
#ifndef UOI_TIMERPROC_EXCEPTION_SUPPRESSION
#define UOI_TIMERPROC_EXCEPTION_SUPPRESSION 7
#endif
#define DESKTOP_HEAP_KB 20480

typedef struct W32WINSTA {
    LIST_ENTRY Links;
    LIST_ENTRY Desktops;
    ULONG Flags;
    WCHAR Name[64];
} W32WINSTA;

typedef struct W32DESKTOP {
    LIST_ENTRY Links;
    W32WINSTA *WinSta; /* referenced */
    ULONG Flags;
    ULONG HeapKb;
    WCHAR Name[64];
} W32DESKTOP;

static POBJECT_TYPE g_winsta_type;

static ULONG status_error(NTSTATUS status)
{
    switch (status) {
    case STATUS_OBJECT_NAME_INVALID:
        return ERROR_INVALID_NAME;
    case STATUS_OBJECT_NAME_NOT_FOUND:
        return ERROR_FILE_NOT_FOUND;
    case STATUS_OBJECT_PATH_NOT_FOUND:
        return ERROR_PATH_NOT_FOUND;
    case STATUS_ACCESS_VIOLATION:
        return ERROR_NOACCESS;
    case STATUS_INSUFFICIENT_RESOURCES:
    case STATUS_NO_MEMORY:
        return ERROR_NOT_ENOUGH_MEMORY;
    case STATUS_INVALID_HANDLE:
        return ERROR_INVALID_HANDLE;
    default:
        return ERROR_INVALID_PARAMETER;
    }
}
static POBJECT_TYPE g_desktop_type;
static LIST_ENTRY g_winstas = {&g_winstas, &g_winstas};
static W32WINSTA *g_winsta0;
static W32DESKTOP *g_default;

static void delete_desktop(PVOID object)
{
    W32DESKTOP *d = object;
    if (d->WinSta) {
        UserEnter();
        RemoveEntryList(&d->Links);
        UserLeave();
        ObDereferenceObject(d->WinSta);
    }
}

static void types(void)
{
    if (g_winsta_type)
        return;
    OBJECT_TYPE_INITIALIZER init = {};
    init.ValidAccessMask = STANDARD_RIGHTS_REQUIRED | WINSTA_ALL_ACCESS;
    ObSetGenericMapping(&init, 0x20303, 0x2001C, 0x20060, 0xF037F);
    g_winsta_type = ObCreateObjectType(L"WindowStation", &init);
    init.ValidAccessMask = STANDARD_RIGHTS_REQUIRED | DESKTOP_ALL_ACCESS;
    init.DeleteProcedure = delete_desktop;
    ObSetGenericMapping(&init, 0x20041, 0x200BE, 0x20100, 0xF01FF);
    g_desktop_type = ObCreateObjectType(L"Desktop", &init);
}

static void copy_name(WCHAR *out, const WCHAR *name, ULONG chars)
{
    if (chars > 63)
        chars = 63;
    memcpy(out, name, chars * sizeof(WCHAR));
    out[chars] = 0;
}

/* a window station of the namespace, created or opened; *created tells which. */
static NTSTATUS winsta_by_name(PCWSTR name, ULONG chars, BOOLEAN create, BOOLEAN create_only, W32WINSTA **out,
                               BOOLEAN *created)
{
    types();
    WCHAR path[160];
    ULONG prefix = (ULONG)wcslen(WINSTA_ROOT);
    if (!chars || chars > 63)
        return STATUS_OBJECT_NAME_INVALID;
    memcpy(path, WINSTA_ROOT, prefix * sizeof(WCHAR));
    memcpy(path + prefix, name, chars * sizeof(WCHAR));
    path[prefix + chars] = 0;
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    HANDLE h;
    NTSTATUS status = ObOpenObjectByName(&oa, g_winsta_type, KernelMode, WINSTA_ALL_ACCESS, nullptr, &h);
    if (NT_SUCCESS(status)) {
        if (create_only) {
            ZwClose(h);
            return STATUS_OBJECT_NAME_COLLISION;
        }
        status = ObReferenceHandle(h, g_winsta_type, KernelMode, (PVOID *)out, nullptr);
        ZwClose(h);
        if (created)
            *created = FALSE;
        return status;
    }
    if (!create)
        return status;
    W32WINSTA *w;
    status = ObCreateObject(g_winsta_type, nullptr, KernelMode, sizeof(*w), (PVOID *)&w);
    if (!NT_SUCCESS(status))
        return status;
    memset(w, 0, sizeof(*w));
    InitializeListHead(&w->Desktops);
    copy_name(w->Name, name, chars);
    UserEnter();
    InsertTailList(&g_winstas, &w->Links);
    UserLeave();
    oa.Attributes |= OBJ_PERMANENT;
    ObReferenceObject(w);
    status = ObInsertObjectNamed(w, &oa, WINSTA_ALL_ACCESS, KernelMode, &h);
    if (!NT_SUCCESS(status))
        return status;
    ZwClose(h);
    *out = w;
    if (created)
        *created = TRUE;
    return STATUS_SUCCESS;
}

static W32DESKTOP *find_desktop(W32WINSTA *w, PCWSTR name, ULONG chars)
{
    for (PLIST_ENTRY l = w->Desktops.Flink; l != &w->Desktops; l = l->Flink) {
        W32DESKTOP *d = CONTAINING_RECORD(l, W32DESKTOP, Links);
        if (wcslen(d->Name) == chars && !_wcsnicmp(d->Name, name, chars))
            return d;
    }
    return nullptr;
}

/* a desktop of a window station, created when missing; returned referenced. */
static NTSTATUS desktop_in(W32WINSTA *w, PCWSTR name, ULONG chars, BOOLEAN create, W32DESKTOP **out,
                           BOOLEAN *created)
{
    if (!chars || chars > 63)
        return STATUS_OBJECT_NAME_INVALID;
    UserEnter();
    W32DESKTOP *d = find_desktop(w, name, chars);
    if (d)
        ObReferenceObject(d);
    UserLeave();
    if (created)
        *created = FALSE;
    if (d || !create) {
        *out = d;
        return d ? STATUS_SUCCESS : STATUS_OBJECT_NAME_NOT_FOUND;
    }
    NTSTATUS status = ObCreateObject(g_desktop_type, nullptr, KernelMode, sizeof(*d), (PVOID *)&d);
    if (!NT_SUCCESS(status))
        return status;
    memset(d, 0, sizeof(*d));
    copy_name(d->Name, name, chars);
    d->HeapKb = DESKTOP_HEAP_KB;
    ObReferenceObject(w);
    d->WinSta = w;
    UserEnter();
    InsertTailList(&w->Desktops, &d->Links);
    UserLeave();
    *out = d;
    if (created)
        *created = TRUE;
    return STATUS_SUCCESS;
}

/* WinSta0 with its Default and Winlogon desktops, as winlogon makes them, kept for good. */
static void interactive(void)
{
    if (g_winsta0)
        return;
    W32WINSTA *w;
    if (!NT_SUCCESS(winsta_by_name(L"WinSta0", 7, TRUE, FALSE, &w, nullptr)))
        return;
    w->Flags = WSF_VISIBLE;
    W32DESKTOP *d, *logon;
    /* everyone at the console works on the station and its default desktop; the logon desktop is the system's. */
    ObSetInteractiveSecurity(w, TRUE);
    if (NT_SUCCESS(desktop_in(w, L"Default", 7, TRUE, &d, nullptr))) {
        ObSetInteractiveSecurity(d, TRUE);
        g_default = d;
    }
    if (NT_SUCCESS(desktop_in(w, L"Winlogon", 8, TRUE, &logon, nullptr))) {
        ObSetInteractiveSecurity(logon, FALSE);
        ObDereferenceObject(logon);
    }
    g_winsta0 = w;
}

/* the window station and desktop a process starts with, from its winsta\desktop. */
static void start_objects(W32PROCESS *p, W32WINSTA **winsta, W32DESKTOP **desktop)
{
    interactive();
    *winsta = nullptr;
    *desktop = nullptr;
    PCWSTR info = p->DesktopInfo, slash = wcschr(info, L'\\');
    if (info[0] && slash) {
        W32WINSTA *w;
        if (NT_SUCCESS(winsta_by_name(info, (ULONG)(slash - info), FALSE, FALSE, &w, nullptr))) {
            W32DESKTOP *d;
            if (NT_SUCCESS(desktop_in(w, slash + 1, (ULONG)wcslen(slash + 1), FALSE, &d, nullptr))) {
                *winsta = w;
                *desktop = d;
                return;
            }
            ObDereferenceObject(w);
        }
    }
    if (g_winsta0 && g_default) {
        ObReferenceObject(g_winsta0);
        ObReferenceObject(g_default);
        *winsta = g_winsta0;
        *desktop = g_default;
    }
}

static W32WINSTA *process_winsta(W32PROCESS *p)
{
    if (!p->WinSta) {
        W32WINSTA *w;
        W32DESKTOP *d;
        start_objects(p, &w, &d);
        p->WinSta = w;
        if (d)
            ObDereferenceObject(d);
    }
    return p->WinSta;
}

static W32DESKTOP *thread_desktop(W32THREAD *t)
{
    if (!t->Desktop) {
        W32WINSTA *w;
        W32DESKTOP *d;
        start_objects(t->Process, &w, &d);
        if (w)
            ObDereferenceObject(w);
        t->Desktop = d;
    }
    return t->Desktop;
}

/* whether the calling thread works on the desktop that has the input: the default one of WinSta0. */
BOOLEAN WinStaOnInputDesktop(void)
{
    W32THREAD *t = W32GetThread();
    W32DESKTOP *d = t ? thread_desktop(t) : nullptr;
    return !d || !g_default || d == g_default;
}

void WinStaFreeProcess(W32PROCESS *p)
{
    if (p->WinSta)
        ObDereferenceObject(p->WinSta);
    p->WinSta = nullptr;
}

void WinStaFreeThread(W32THREAD *t)
{
    if (t->Desktop)
        ObDereferenceObject(t->Desktop);
    t->Desktop = nullptr;
}

/* the object's own name: the leaf of a relative or full path. */
static NTSTATUS capture_name(POBJECT_ATTRIBUTES user, OBJECT_ATTRIBUTES *oa, UNICODE_STRING *name, PCWSTR *leaf,
                             ULONG *chars)
{
    NTSTATUS status = ObCaptureObjectAttributes(user, oa, name);
    if (!NT_SUCCESS(status))
        return status;
    PCWSTR s = name->Buffer;
    ULONG n = name->Length / sizeof(WCHAR);
    for (ULONG i = n; i > 0; i--) {
        if (s[i - 1] == L'\\') {
            s += i;
            n -= i;
            break;
        }
    }
    *leaf = s;
    *chars = n;
    return STATUS_SUCCESS;
}

static void free_name(UNICODE_STRING *name)
{
    if (name->Buffer)
        ExFreePool(name->Buffer);
}

/* generic rights as the rights of a window station or a desktop, in windows' mappings. */
static ACCESS_MASK map_access(PVOID object, ACCESS_MASK access)
{
    BOOLEAN station = ObGetObjectType(object) == g_winsta_type;
    ACCESS_MASK all = station ? WINSTA_ALL_ACCESS : DESKTOP_ALL_ACCESS;
    if (access & (MAXIMUM_ALLOWED | GENERIC_ALL))
        return all | 0xF0000;
    ACCESS_MASK mapped = access & (all | 0x1F0000);
    if (access & GENERIC_READ)
        mapped |= 0x20000 | (station ? 0x1 | 0x2 | 0x100 | 0x200 : 0x1 | 0x40);
    if (access & GENERIC_WRITE)
        mapped |= 0x20000 | (station ? 0x4 | 0x8 | 0x10 : 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x80);
    if (access & GENERIC_EXECUTE)
        mapped |= 0x20000 | (station ? 0x20 | 0x40 : 0x100);
    return mapped;
}

static HANDLE insert(PVOID object, ACCESS_MASK access, ULONG attributes)
{
    HANDLE h = nullptr;
    ObReferenceObject(object);
    NTSTATUS status = ObInsertObject(object, map_access(object, access), attributes & OBJ_INHERIT, &h);
    if (!NT_SUCCESS(status)) {
        W32SetLastError(status_error(status));
        return nullptr;
    }
    return h;
}

/* a handle to a window station or desktop that was there: the descriptor says what the caller may have of it. */
static HANDLE insert_open(PVOID object, ACCESS_MASK access, ULONG attributes)
{
    ACCESS_MASK granted;
    NTSTATUS status = ObCheckOpenAccess(object, access, UserMode, &granted);
    if (!NT_SUCCESS(status)) {
        W32SetLastError(status_error(status));
        return nullptr;
    }
    return insert(object, granted, attributes);
}

/* the descriptor of one the caller made, from the security attributes it gave. */
static BOOL assign_new(PVOID object, OBJECT_ATTRIBUTES *oa)
{
    NTSTATUS status = ObAssignSecurity(object, oa, nullptr, UserMode);
    if (!NT_SUCCESS(status)) {
        W32SetLastError(status_error(status));
        return FALSE;
    }
    return TRUE;
}

HWINSTA NTAPI NtUserCreateWindowStation(POBJECT_ATTRIBUTES attributes, ACCESS_MASK access, HANDLE keyboard,
                                        DWORD offset, PVOID table, PUNICODE_STRING klid, DWORD locale)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name = {};
    PCWSTR leaf;
    ULONG chars;
    NTSTATUS status = capture_name(attributes, &oa, &name, &leaf, &chars);
    if (!NT_SUCCESS(status)) {
        W32SetLastError(status_error(status));
        return nullptr;
    }
    /* an unnamed window station is the one of the caller's logon session. */
    static const WCHAR service[] = L"Service-0x0-3e8d2$";
    if (!chars) {
        leaf = service;
        chars = RTL_NUMBER_OF(service) - 1;
    }
    W32WINSTA *w;
    BOOLEAN created;
    status = winsta_by_name(leaf, chars, TRUE, (oa.Attributes & OBJ_OPENIF) == 0 && (locale & CWF_CREATE_ONLY),
                            &w, &created);
    free_name(&name);
    if (!NT_SUCCESS(status)) {
        W32SetLastError(status == STATUS_OBJECT_NAME_COLLISION ? ERROR_ALREADY_EXISTS : status_error(status));
        return nullptr;
    }
    HANDLE h = created ? insert(w, access, oa.Attributes) : insert_open(w, access, oa.Attributes);
    ObDereferenceObject(w);
    if (h)
        W32SetLastError(created ? ERROR_SUCCESS : ERROR_ALREADY_EXISTS);
    return (HWINSTA)h;
}

HWINSTA NTAPI NtUserCreateWindowStationEx(POBJECT_ATTRIBUTES attributes, ACCESS_MASK access, HANDLE keyboard,
                                          DWORD offset, PVOID table, PUNICODE_STRING klid, DWORD locale)
{
    return NtUserCreateWindowStation(attributes, access, keyboard, offset, table, klid, locale);
}

HWINSTA NTAPI NtUserOpenWindowStation(POBJECT_ATTRIBUTES attributes, ACCESS_MASK access)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name = {};
    PCWSTR leaf;
    ULONG chars;
    NTSTATUS status = capture_name(attributes, &oa, &name, &leaf, &chars);
    if (!NT_SUCCESS(status)) {
        W32SetLastError(status_error(status));
        return nullptr;
    }
    interactive();
    W32WINSTA *w;
    status = winsta_by_name(leaf, chars, FALSE, FALSE, &w, nullptr);
    free_name(&name);
    if (!NT_SUCCESS(status)) {
        W32SetLastError(ERROR_FILE_NOT_FOUND);
        return nullptr;
    }
    HANDLE h = insert_open(w, access, oa.Attributes);
    ObDereferenceObject(w);
    return (HWINSTA)h;
}

/* the window station a desktop call names: its root handle, else the caller's own. */
static W32WINSTA *root_winsta(HANDLE root)
{
    W32WINSTA *w = nullptr;
    if (root && NT_SUCCESS(ObReferenceHandle(root, g_winsta_type, UserMode, (PVOID *)&w, nullptr)))
        return w;
    UserEnter();
    w = process_winsta(W32GetProcess());
    if (w)
        ObReferenceObject(w);
    UserLeave();
    return w;
}

HDESK NTAPI NtUserCreateDesktopEx(POBJECT_ATTRIBUTES attributes, PUNICODE_STRING device, PVOID devmode, DWORD flags,
                                  ACCESS_MASK access, ULONG heap_kb)
{
    types();
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name = {};
    PCWSTR leaf;
    ULONG chars;
    NTSTATUS status = capture_name(attributes, &oa, &name, &leaf, &chars);
    if (!NT_SUCCESS(status)) {
        W32SetLastError(status_error(status));
        return nullptr;
    }
    W32WINSTA *w = root_winsta(oa.RootDirectory);
    W32DESKTOP *d = nullptr;
    BOOLEAN created = FALSE;
    status = w ? desktop_in(w, leaf, chars, TRUE, &d, &created) : STATUS_INVALID_HANDLE;
    free_name(&name);
    if (w)
        ObDereferenceObject(w);
    if (!NT_SUCCESS(status)) {
        W32SetLastError(status_error(status));
        return nullptr;
    }
    if (created) {
        d->Flags = flags;
        if (heap_kb)
            d->HeapKb = heap_kb;
        /* what the caller gave as the security of the desktop is its descriptor; a refusal leaves no desktop. */
        if (!assign_new(d, &oa)) {
            ObDereferenceObject(d);
            return nullptr;
        }
    }
    HANDLE h = created ? insert(d, access, oa.Attributes) : insert_open(d, access, oa.Attributes);
    ObDereferenceObject(d);
    if (h)
        W32SetLastError(created ? ERROR_SUCCESS : ERROR_ALREADY_EXISTS);
    return (HDESK)h;
}

HDESK NTAPI NtUserOpenDesktop(POBJECT_ATTRIBUTES attributes, DWORD flags, ACCESS_MASK access)
{
    types();
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name = {};
    PCWSTR leaf;
    ULONG chars;
    NTSTATUS status = capture_name(attributes, &oa, &name, &leaf, &chars);
    if (!NT_SUCCESS(status)) {
        W32SetLastError(status_error(status));
        return nullptr;
    }
    W32WINSTA *w = root_winsta(oa.RootDirectory);
    W32DESKTOP *d = nullptr;
    status = w ? desktop_in(w, leaf, chars, FALSE, &d, nullptr) : STATUS_INVALID_HANDLE;
    free_name(&name);
    if (w)
        ObDereferenceObject(w);
    if (!NT_SUCCESS(status)) {
        W32SetLastError(ERROR_FILE_NOT_FOUND);
        return nullptr;
    }
    HANDLE h = insert_open(d, access, oa.Attributes);
    ObDereferenceObject(d);
    return (HDESK)h;
}

HDESK NTAPI NtUserOpenInputDesktop(DWORD flags, BOOL inherit, ACCESS_MASK access)
{
    types();
    interactive();
    if (!g_default) {
        W32SetLastError(ERROR_ACCESS_DENIED);
        return nullptr;
    }
    return (HDESK)insert_open(g_default, access, inherit ? OBJ_INHERIT : 0);
}

HWINSTA NTAPI NtUserGetProcessWindowStation(void)
{
    types();
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    W32WINSTA *w = p ? process_winsta(p) : nullptr;
    if (w && !p->WinStaHandle)
        p->WinStaHandle = insert(w, 0xF0000 | WINSTA_ALL_ACCESS, 0);
    HANDLE h = p ? p->WinStaHandle : nullptr;
    UserLeave();
    return (HWINSTA)h;
}

BOOL NTAPI NtUserSetProcessWindowStation(HWINSTA handle)
{
    types();
    W32WINSTA *w;
    NTSTATUS status = ObReferenceHandle(handle, g_winsta_type, UserMode, (PVOID *)&w, nullptr);
    if (!NT_SUCCESS(status)) {
        W32SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    if (p->WinSta)
        ObDereferenceObject(p->WinSta);
    p->WinSta = w;
    p->WinStaHandle = handle;
    UserLeave();
    return TRUE;
}

HDESK NTAPI NtUserGetThreadDesktop(DWORD tid, HDESK console)
{
    types();
    UserEnter();
    W32THREAD *t = tid ? W32LookupThread((HANDLE)(ULONG_PTR)tid) : nullptr;
    HANDLE h = nullptr;
    if (!t || t->Process != W32GetProcess()) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
    } else {
        W32DESKTOP *d = thread_desktop(t);
        if (d && !t->DesktopHandle)
            t->DesktopHandle = insert(d, 0xF0000 | DESKTOP_ALL_ACCESS, 0);
        h = t->DesktopHandle;
    }
    UserLeave();
    return (HDESK)h;
}

/* a thread with windows or hooks keeps its desktop, as on windows. */
BOOL NTAPI NtUserSetThreadDesktop(HDESK handle)
{
    types();
    W32DESKTOP *d;
    NTSTATUS status = ObReferenceHandle(handle, g_desktop_type, UserMode, (PVOID *)&d, nullptr);
    if (!NT_SUCCESS(status)) {
        W32SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    UserEnter();
    W32THREAD *t = W32GetThread();
    BOOL ok = TRUE;
    if (t->Desktop != d && WndThreadHasWindows(t)) {
        ok = FALSE;
    } else {
        if (t->Desktop)
            ObDereferenceObject(t->Desktop);
        t->Desktop = d;
        t->DesktopHandle = handle;
        d = nullptr;
    }
    UserLeave();
    if (d)
        ObDereferenceObject(d);
    if (!ok)
        W32SetLastError(ERROR_BUSY);
    return ok;
}

BOOL NTAPI NtUserCloseWindowStation(HWINSTA handle)
{
    types();
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    BOOL current = p && p->WinStaHandle == (HANDLE)handle;
    UserLeave();
    if (current) {
        W32SetLastError(ERROR_BUSY);
        return FALSE;
    }
    W32WINSTA *w;
    if (!NT_SUCCESS(ObReferenceHandle(handle, g_winsta_type, UserMode, (PVOID *)&w, nullptr))) {
        W32SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    ObDereferenceObject(w);
    return NT_SUCCESS(ZwClose(handle));
}

BOOL NTAPI NtUserCloseDesktop(HDESK handle)
{
    types();
    UserEnter();
    BOOL current = W32GetThread()->DesktopHandle == (HANDLE)handle;
    UserLeave();
    if (current) {
        W32SetLastError(ERROR_BUSY);
        return FALSE;
    }
    W32DESKTOP *d;
    if (!NT_SUCCESS(ObReferenceHandle(handle, g_desktop_type, UserMode, (PVOID *)&d, nullptr))) {
        W32SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    ObDereferenceObject(d);
    return NT_SUCCESS(ZwClose(handle));
}

BOOL NTAPI NtUserSwitchDesktop(HDESK handle, DWORD flags, DWORD fade)
{
    types();
    W32DESKTOP *d;
    if (!NT_SUCCESS(ObReferenceHandle(handle, g_desktop_type, UserMode, (PVOID *)&d, nullptr))) {
        W32SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    BOOL ok = d->WinSta == g_winsta0;
    ObDereferenceObject(d);
    if (!ok)
        W32SetLastError(ERROR_ACCESS_DENIED);
    return ok;
}

static BOOL copy_out(PVOID info, DWORD length, const void *data, DWORD size, PDWORD needed)
{
    if (needed && !NT_SUCCESS(MimeCopyToUser(needed, &size, sizeof(size)))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    if (length < size) {
        W32SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (size && !NT_SUCCESS(MimeCopyToUser(info, data, size))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    return TRUE;
}

BOOL NTAPI NtUserGetObjectInformation(HANDLE handle, DWORD index, PVOID info, DWORD length, PDWORD needed)
{
    types();
    PVOID object;
    W32WINSTA *w = nullptr;
    W32DESKTOP *d = nullptr;
    if (NT_SUCCESS(ObReferenceHandle(handle, g_winsta_type, UserMode, &object, nullptr)))
        w = object;
    else if (NT_SUCCESS(ObReferenceHandle(handle, g_desktop_type, UserMode, &object, nullptr)))
        d = object;
    else {
        W32SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    BOOL ok;
    switch (index) {
    case UOI_FLAGS: {
        ULONG flags[3] = {FALSE, FALSE, w ? w->Flags : d->Flags};
        ok = copy_out(info, length, flags, sizeof(flags), needed);
        break;
    }
    case UOI_NAME: {
        PCWSTR name = w ? w->Name : d->Name;
        ok = copy_out(info, length, name, (DWORD)(wcslen(name) + 1) * sizeof(WCHAR), needed);
        break;
    }
    case UOI_TYPE: {
        PCWSTR type = w ? L"WindowStation" : L"Desktop";
        ok = copy_out(info, length, type, (DWORD)(wcslen(type) + 1) * sizeof(WCHAR), needed);
        break;
    }
    case UOI_USER_SID:
        /* no user is logged on to a window station of its own here. */
        ok = copy_out(info, length, nullptr, 0, needed);
        break;
    case UOI_HEAPSIZE: {
        ULONG kb = d ? d->HeapKb : 0;
        ok = copy_out(info, length, &kb, sizeof(kb), needed);
        break;
    }
    case UOI_IO: {
        BOOL io = d ? d == g_default : w == g_winsta0;
        ok = copy_out(info, length, &io, sizeof(io), needed);
        break;
    }
    case UOI_TIMERPROC_EXCEPTION_SUPPRESSION: {
        BOOL suppress = TRUE;
        ok = copy_out(info, length, &suppress, sizeof(suppress), needed);
        break;
    }
    default:
        W32SetLastError(ERROR_INVALID_PARAMETER);
        ok = FALSE;
        break;
    }
    ObDereferenceObject(object);
    return ok;
}

BOOL NTAPI NtUserSetObjectInformation(HANDLE handle, DWORD index, PVOID info, DWORD length)
{
    types();
    PVOID object;
    BOOL winsta = FALSE;
    if (NT_SUCCESS(ObReferenceHandle(handle, g_winsta_type, UserMode, &object, nullptr)))
        winsta = TRUE;
    else if (!NT_SUCCESS(ObReferenceHandle(handle, g_desktop_type, UserMode, &object, nullptr))) {
        W32SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    BOOL ok = FALSE;
    ULONG flags[3];
    if (index == UOI_FLAGS && length >= sizeof(flags) && NT_SUCCESS(MimeCopyFromUser(flags, info, sizeof(flags)))) {
        if (winsta)
            ((W32WINSTA *)object)->Flags = flags[2];
        else
            ((W32DESKTOP *)object)->Flags = flags[2];
        ok = TRUE;
    } else if (index == UOI_TIMERPROC_EXCEPTION_SUPPRESSION) {
        ok = TRUE;
    } else {
        W32SetLastError(ERROR_INVALID_PARAMETER);
    }
    ObDereferenceObject(object);
    return ok;
}

/*
 * the names of the window stations, or of a window station's desktops, as
 * a byte count, a name count and the names one after the other.
 */
NTSTATUS NTAPI NtUserBuildNameList(HWINSTA handle, ULONG length, PVOID buffer, PULONG needed)
{
    types();
    interactive();
    W32WINSTA *w = nullptr;
    if (handle && !NT_SUCCESS(ObReferenceHandle(handle, g_winsta_type, UserMode, (PVOID *)&w, nullptr)))
        return STATUS_INVALID_HANDLE;
    ULONG size, count = 0;
    BYTE *list = W32Alloc(4096);
    if (!list) {
        if (w)
            ObDereferenceObject(w);
        return STATUS_NO_MEMORY;
    }
    WCHAR *names = (WCHAR *)(list + 8);
    ULONG at = 0;
    UserEnter();
    if (w) {
        for (PLIST_ENTRY l = w->Desktops.Flink; l != &w->Desktops; l = l->Flink) {
            W32DESKTOP *d = CONTAINING_RECORD(l, W32DESKTOP, Links);
            ULONG n = (ULONG)wcslen(d->Name) + 1;
            if ((at + n + 1) * sizeof(WCHAR) + 8 <= 4096) {
                memcpy(names + at, d->Name, n * sizeof(WCHAR));
                at += n;
                count++;
            }
        }
    } else {
        for (PLIST_ENTRY l = g_winstas.Flink; l != &g_winstas; l = l->Flink) {
            W32WINSTA *each = CONTAINING_RECORD(l, W32WINSTA, Links);
            ULONG n = (ULONG)wcslen(each->Name) + 1;
            if ((at + n + 1) * sizeof(WCHAR) + 8 <= 4096) {
                memcpy(names + at, each->Name, n * sizeof(WCHAR));
                at += n;
                count++;
            }
        }
    }
    UserLeave();
    names[at] = 0;
    size = 8 + (at + 1) * sizeof(WCHAR);
    ((ULONG *)list)[0] = size;
    ((ULONG *)list)[1] = count;
    NTSTATUS status = STATUS_SUCCESS;
    if (needed)
        status = MimeCopyToUser(needed, &size, sizeof(size));
    if (NT_SUCCESS(status))
        status = length < size ? STATUS_BUFFER_TOO_SMALL : MimeCopyToUser(buffer, list, size);
    W32Free(list);
    if (w)
        ObDereferenceObject(w);
    return status;
}

#define GR_GDIOBJECTS 0
#define GR_USEROBJECTS 1
#define GR_GDIOBJECTS_PEAK 2
#define GR_USEROBJECTS_PEAK 4

/*
 * the gdi and user objects of a process, GR_GLOBAL counting the caller's. peaks are the largest
 * counts seen by these queries, a guess at what windows tracks.
 */
DWORD NTAPI NtUserGetGuiResources(HANDLE process, DWORD flags)
{
    static ULONG peaks[2];
    HANDLE pid = PsGetCurrentProcessId();
    if (process != (HANDLE)(LONG_PTR)-2) {
        PEPROCESS target;
        if (!NT_SUCCESS(ObReferenceObjectByHandle(process, 0x1000, *PsProcessType, UserMode, (PVOID *)&target,
                                                  nullptr))) {
            W32SetLastError(ERROR_INVALID_HANDLE);
            return 0;
        }
        pid = PsGetProcessId(target);
        ObDereferenceObject(target);
    }
    if (flags != GR_GDIOBJECTS && flags != GR_USEROBJECTS && flags != GR_GDIOBJECTS_PEAK &&
        flags != GR_USEROBJECTS_PEAK) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UserEnter();
    ULONG n = 0;
    if (flags == GR_GDIOBJECTS || flags == GR_GDIOBJECTS_PEAK) {
        n = GdiCountProcessObjects(pid);
        if (n > peaks[0])
            peaks[0] = n;
        if (flags == GR_GDIOBJECTS_PEAK)
            n = peaks[0];
    } else {
        for (PLIST_ENTRY l = W32Processes.Flink; l != &W32Processes; l = l->Flink) {
            W32PROCESS *p = CONTAINING_RECORD(l, W32PROCESS, Links);
            if (p->Pid == pid)
                n = WndCountProcessObjects(p);
        }
        if (n > peaks[1])
            peaks[1] = n;
        if (flags == GR_USEROBJECTS_PEAK)
            n = peaks[1];
    }
    UserLeave();
    return n;
}
