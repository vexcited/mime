/* tool help snapshots, volume names, geographical information, thread contexts and DeviceIoControl. */
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

typedef struct tagHEAPLIST32 {
    SIZE_T dwSize;
    DWORD th32ProcessID;
    ULONG_PTR th32HeapID;
    DWORD dwFlags;
} HEAPLIST32, *PHEAPLIST32;

typedef struct tagHEAPENTRY32 {
    SIZE_T dwSize;
    HANDLE hHandle;
    ULONG_PTR dwAddress;
    SIZE_T dwBlockSize;
    DWORD dwFlags;
    DWORD dwLockCount;
    DWORD dwResvd;
    DWORD th32ProcessID;
    ULONG_PTR th32HeapID;
} HEAPENTRY32, *PHEAPENTRY32;

#define HF32_DEFAULT 1
#define LF32_FIXED 0x00000001

/*
 * like on windows, the snapshot lives in a section so its handle can be duplicated. a header with the list counts and
 * offsets is followed by the module, process and thread entries.
 */
typedef struct SNAPSHOT {
    ULONG ModuleCount;
    ULONG ProcessCount;
    ULONG ThreadCount;
    ULONG ModulePos;
    ULONG ProcessPos;
    ULONG ThreadPos;
    ULONG HeapCount;
    ULONG HeapPos;
} SNAPSHOT;

#define SNAPSHOT_MODULES(s) ((MODULEENTRY32W *)((PUCHAR)(s) + sizeof(SNAPSHOT)))
#define SNAPSHOT_PROCESSES(s) ((PROCESSENTRY32W *)(SNAPSHOT_MODULES(s) + (s)->ModuleCount))
#define SNAPSHOT_THREADS(s) ((THREADENTRY32 *)(SNAPSHOT_PROCESSES(s) + (s)->ProcessCount))
#define SNAPSHOT_HEAPS(s) ((HEAPLIST32 *)(SNAPSHOT_THREADS(s) + (s)->ThreadCount))

#define TH32CS_INHERIT 0x80000000u
#define TH32CS_ALL (TH32CS_SNAPHEAPLIST | TH32CS_SNAPPROCESS | TH32CS_SNAPTHREAD | TH32CS_SNAPMODULE)
#define MAX_SNAPSHOT_MODULES 4096

/* grows while modules are collected, before the final count is known. */
typedef struct MODULE_LIST {
    MODULEENTRY32W *Entries;
    ULONG Count, Capacity;
} MODULE_LIST;

static MODULEENTRY32W *add_module(MODULE_LIST *list, DWORD pid)
{
    if (list->Count >= MAX_SNAPSHOT_MODULES)
        return nullptr;
    if (list->Count == list->Capacity) {
        ULONG capacity = list->Capacity ? list->Capacity * 2 : 64;
        MODULEENTRY32W *grown = KbAlloc(capacity * sizeof(*grown));
        if (!grown)
            return nullptr;
        if (list->Entries) {
            memcpy(grown, list->Entries, list->Count * sizeof(*grown));
            KbFree(list->Entries);
        }
        list->Entries = grown;
        list->Capacity = capacity;
    }
    MODULEENTRY32W *m = &list->Entries[list->Count++];
    memset(m, 0, sizeof(*m));
    m->dwSize = sizeof(*m);
    m->th32ModuleID = 1;
    m->th32ProcessID = pid;
    m->GlblcntUsage = 0xFFFF;
    m->ProccntUsage = 0xFFFF;
    return m;
}

static void copy_name(WCHAR *out, SIZE_T chars_max, PCWSTR in, SIZE_T bytes)
{
    SIZE_T n = bytes / sizeof(WCHAR);
    if (n >= chars_max)
        n = chars_max - 1;
    memcpy(out, in, n * sizeof(WCHAR));
    out[n] = 0;
}

static void own_modules(MODULE_LIST *list, DWORD pid)
{
    PPEB_LDR_DATA ldr = KbPeb()->Ldr;
    RtlEnterCriticalSection(KbPeb()->LoaderLock);
    for (PLIST_ENTRY l = ldr->InLoadOrderModuleList.Flink; l != &ldr->InLoadOrderModuleList; l = l->Flink) {
        PLDR_DATA_TABLE_ENTRY e = CONTAINING_RECORD(l, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        MODULEENTRY32W *m = add_module(list, pid);
        if (!m)
            break;
        m->modBaseAddr = e->DllBase;
        m->modBaseSize = e->SizeOfImage;
        m->hModule = e->DllBase;
        copy_name(m->szModule, MAX_MODULE_NAME32 + 1, e->BaseDllName.Buffer, e->BaseDllName.Length);
        copy_name(m->szExePath, MAX_PATH, e->FullDllName.Buffer, e->FullDllName.Length);
    }
    RtlLeaveCriticalSection(KbPeb()->LoaderLock);
}

static BOOL read_memory(HANDLE process, ULONG64 address, PVOID out, SIZE_T size)
{
    SIZE_T done = 0;
    if ((ULONG64)(ULONG_PTR)address != address)
        return FALSE;
    return NT_SUCCESS(NtReadVirtualMemory(process, (PVOID)(ULONG_PTR)address, out, size, &done)) && done == size;
}

static void read_name(HANDLE process, ULONG64 buffer, ULONG bytes, WCHAR *out, SIZE_T chars_max)
{
    SIZE_T n = bytes / sizeof(WCHAR);
    out[0] = 0;
    if (!buffer || !n)
        return;
    if (n >= chars_max)
        n = chars_max - 1;
    if (!read_memory(process, buffer, out, n * sizeof(WCHAR)))
        n = 0;
    out[n] = 0;
}

/* where to find things in another process's loader list: native width, or the 32-bit layout of a wow64 process. */
typedef struct LDR_LAYOUT {
    ULONG PointerSize;
    ULONG LdrOffset;       /* in the peb. */
    ULONG ListOffset;      /* of the load order list in PEB_LDR_DATA. */
    ULONG BaseOffset;
    ULONG SizeOffset;
    ULONG FullNameOffset;  /* UNICODE_STRING: length, maximum length, padding, then the buffer. */
    ULONG BaseNameOffset;
    ULONG StringBufferOffset;
} LDR_LAYOUT;

static BOOL read_pointer(HANDLE process, ULONG64 address, ULONG size, ULONG64 *value)
{
    *value = 0;
    return read_memory(process, address, value, size);
}

static BOOL other_modules(HANDLE process, ULONG64 peb, const LDR_LAYOUT *layout, MODULE_LIST *list, DWORD pid)
{
    ULONG64 ldr;
    if (!read_pointer(process, peb + layout->LdrOffset, layout->PointerSize, &ldr) || !ldr)
        return FALSE;
    ULONG64 head = ldr + layout->ListOffset, link;
    if (!read_pointer(process, head, layout->PointerSize, &link))
        return FALSE;
    for (ULONG guard = 0; link && link != head && guard < MAX_SNAPSHOT_MODULES; guard++) {
        UCHAR entry[0x80];
        if (!read_memory(process, link, entry, sizeof(entry)))
            return FALSE;
        MODULEENTRY32W *m = add_module(list, pid);
        if (!m)
            return TRUE;
        ULONG64 base = 0, name_buffer;
        ULONG size_of_image = 0;
        memcpy(&base, entry + layout->BaseOffset, layout->PointerSize);
        memcpy(&size_of_image, entry + layout->SizeOffset, 4);
        m->modBaseAddr = (BYTE *)(ULONG_PTR)base;
        m->modBaseSize = size_of_image;
        m->hModule = (HMODULE)(ULONG_PTR)base;
        name_buffer = 0;
        memcpy(&name_buffer, entry + layout->BaseNameOffset + layout->StringBufferOffset, layout->PointerSize);
        read_name(process, name_buffer, *(USHORT *)(entry + layout->BaseNameOffset), m->szModule, MAX_MODULE_NAME32 + 1);
        name_buffer = 0;
        memcpy(&name_buffer, entry + layout->FullNameOffset + layout->StringBufferOffset, layout->PointerSize);
        read_name(process, name_buffer, *(USHORT *)(entry + layout->FullNameOffset), m->szExePath, MAX_PATH);
        if (!read_pointer(process, link, layout->PointerSize, &link))
            return FALSE;
    }
    return TRUE;
}

/*
 * like windows, the caller's width decides what is visible. a 64-bit caller lists 64-bit modules with
 * TH32CS_SNAPMODULE and a wow64 process's 32-bit ones with TH32CS_SNAPMODULE32. a 32-bit caller only sees 32-bit
 * processes.
 */
static BOOL modules_of(DWORD pid, DWORD flags, MODULE_LIST *list)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process)
        return FALSE;
    BOOL ok = TRUE;
    PROCESS_BASIC_INFORMATION basic;
    ULONG_PTR peb32 = 0;
    NTSTATUS status = NtQueryInformationProcess(process, ProcessBasicInformation, &basic, sizeof(basic), nullptr);
    if (NT_SUCCESS(status))
        NtQueryInformationProcess(process, ProcessWow64Information, &peb32, sizeof(peb32), nullptr);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        ok = FALSE;
    }
#ifdef _WIN64
    static const LDR_LAYOUT native = {8, offsetof(PEB, Ldr), offsetof(PEB_LDR_DATA, InLoadOrderModuleList),
                                      offsetof(LDR_DATA_TABLE_ENTRY, DllBase), offsetof(LDR_DATA_TABLE_ENTRY, SizeOfImage),
                                      offsetof(LDR_DATA_TABLE_ENTRY, FullDllName), offsetof(LDR_DATA_TABLE_ENTRY, BaseDllName),
                                      8};
    /* wow64 layout: base at 0x18, size at 0x20, names at 0x24 and 0x2C with the buffer 4 bytes in. */
    static const LDR_LAYOUT wow = {4, 0x0C, 0x0C, 0x18, 0x20, 0x24, 0x2C, 4};
    if (ok && (flags & TH32CS_SNAPMODULE))
        ok = other_modules(process, (ULONG64)(ULONG_PTR)basic.PebBaseAddress, &native, list, pid);
    if (ok && (flags & TH32CS_SNAPMODULE32) && peb32)
        ok = other_modules(process, peb32, &wow, list, pid);
#else
    static const LDR_LAYOUT native = {4, offsetof(PEB, Ldr), offsetof(PEB_LDR_DATA, InLoadOrderModuleList),
                                      offsetof(LDR_DATA_TABLE_ENTRY, DllBase), offsetof(LDR_DATA_TABLE_ENTRY, SizeOfImage),
                                      offsetof(LDR_DATA_TABLE_ENTRY, FullDllName), offsetof(LDR_DATA_TABLE_ENTRY, BaseDllName),
                                      4};
    /* a 64-bit process's peb is out of a 32-bit caller's reach. */
    BOOL wow_target = FALSE;
    if (ok && (!IsWow64Process(process, &wow_target) || !wow_target)) {
        KbSetError(ERROR_PARTIAL_COPY);
        ok = FALSE;
    }
    if (ok)
        ok = other_modules(process, peb32 ? peb32 : (ULONG64)(ULONG_PTR)basic.PebBaseAddress, &native, list, pid);
#endif
    if (!ok && list->Count == 0 && NT_SUCCESS(status))
        KbSetError(ERROR_PARTIAL_COPY);
    NtClose(process);
    return ok;
}

static PSYSTEM_PROCESS_INFORMATION process_list(PVOID *memory)
{
    SIZE_T size = 0x20000;
    for (int round = 0; round < 8; round++, size *= 2) {
        PVOID buffer = KbAlloc(size);
        if (!buffer)
            return nullptr;
        ULONG need = 0;
        NTSTATUS status = NtQuerySystemInformation(SystemProcessInformation, buffer, (ULONG)size, &need);
        if (NT_SUCCESS(status)) {
            *memory = buffer;
            return buffer;
        }
        KbFree(buffer);
        if (status != STATUS_INFO_LENGTH_MISMATCH)
            break;
        if (need > size)
            size = need;
    }
    return nullptr;
}

WINBASEAPI HANDLE WINAPI CreateToolhelp32Snapshot(DWORD flags, DWORD pid)
{
    DWORD self = GetCurrentProcessId();
    if (flags & ~(TH32CS_INHERIT | TH32CS_ALL | TH32CS_SNAPMODULE32)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    BOOL module_list = (flags & (TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32)) != 0;
    if (!pid)
        pid = self;
    MODULE_LIST modules = {};
    PVOID memory = nullptr;
    PSYSTEM_PROCESS_INFORMATION info = nullptr;
    ULONG processes = 0, threads = 0;
    if (flags & (TH32CS_SNAPPROCESS | TH32CS_SNAPTHREAD) || module_list) {
        info = process_list(&memory);
        if (!info && (flags & (TH32CS_SNAPPROCESS | TH32CS_SNAPTHREAD))) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return INVALID_HANDLE_VALUE;
        }
    }
    if (info) {
        for (PSYSTEM_PROCESS_INFORMATION p = info;; p = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)p + p->NextEntryOffset)) {
            processes++;
            threads += p->NumberOfThreads;
            if (!p->NextEntryOffset)
                break;
        }
    }
    if (module_list) {
        BOOL found = pid == self;
        for (PSYSTEM_PROCESS_INFORMATION p = info; p && !found;
             p = p->NextEntryOffset ? (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)p + p->NextEntryOffset) : nullptr)
            found = (DWORD)(ULONG_PTR)p->UniqueProcessId == pid;
        if (!found) {
            KbFree(memory);
            KbSetError(ERROR_INVALID_PARAMETER);
            return INVALID_HANDLE_VALUE;
        }
        BOOL ok = TRUE;
        if (pid == self) {
#ifdef _WIN64
            if (flags & TH32CS_SNAPMODULE)
                own_modules(&modules, pid);
#else
            own_modules(&modules, pid);
#endif
        } else {
            ok = modules_of(pid, flags, &modules);
        }
        if (!ok && modules.Count == 0) {
            KbFree(modules.Entries);
            KbFree(memory);
            return INVALID_HANDLE_VALUE;
        }
    }
    if (!(flags & TH32CS_SNAPPROCESS))
        processes = 0;
    if (!(flags & TH32CS_SNAPTHREAD))
        threads = 0;
    /* the heaps are listed for this process only, whose block structures can be read. */
    ULONG heaps = (flags & TH32CS_SNAPHEAPLIST) && pid == self ? NtCurrentPeb()->NumberOfHeaps : 0;
    SIZE_T total = sizeof(SNAPSHOT) + modules.Count * sizeof(MODULEENTRY32W) + processes * sizeof(PROCESSENTRY32W) +
                   threads * sizeof(THREADENTRY32) + heaps * sizeof(HEAPLIST32);
    LARGE_INTEGER size;
    size.QuadPart = (LONGLONG)total;
    HANDLE section;
    NTSTATUS status = NtCreateSection(&section, SECTION_ALL_ACCESS, nullptr, &size, PAGE_READWRITE, SEC_COMMIT, nullptr);
    if (!NT_SUCCESS(status)) {
        KbFree(modules.Entries);
        KbFree(memory);
        KbSetStatus(status);
        return INVALID_HANDLE_VALUE;
    }
    PVOID view = nullptr;
    SIZE_T view_size = 0;
    status = NtMapViewOfSection(section, NtCurrentProcess(), &view, 0, 0, nullptr, &view_size, ViewShare, 0, PAGE_READWRITE);
    if (!NT_SUCCESS(status)) {
        NtClose(section);
        KbFree(modules.Entries);
        KbFree(memory);
        KbSetStatus(status);
        return INVALID_HANDLE_VALUE;
    }
    SNAPSHOT *s = view;
    s->ModuleCount = modules.Count;
    s->ProcessCount = processes;
    s->ThreadCount = threads;
    s->HeapCount = heaps;
    for (ULONG i = 0; i < heaps; i++) {
        HEAPLIST32 *list = &SNAPSHOT_HEAPS(s)[i];
        list->dwSize = sizeof(*list);
        list->th32ProcessID = pid;
        list->th32HeapID = (ULONG_PTR)NtCurrentPeb()->ProcessHeaps[i];
        list->dwFlags = NtCurrentPeb()->ProcessHeaps[i] == NtCurrentPeb()->ProcessHeap ? HF32_DEFAULT : 0;
    }
    if (modules.Count)
        memcpy(SNAPSHOT_MODULES(s), modules.Entries, modules.Count * sizeof(MODULEENTRY32W));
    PROCESSENTRY32W *out_process = SNAPSHOT_PROCESSES(s);
    THREADENTRY32 *out_thread = SNAPSHOT_THREADS(s);
    for (PSYSTEM_PROCESS_INFORMATION p = info; p;
         p = p->NextEntryOffset ? (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)p + p->NextEntryOffset) : nullptr) {
        if (processes) {
            memset(out_process, 0, sizeof(*out_process));
            out_process->dwSize = sizeof(*out_process);
            out_process->th32ProcessID = (DWORD)(ULONG_PTR)p->UniqueProcessId;
            out_process->cntThreads = p->NumberOfThreads;
            out_process->th32ParentProcessID = (DWORD)(ULONG_PTR)p->InheritedFromUniqueProcessId;
            out_process->pcPriClassBase = p->BasePriority;
            if (p->ImageName.Buffer)
                copy_name(out_process->szExeFile, MAX_PATH, p->ImageName.Buffer, p->ImageName.Length);
            out_process++;
        }
        if (threads) {
            PSYSTEM_THREAD_INFORMATION t = (PSYSTEM_THREAD_INFORMATION)(p + 1);
            for (ULONG i = 0; i < p->NumberOfThreads; i++, t++) {
                out_thread->dwSize = sizeof(*out_thread);
                out_thread->cntUsage = 0;
                out_thread->th32ThreadID = (DWORD)(ULONG_PTR)t->ClientId.UniqueThread;
                out_thread->th32OwnerProcessID = (DWORD)(ULONG_PTR)t->ClientId.UniqueProcess;
                out_thread->tpBasePri = t->BasePriority;
                out_thread->tpDeltaPri = 0;
                out_thread->dwFlags = 0;
                out_thread++;
            }
        }
    }
    NtUnmapViewOfSection(NtCurrentProcess(), view);
    KbFree(modules.Entries);
    KbFree(memory);
    return section;
}

/* maps the snapshot behind a handle; NULL with the last error set when it is not one. */
static SNAPSHOT *map_snapshot(HANDLE handle)
{
    PVOID view = nullptr;
    SIZE_T size = 0;
    NTSTATUS status = NtMapViewOfSection(handle, NtCurrentProcess(), &view, 0, 0, nullptr, &size, ViewShare, 0,
                                         PAGE_READWRITE);
    if (!NT_SUCCESS(status) || size < sizeof(SNAPSHOT)) {
        if (NT_SUCCESS(status))
            NtUnmapViewOfSection(NtCurrentProcess(), view);
        KbSetError(ERROR_INVALID_HANDLE);
        return nullptr;
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
        *entry = SNAPSHOT_MODULES(s)[s->ModulePos++];
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
    WideCharToMultiByte(CP_ACP, 0, w.szModule, -1, entry->szModule, sizeof(entry->szModule), nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, w.szExePath, -1, entry->szExePath, sizeof(entry->szExePath), nullptr, nullptr);
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
        *entry = SNAPSHOT_PROCESSES(s)[s->ProcessPos++];
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
    WideCharToMultiByte(CP_ACP, 0, w.szExeFile, -1, entry->szExeFile, sizeof(entry->szExeFile), nullptr, nullptr);
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
        *entry = SNAPSHOT_THREADS(s)[s->ThreadPos++];
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

/* ------------------------------------------------------------------ volumes */

/* windows derives volume guids from the partition, these are made up from the drive letter. */
static const WCHAR *const g_volume_names[2] = {
    L"\\\\?\\Volume{4d696d65-0000-0043-0000-000000000043}\\",
    L"\\\\?\\Volume{4d696d65-0000-005a-0000-00000000005a}\\",
};
static constexpr WCHAR g_volume_roots[2] = {'C', 'Z'};

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
    /* a multi string: "X:\" and an empty string. */
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

#ifndef ERROR_NOT_A_REPARSE_POINT
#define ERROR_NOT_A_REPARSE_POINT 4390L
#endif

/*
 * volumes of the machine are reached by their drive letter only, nothing is mounted in a folder: a volume has no mount
 * points to list, none can be made and a folder is never one.
 */
WINBASEAPI HANDLE WINAPI FindFirstVolumeMountPointW(LPCWSTR root, LPWSTR name, DWORD length)
{
    if (!root || !name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    WCHAR letter = (WCHAR)(root[0] & ~0x20);
    BOOL drive = (letter == 'C' || letter == 'Z') && root[1] == ':' && root[2] == '\\' && !root[3];
    if (!drive && volume_index(root) < 0) {
        KbSetError(ERROR_PATH_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    KbSetError(ERROR_NO_MORE_FILES);
    return INVALID_HANDLE_VALUE;
}

WINBASEAPI BOOL WINAPI FindNextVolumeMountPointW(HANDLE handle, LPWSTR name, DWORD length)
{
    KbSetError(ERROR_INVALID_HANDLE);
    return FALSE;
}

WINBASEAPI BOOL WINAPI FindVolumeMountPointClose(HANDLE handle)
{
    KbSetError(ERROR_INVALID_HANDLE);
    return FALSE;
}

WINBASEAPI BOOL WINAPI SetVolumeMountPointW(LPCWSTR mount_point, LPCWSTR volume)
{
    if (!mount_point || !volume || volume_index(volume) < 0) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    KbSetError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

WINBASEAPI BOOL WINAPI DeleteVolumeMountPointW(LPCWSTR mount_point)
{
    if (!mount_point) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    DWORD attributes = GetFileAttributesW(mount_point);
    KbSetError(attributes == INVALID_FILE_ATTRIBUTES ? ERROR_FILE_NOT_FOUND : ERROR_NOT_A_REPARSE_POINT);
    return FALSE;
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

/* nations of the supported locales, with the values windows documents for each. */
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
    return nullptr;
}

static DWORD g_nls_updates;

DWORD WINAPI NlsGetCacheUpdateCount(void)
{
    return g_nls_updates;
}

#define GEOCLASS_REGION_MASK 14
#define GEOCLASS_NATION_MASK 16

/* there is no region data, so GEOCLASS_REGION_MASK enumerates nothing. */
WINBASEAPI BOOL WINAPI EnumSystemGeoID(DWORD geo_class, GEOID parent, GEO_ENUMPROC proc)
{
    if (!proc) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (geo_class != 0 && geo_class != GEOCLASS_REGION_MASK && geo_class != GEOCLASS_NATION_MASK) {
        KbSetError(ERROR_INVALID_FLAGS);
        return FALSE;
    }
    if (geo_class == GEOCLASS_REGION_MASK)
        return TRUE;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_geos); i++) {
        if (parent && g_geos[i].Parent != parent)
            continue;
        if (!proc(g_geos[i].Id))
            break;
    }
    return TRUE;
}

/* stored under the international settings, by name and by number. */
WINBASEAPI BOOL WINAPI SetUserGeoID(GEOID id)
{
    const GEO_DATA *g = find_geo(id);
    if (!g) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    HKEY key;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, L"Control Panel\\International\\Geo", 0, nullptr, 0, KEY_SET_VALUE,
                                     nullptr, &key, nullptr);
    if (status) {
        KbSetError((DWORD)status);
        return FALSE;
    }
    WCHAR number[16];
    KbFormat(number, RTL_NUMBER_OF(number), L"%d", g->Id);
    status = RegSetValueExW(key, L"Nation", 0, REG_SZ, (const BYTE *)number, (DWORD)(wcslen(number) + 1) * sizeof(WCHAR));
    if (!status)
        status = RegSetValueExW(key, L"Name", 0, REG_SZ, (const BYTE *)g->Iso2,
                                (DWORD)(wcslen(g->Iso2) + 1) * sizeof(WCHAR));
    RegCloseKey(key);
    if (status) {
        KbSetError((DWORD)status);
        return FALSE;
    }
    g_nls_updates++;
    return TRUE;
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
    return WideCharToMultiByte(CP_ACP, 0, wide, -1, data, length, nullptr, nullptr);
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

/* file system control codes go to NtFsControlFile, the rest to the device. */
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
        PVOID apc_context = ((ULONG_PTR)overlapped->hEvent & 1) ? nullptr : overlapped;
        if (fs)
            status = NtFsControlFile(device, overlapped->hEvent, nullptr, apc_context, iosb, code, in, in_size, out, out_size);
        else
            status = NtDeviceIoControlFile(device, overlapped->hEvent, nullptr, apc_context, iosb, code, in, in_size, out,
                                           out_size);
        if (returned && !NT_ERROR(status) && status != STATUS_PENDING)
            *returned = (DWORD)iosb->Information;
        if (status == STATUS_PENDING || NT_ERROR(status))
            return KbSetStatus(status);
        return TRUE;
    }
    IO_STATUS_BLOCK iosb = {};
    if (fs)
        status = NtFsControlFile(device, nullptr, nullptr, nullptr, &iosb, code, in, in_size, out, out_size);
    else
        status = NtDeviceIoControlFile(device, nullptr, nullptr, nullptr, &iosb, code, in, in_size, out, out_size);
    if (status == STATUS_PENDING) {
        WaitForSingleObject(device, INFINITE);
        status = iosb.Status;
    }
    if (returned)
        *returned = (DWORD)iosb.Information;
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

/* ------------------------------------------------------------------ heaps */

static BOOL next_heap(HANDLE handle, PHEAPLIST32 entry, BOOL first)
{
    if (!entry || entry->dwSize < sizeof(*entry)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SNAPSHOT *s = map_snapshot(handle);
    if (!s)
        return FALSE;
    if (first)
        s->HeapPos = 0;
    BOOL ok = s->HeapPos < s->HeapCount;
    if (ok)
        *entry = SNAPSHOT_HEAPS(s)[s->HeapPos++];
    else
        KbSetError(ERROR_NO_MORE_FILES);
    unmap_snapshot(s);
    return ok;
}

WINBASEAPI BOOL WINAPI Heap32ListFirst(HANDLE snapshot, PHEAPLIST32 entry)
{
    return next_heap(snapshot, entry, TRUE);
}

WINBASEAPI BOOL WINAPI Heap32ListNext(HANDLE snapshot, PHEAPLIST32 entry)
{
    return next_heap(snapshot, entry, FALSE);
}

/* the blocks of a heap of this process that are in use, the one after the address given if one is. */
static BOOL heap_block(PHEAPENTRY32 entry, DWORD pid, ULONG_PTR heap, BOOL after)
{
    if (!entry || entry->dwSize < sizeof(*entry) || pid != GetCurrentProcessId()) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PEB *peb = NtCurrentPeb();
    BOOL known = FALSE;
    for (ULONG i = 0; i < peb->NumberOfHeaps; i++)
        known |= (ULONG_PTR)peb->ProcessHeaps[i] == heap;
    if (!known) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PROCESS_HEAP_ENTRY walk = {};
    while (HeapWalk((HANDLE)heap, &walk)) {
        if (!(walk.wFlags & PROCESS_HEAP_ENTRY_BUSY))
            continue;
        if (after && (ULONG_PTR)walk.lpData <= entry->dwAddress)
            continue;
        entry->hHandle = (HANDLE)heap;
        entry->dwAddress = (ULONG_PTR)walk.lpData;
        entry->dwBlockSize = walk.cbData;
        entry->dwFlags = LF32_FIXED;
        entry->dwLockCount = 0;
        entry->dwResvd = 0;
        entry->th32ProcessID = pid;
        entry->th32HeapID = heap;
        return TRUE;
    }
    KbSetError(ERROR_NO_MORE_FILES);
    return FALSE;
}

WINBASEAPI BOOL WINAPI Heap32First(PHEAPENTRY32 entry, DWORD pid, ULONG_PTR heap)
{
    return heap_block(entry, pid, heap, FALSE);
}

WINBASEAPI BOOL WINAPI Heap32Next(PHEAPENTRY32 entry)
{
    if (!entry || entry->dwSize < sizeof(*entry)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return heap_block(entry, entry->th32ProcessID, entry->th32HeapID, TRUE);
}
