/*
 * tool help snapshots, registered waits, volume names, geographical
 * information and the thread context and device control wrappers.
 */
#include "kernelbase_private.h"

/* ------------------------------------------------------------------ tool help snapshots */

#define TH32CS_SNAPHEAPLIST 0x00000001
#define TH32CS_SNAPPROCESS 0x00000002
#define TH32CS_SNAPTHREAD 0x00000004
#define TH32CS_SNAPMODULE 0x00000008
#define TH32CS_SNAPMODULE32 0x00000010
#define MAX_MODULE_NAME32 255

typedef struct tagMODULEENTRY32W {
    DWORD dwSize;
    DWORD th32ModuleID;
    DWORD th32ProcessID;
    DWORD GlblcntUsage;
    DWORD ProccntUsage;
    BYTE *modBaseAddr;
    DWORD modBaseSize;
    HMODULE hModule;
    WCHAR szModule[MAX_MODULE_NAME32 + 1];
    WCHAR szExePath[MAX_PATH];
} MODULEENTRY32W, *LPMODULEENTRY32W;

typedef struct tagMODULEENTRY32 {
    DWORD dwSize;
    DWORD th32ModuleID;
    DWORD th32ProcessID;
    DWORD GlblcntUsage;
    DWORD ProccntUsage;
    BYTE *modBaseAddr;
    DWORD modBaseSize;
    HMODULE hModule;
    char szModule[MAX_MODULE_NAME32 + 1];
    char szExePath[MAX_PATH];
} MODULEENTRY32, *LPMODULEENTRY32;

typedef struct tagPROCESSENTRY32W {
    DWORD dwSize;
    DWORD cntUsage;
    DWORD th32ProcessID;
    ULONG_PTR th32DefaultHeapID;
    DWORD th32ModuleID;
    DWORD cntThreads;
    DWORD th32ParentProcessID;
    LONG pcPriClassBase;
    DWORD dwFlags;
    WCHAR szExeFile[MAX_PATH];
} PROCESSENTRY32W, *LPPROCESSENTRY32W;

typedef struct tagPROCESSENTRY32 {
    DWORD dwSize;
    DWORD cntUsage;
    DWORD th32ProcessID;
    ULONG_PTR th32DefaultHeapID;
    DWORD th32ModuleID;
    DWORD cntThreads;
    DWORD th32ParentProcessID;
    LONG pcPriClassBase;
    DWORD dwFlags;
    char szExeFile[MAX_PATH];
} PROCESSENTRY32, *LPPROCESSENTRY32;

typedef struct tagTHREADENTRY32 {
    DWORD dwSize;
    DWORD cntUsage;
    DWORD th32ThreadID;
    DWORD th32OwnerProcessID;
    LONG tpBasePri;
    LONG tpDeltaPri;
    DWORD dwFlags;
} THREADENTRY32, *LPTHREADENTRY32;

/* the snapshot lives in a section, like windows keeps it, so the handle can be duplicated. */
typedef struct SNAPSHOT {
    ULONG ModuleCount;
    ULONG ProcessCount;
    ULONG ThreadCount;
    ULONG ModulePos;
    ULONG ProcessPos;
    ULONG ThreadPos;
    MODULEENTRY32W Modules[64];
    PROCESSENTRY32W Processes[1];
    THREADENTRY32 Threads[1];
} SNAPSHOT;

static void fill_modules(SNAPSHOT *s, DWORD pid)
{
    PPEB_LDR_DATA ldr = KbPeb()->Ldr;
    RtlEnterCriticalSection(KbPeb()->LoaderLock);
    for (PLIST_ENTRY l = ldr->InLoadOrderModuleList.Flink; l != &ldr->InLoadOrderModuleList && s->ModuleCount < 64;
         l = l->Flink) {
        PLDR_DATA_TABLE_ENTRY e = CONTAINING_RECORD(l, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        MODULEENTRY32W *m = &s->Modules[s->ModuleCount++];
        m->dwSize = sizeof(*m);
        m->th32ModuleID = 1;
        m->th32ProcessID = pid;
        m->GlblcntUsage = 0xFFFF;
        m->ProccntUsage = 0xFFFF;
        m->modBaseAddr = e->DllBase;
        m->modBaseSize = e->SizeOfImage;
        m->hModule = e->DllBase;
        SIZE_T n = e->BaseDllName.Length / sizeof(WCHAR);
        if (n > MAX_MODULE_NAME32)
            n = MAX_MODULE_NAME32;
        memcpy(m->szModule, e->BaseDllName.Buffer, n * sizeof(WCHAR));
        n = e->FullDllName.Length / sizeof(WCHAR);
        if (n >= MAX_PATH)
            n = MAX_PATH - 1;
        memcpy(m->szExePath, e->FullDllName.Buffer, n * sizeof(WCHAR));
    }
    RtlLeaveCriticalSection(KbPeb()->LoaderLock);
}

WINBASEAPI HANDLE WINAPI CreateToolhelp32Snapshot(DWORD flags, DWORD pid)
{
    DWORD self = GetCurrentProcessId();
    if (!pid)
        pid = self;
    LARGE_INTEGER size;
    size.QuadPart = sizeof(SNAPSHOT);
    HANDLE section;
    NTSTATUS status = NtCreateSection(&section, SECTION_ALL_ACCESS, NULL, &size, PAGE_READWRITE, SEC_COMMIT, NULL);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return INVALID_HANDLE_VALUE;
    }
    PVOID view = NULL;
    SIZE_T view_size = 0;
    status = NtMapViewOfSection(section, NtCurrentProcess(), &view, 0, 0, NULL, &view_size, ViewShare, 0, PAGE_READWRITE);
    if (!NT_SUCCESS(status)) {
        NtClose(section);
        KbSetStatus(status);
        return INVALID_HANDLE_VALUE;
    }
    SNAPSHOT *s = view;
    /* only the calling process is visible: other processes cannot be enumerated yet. */
    if ((flags & (TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32)) && pid == self)
        fill_modules(s, pid);
    if (flags & TH32CS_SNAPPROCESS) {
        PROCESSENTRY32W *p = &s->Processes[0];
        p->dwSize = sizeof(*p);
        p->th32ProcessID = self;
        p->cntThreads = 1;
        p->pcPriClassBase = 8;
        PCWSTR image = KbParams()->ImagePathName.Buffer;
        PCWSTR name = image ? wcsrchr(image, '\\') : NULL;
        name = name ? name + 1 : image ? image : L"";
        SIZE_T n = wcslen(name);
        if (n >= MAX_PATH)
            n = MAX_PATH - 1;
        memcpy(p->szExeFile, name, n * sizeof(WCHAR));
        s->ProcessCount = 1;
    }
    if (flags & TH32CS_SNAPTHREAD) {
        THREADENTRY32 *t = &s->Threads[0];
        t->dwSize = sizeof(*t);
        t->th32ThreadID = GetCurrentThreadId();
        t->th32OwnerProcessID = self;
        t->tpBasePri = 8;
        s->ThreadCount = 1;
    }
    NtUnmapViewOfSection(NtCurrentProcess(), view);
    return section;
}

/* maps the snapshot behind a handle; NULL with the last error set when it is not one. */
static SNAPSHOT *map_snapshot(HANDLE handle)
{
    PVOID view = NULL;
    SIZE_T size = 0;
    NTSTATUS status = NtMapViewOfSection(handle, NtCurrentProcess(), &view, 0, 0, NULL, &size, ViewShare, 0,
                                         PAGE_READWRITE);
    if (!NT_SUCCESS(status) || size < sizeof(SNAPSHOT)) {
        if (NT_SUCCESS(status))
            NtUnmapViewOfSection(NtCurrentProcess(), view);
        KbSetError(ERROR_INVALID_HANDLE);
        return NULL;
    }
    return view;
}

static void unmap_snapshot(SNAPSHOT *s)
{
    NtUnmapViewOfSection(NtCurrentProcess(), s);
}

static BOOL next_module(HANDLE handle, LPMODULEENTRY32W entry, BOOL first)
{
    if (!entry || entry->dwSize < sizeof(*entry)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SNAPSHOT *s = map_snapshot(handle);
    if (!s)
        return FALSE;
    if (first)
        s->ModulePos = 0;
    BOOL ok = s->ModulePos < s->ModuleCount;
    if (ok)
        *entry = s->Modules[s->ModulePos++];
    else
        KbSetError(ERROR_NO_MORE_FILES);
    unmap_snapshot(s);
    return ok;
}

WINBASEAPI BOOL WINAPI Module32FirstW(HANDLE snapshot, LPMODULEENTRY32W entry)
{
    return next_module(snapshot, entry, TRUE);
}

WINBASEAPI BOOL WINAPI Module32NextW(HANDLE snapshot, LPMODULEENTRY32W entry)
{
    return next_module(snapshot, entry, FALSE);
}

static BOOL next_module_a(HANDLE handle, LPMODULEENTRY32 entry, BOOL first)
{
    if (!entry || entry->dwSize < sizeof(*entry)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    MODULEENTRY32W w;
    w.dwSize = sizeof(w);
    if (!next_module(handle, &w, first))
        return FALSE;
    entry->th32ModuleID = w.th32ModuleID;
    entry->th32ProcessID = w.th32ProcessID;
    entry->GlblcntUsage = w.GlblcntUsage;
    entry->ProccntUsage = w.ProccntUsage;
    entry->modBaseAddr = w.modBaseAddr;
    entry->modBaseSize = w.modBaseSize;
    entry->hModule = w.hModule;
    WideCharToMultiByte(CP_ACP, 0, w.szModule, -1, entry->szModule, sizeof(entry->szModule), NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, w.szExePath, -1, entry->szExePath, sizeof(entry->szExePath), NULL, NULL);
    return TRUE;
}

WINBASEAPI BOOL WINAPI Module32First(HANDLE snapshot, LPMODULEENTRY32 entry)
{
    return next_module_a(snapshot, entry, TRUE);
}

WINBASEAPI BOOL WINAPI Module32Next(HANDLE snapshot, LPMODULEENTRY32 entry)
{
    return next_module_a(snapshot, entry, FALSE);
}

static BOOL next_process(HANDLE handle, LPPROCESSENTRY32W entry, BOOL first)
{
    if (!entry || entry->dwSize < sizeof(*entry)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SNAPSHOT *s = map_snapshot(handle);
    if (!s)
        return FALSE;
    if (first)
        s->ProcessPos = 0;
    BOOL ok = s->ProcessPos < s->ProcessCount;
    if (ok)
        *entry = s->Processes[s->ProcessPos++];
    else
        KbSetError(ERROR_NO_MORE_FILES);
    unmap_snapshot(s);
    return ok;
}

WINBASEAPI BOOL WINAPI Process32FirstW(HANDLE snapshot, LPPROCESSENTRY32W entry)
{
    return next_process(snapshot, entry, TRUE);
}

WINBASEAPI BOOL WINAPI Process32NextW(HANDLE snapshot, LPPROCESSENTRY32W entry)
{
    return next_process(snapshot, entry, FALSE);
}

static BOOL next_process_a(HANDLE handle, LPPROCESSENTRY32 entry, BOOL first)
{
    if (!entry || entry->dwSize < sizeof(*entry)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PROCESSENTRY32W w;
    w.dwSize = sizeof(w);
    if (!next_process(handle, &w, first))
        return FALSE;
    memcpy(entry, &w, offsetof(PROCESSENTRY32, szExeFile));
    entry->dwSize = sizeof(*entry);
    WideCharToMultiByte(CP_ACP, 0, w.szExeFile, -1, entry->szExeFile, sizeof(entry->szExeFile), NULL, NULL);
    return TRUE;
}

WINBASEAPI BOOL WINAPI Process32First(HANDLE snapshot, LPPROCESSENTRY32 entry)
{
    return next_process_a(snapshot, entry, TRUE);
}

WINBASEAPI BOOL WINAPI Process32Next(HANDLE snapshot, LPPROCESSENTRY32 entry)
{
    return next_process_a(snapshot, entry, FALSE);
}

static BOOL next_thread(HANDLE handle, LPTHREADENTRY32 entry, BOOL first)
{
    if (!entry || entry->dwSize < sizeof(*entry)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SNAPSHOT *s = map_snapshot(handle);
    if (!s)
        return FALSE;
    if (first)
        s->ThreadPos = 0;
    BOOL ok = s->ThreadPos < s->ThreadCount;
    if (ok)
        *entry = s->Threads[s->ThreadPos++];
    else
        KbSetError(ERROR_NO_MORE_FILES);
    unmap_snapshot(s);
    return ok;
}

WINBASEAPI BOOL WINAPI Thread32First(HANDLE snapshot, LPTHREADENTRY32 entry)
{
    return next_thread(snapshot, entry, TRUE);
}

WINBASEAPI BOOL WINAPI Thread32Next(HANDLE snapshot, LPTHREADENTRY32 entry)
{
    return next_thread(snapshot, entry, FALSE);
}

/* ------------------------------------------------------------------ registered waits */

/* one waiting thread per registration: the object, and an event that ends the wait. */
typedef struct REGISTERED_WAIT {
    HANDLE Object;
    HANDLE Cancel;
    HANDLE Thread;
    WAITORTIMERCALLBACK Callback;
    PVOID Context;
    DWORD Timeout;
    ULONG Flags;
    volatile LONG Refs;
} REGISTERED_WAIT;

static void release_wait(REGISTERED_WAIT *w)
{
    if (__atomic_sub_fetch(&w->Refs, 1, __ATOMIC_ACQ_REL))
        return;
    NtClose(w->Object);
    NtClose(w->Cancel);
    if (w->Thread)
        NtClose(w->Thread);
    KbFree(w);
}

static DWORD WINAPI wait_thread(LPVOID parameter)
{
    REGISTERED_WAIT *w = parameter;
    HANDLE handles[2] = {w->Cancel, w->Object};
    for (;;) {
        DWORD r = WaitForMultipleObjects(2, handles, FALSE, w->Timeout);
        if (r == WAIT_OBJECT_0 || r == WAIT_FAILED)
            break;
        w->Callback(w->Context, r == WAIT_TIMEOUT);
        if (w->Flags & WT_EXECUTEONLYONCE)
            break;
        /* a manual reset object stays signaled: the callback runs again, as windows does. */
    }
    release_wait(w);
    return 0;
}

WINBASEAPI BOOL WINAPI RegisterWaitForSingleObject(PHANDLE result, HANDLE object, WAITORTIMERCALLBACK callback, PVOID context,
                                        ULONG timeout, ULONG flags)
{
    if (!result || !callback) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    REGISTERED_WAIT *w = KbAllocZero(sizeof(*w));
    if (!w) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    if (!DuplicateHandle(GetCurrentProcess(), object, GetCurrentProcess(), &w->Object, SYNCHRONIZE, FALSE, 0)) {
        KbFree(w);
        return FALSE;
    }
    w->Cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    w->Callback = callback;
    w->Context = context;
    w->Timeout = timeout;
    w->Flags = flags;
    w->Refs = 2; /* the waiting thread and the caller's handle */
    if (w->Cancel)
        w->Thread = CreateThread(NULL, 0, wait_thread, w, 0, NULL);
    if (!w->Thread) {
        if (w->Cancel)
            NtClose(w->Cancel);
        NtClose(w->Object);
        KbFree(w);
        return FALSE;
    }
    *result = w;
    return TRUE;
}

WINBASEAPI HANDLE WINAPI RegisterWaitForSingleObjectEx(HANDLE object, WAITORTIMERCALLBACK callback, PVOID context, ULONG timeout,
                                            ULONG flags)
{
    HANDLE h;
    return RegisterWaitForSingleObject(&h, object, callback, context, timeout, flags) ? h : NULL;
}

/* completion: NULL returns at once, INVALID_HANDLE_VALUE waits for a running callback, or an event to signal. */
WINBASEAPI BOOL WINAPI UnregisterWaitEx(HANDLE handle, HANDLE completion)
{
    REGISTERED_WAIT *w = handle;
    if (!w) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    SetEvent(w->Cancel);
    BOOL self = GetThreadId(w->Thread) == GetCurrentThreadId();
    if (completion == INVALID_HANDLE_VALUE && !self) {
        WaitForSingleObject(w->Thread, INFINITE);
    } else if (completion && completion != INVALID_HANDLE_VALUE) {
        /* signaled once the callbacks are done: from here when called by the callback itself. */
        if (self || WaitForSingleObject(w->Thread, 0) == WAIT_OBJECT_0)
            SetEvent(completion);
        else {
            WaitForSingleObject(w->Thread, INFINITE);
            SetEvent(completion);
        }
    }
    release_wait(w);
    return TRUE;
}

WINBASEAPI BOOL WINAPI UnregisterWait(HANDLE handle)
{
    return UnregisterWaitEx(handle, NULL);
}

/* ------------------------------------------------------------------ volumes */

/*
 * the volume guid names of C: and Z:. windows derives them from the
 * partition; these fixed values are guessed, built from the drive letter.
 */
static const WCHAR *const g_volume_names[2] = {
    L"\\\\?\\Volume{4d696d65-0000-0043-0000-000000000043}\\",
    L"\\\\?\\Volume{4d696d65-0000-005a-0000-00000000005a}\\",
};
static const WCHAR g_volume_roots[2] = {'C', 'Z'};

static int volume_index(LPCWSTR name)
{
    for (int i = 0; i < 2; i++)
        if (name && !_wcsnicmp(name, g_volume_names[i], 48))
            return i;
    return -1;
}

WINBASEAPI BOOL WINAPI GetVolumeNameForVolumeMountPointW(LPCWSTR mount_point, LPWSTR name, DWORD length)
{
    if (!mount_point || !name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR letter = (WCHAR)(mount_point[0] & ~0x20);
    int i = letter == 'C' ? 0 : letter == 'Z' ? 1 : -1;
    if (i < 0 || mount_point[1] != ':' || mount_point[2] != '\\' || mount_point[3]) {
        KbSetError(i < 0 ? ERROR_FILE_NOT_FOUND : ERROR_INVALID_NAME);
        return FALSE;
    }
    SIZE_T n = wcslen(g_volume_names[i]) + 1;
    if (length < n) {
        KbSetError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    memcpy(name, g_volume_names[i], n * sizeof(WCHAR));
    return TRUE;
}

WINBASEAPI BOOL WINAPI GetVolumePathNamesForVolumeNameW(LPCWSTR volume, LPWSTR names, DWORD length, PDWORD returned)
{
    int i = volume_index(volume);
    if (i < 0) {
        KbSetError(volume && !_wcsnicmp(volume, L"\\\\?\\Volume{", 11) ? ERROR_FILE_NOT_FOUND : ERROR_INVALID_NAME);
        return FALSE;
    }
    /* a multi string: "X:\" then an empty string. */
    DWORD need = 5;
    if (returned)
        *returned = need;
    if (!names || length < need) {
        KbSetError(ERROR_MORE_DATA);
        return FALSE;
    }
    names[0] = g_volume_roots[i];
    names[1] = ':';
    names[2] = '\\';
    names[3] = 0;
    names[4] = 0;
    return TRUE;
}

WINBASEAPI BOOL WINAPI FindNextVolumeW(HANDLE handle, LPWSTR name, DWORD length);

typedef struct VOLUME_FIND {
    int Next;
} VOLUME_FIND;

WINBASEAPI HANDLE WINAPI FindFirstVolumeW(LPWSTR name, DWORD length)
{
    VOLUME_FIND *f = KbAllocZero(sizeof(*f));
    if (!f) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE_VALUE;
    }
    if (!FindNextVolumeW(f, name, length)) {
        KbFree(f);
        return INVALID_HANDLE_VALUE;
    }
    return f;
}

WINBASEAPI BOOL WINAPI FindNextVolumeW(HANDLE handle, LPWSTR name, DWORD length)
{
    VOLUME_FIND *f = handle;
    if (!f || f == INVALID_HANDLE_VALUE) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (f->Next >= 2) {
        KbSetError(ERROR_NO_MORE_FILES);
        return FALSE;
    }
    SIZE_T n = wcslen(g_volume_names[f->Next]) + 1;
    if (!name || length < n) {
        KbSetError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    memcpy(name, g_volume_names[f->Next++], n * sizeof(WCHAR));
    return TRUE;
}

WINBASEAPI BOOL WINAPI FindVolumeClose(HANDLE handle)
{
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    KbFree(handle);
    return TRUE;
}

/* ------------------------------------------------------------------ geographical information */

#define GEO_NATION 0x0001
#define GEO_LATITUDE 0x0002
#define GEO_LONGITUDE 0x0003
#define GEO_ISO2 0x0004
#define GEO_ISO3 0x0005
#define GEO_RFC1766 0x0006
#define GEO_LCID 0x0007
#define GEO_FRIENDLYNAME 0x0008
#define GEO_OFFICIALNAME 0x0009
#define GEO_TIMEZONES 0x000A
#define GEO_OFFICIALLANGUAGES 0x000B
#define GEO_ISO_UN_NUMBER 0x000C
#define GEO_PARENT 0x000D
#define GEO_DIALINGCODE 0x000E
#define GEO_CURRENCYCODE 0x000F
#define GEO_CURRENCYSYMBOL 0x0010
#define GEO_NAME 0x0011
#define GEO_ID 0x0012

typedef struct GEO_DATA {
    GEOID Id;
    PCWSTR Iso2;
    PCWSTR Iso3;
    PCWSTR Name;
    PCWSTR Official;
    PCWSTR Latitude;
    PCWSTR Longitude;
    PCWSTR Rfc1766;
    LCID Lcid;
    int UnNumber;
    GEOID Parent;
    PCWSTR Dialing;
    PCWSTR Currency;
    PCWSTR CurrencySymbol;
} GEO_DATA;

/* the nations of the locales mime knows; the values are those windows documents for each nation. */
static const GEO_DATA g_geos[] = {
    {244, L"US", L"USA", L"United States", L"United States of America", L"39.45", L"-98.908", L"en-us", 0x0409, 840,
     39070, L"1", L"USD", L"$"},
    {242, L"GB", L"GBR", L"United Kingdom", L"United Kingdom of Great Britain and Northern Ireland", L"54.633",
     L"-3.432", L"en-gb", 0x0809, 826, 10039882, L"44", L"GBP", L"\x00A3"},
    {84, L"FR", L"FRA", L"France", L"French Republic", L"46.15", L"2.202", L"fr-fr", 0x040C, 250, 10210824, L"33",
     L"EUR", L"\x20AC"},
    {94, L"DE", L"DEU", L"Germany", L"Federal Republic of Germany", L"51.164", L"10.454", L"de-de", 0x0407, 276,
     10210824, L"49", L"EUR", L"\x20AC"},
    {39, L"CA", L"CAN", L"Canada", L"Canada", L"60.108", L"-113.643", L"en-ca", 0x1009, 124, 23581, L"1", L"CAD",
     L"$"},
};

static const GEO_DATA *find_geo(GEOID id)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_geos); i++)
        if (g_geos[i].Id == id)
            return &g_geos[i];
    return NULL;
}

WINBASEAPI int WINAPI GetGeoInfoW(GEOID id, GEOTYPE type, LPWSTR data, int length, LANGID language)
{
    const GEO_DATA *g = find_geo(id);
    if (!g) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    WCHAR number[16];
    PCWSTR value;
    switch (type) {
    case GEO_NATION:
    case GEO_ID:
        KbFormat(number, RTL_NUMBER_OF(number), L"%d", g->Id);
        value = number;
        break;
    case GEO_LATITUDE: value = g->Latitude; break;
    case GEO_LONGITUDE: value = g->Longitude; break;
    case GEO_ISO2:
    case GEO_NAME: value = g->Iso2; break;
    case GEO_ISO3: value = g->Iso3; break;
    case GEO_RFC1766: value = g->Rfc1766; break;
    case GEO_LCID:
        KbFormat(number, RTL_NUMBER_OF(number), L"%08x", g->Lcid);
        value = number;
        break;
    case GEO_FRIENDLYNAME: value = g->Name; break;
    case GEO_OFFICIALNAME: value = g->Official; break;
    case GEO_ISO_UN_NUMBER:
        KbFormat(number, RTL_NUMBER_OF(number), L"%03d", g->UnNumber);
        value = number;
        break;
    case GEO_PARENT:
        KbFormat(number, RTL_NUMBER_OF(number), L"%d", g->Parent);
        value = number;
        break;
    case GEO_DIALINGCODE: value = g->Dialing; break;
    case GEO_CURRENCYCODE: value = g->Currency; break;
    case GEO_CURRENCYSYMBOL: value = g->CurrencySymbol; break;
    default:
        KbSetError(ERROR_INVALID_FLAGS);
        return 0;
    }
    int n = (int)wcslen(value) + 1;
    if (!length)
        return n;
    if (!data || length < n) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(data, value, n * sizeof(WCHAR));
    return n;
}

WINBASEAPI int WINAPI GetGeoInfoA(GEOID id, GEOTYPE type, LPSTR data, int length, LANGID language)
{
    WCHAR wide[128];
    int n = GetGeoInfoW(id, type, wide, RTL_NUMBER_OF(wide), language);
    if (!n)
        return 0;
    return WideCharToMultiByte(CP_ACP, 0, wide, -1, data, length, NULL, NULL);
}

/* ------------------------------------------------------------------ thread contexts and device control */

WINBASEAPI BOOL WINAPI GetThreadContext(HANDLE thread, LPCONTEXT context)
{
    NTSTATUS status = NtGetContextThread(thread, context);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

WINBASEAPI BOOL WINAPI SetThreadContext(HANDLE thread, const CONTEXT *context)
{
    NTSTATUS status = NtSetContextThread(thread, (PCONTEXT)context);
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

/* the control codes of file systems go to NtFsControlFile, the rest to the device. */
#define KB_DEVICE_TYPE(code) ((code) >> 16)
#define KB_FILE_DEVICE_FILE_SYSTEM 0x00000009
#define KB_FILE_DEVICE_NAMED_PIPE 0x00000011

WINBASEAPI BOOL WINAPI DeviceIoControl(HANDLE device, DWORD code, LPVOID in, DWORD in_size, LPVOID out, DWORD out_size,
                            LPDWORD returned, LPOVERLAPPED overlapped)
{
    BOOL fs = KB_DEVICE_TYPE(code) == KB_FILE_DEVICE_FILE_SYSTEM || KB_DEVICE_TYPE(code) == KB_FILE_DEVICE_NAMED_PIPE;
    NTSTATUS status;
    if (overlapped) {
        PIO_STATUS_BLOCK iosb = (PIO_STATUS_BLOCK)overlapped;
        iosb->Status = STATUS_PENDING;
        iosb->Information = 0;
        PVOID apc_context = ((ULONG_PTR)overlapped->hEvent & 1) ? NULL : overlapped;
        if (fs)
            status = NtFsControlFile(device, overlapped->hEvent, NULL, apc_context, iosb, code, in, in_size, out, out_size);
        else
            status = NtDeviceIoControlFile(device, overlapped->hEvent, NULL, apc_context, iosb, code, in, in_size, out,
                                           out_size);
        if (returned && !NT_ERROR(status) && status != STATUS_PENDING)
            *returned = (DWORD)iosb->Information;
        if (status == STATUS_PENDING || NT_ERROR(status))
            return KbSetStatus(status);
        return TRUE;
    }
    IO_STATUS_BLOCK iosb = {0};
    if (fs)
        status = NtFsControlFile(device, NULL, NULL, NULL, &iosb, code, in, in_size, out, out_size);
    else
        status = NtDeviceIoControlFile(device, NULL, NULL, NULL, &iosb, code, in, in_size, out, out_size);
    if (status == STATUS_PENDING) {
        WaitForSingleObject(device, INFINITE);
        status = iosb.Status;
    }
    if (returned)
        *returned = (DWORD)iosb.Information;
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

/* directory change notifications need NtNotifyChangeDirectoryFile, which the file systems do not support yet. */
WINBASEAPI BOOL WINAPI ReadDirectoryChangesW(HANDLE directory, LPVOID buffer, DWORD length, BOOL subtree, DWORD filter,
                                  LPDWORD returned, LPOVERLAPPED overlapped, LPOVERLAPPED_COMPLETION_ROUTINE routine)
{
    if (!buffer || !length) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    KbSetError(ERROR_INVALID_FUNCTION);
    return FALSE;
}
