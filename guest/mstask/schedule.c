/*
 * the scheduler: the folder of the task files, and its names. A name without an extension has the job extension added,
 * and one without a path is below the tasks folder.
 */
#include "mstask_private.h"

typedef struct SCHEDULE {
    ITaskScheduler scheduler;
    LONG refs;
    WCHAR folder[MAX_PATH];
} SCHEDULE;

typedef struct ENUMERATOR {
    IEnumWorkItems items;
    LONG refs;
    /* the names found, taken when the enumerator is made. */
    LPWSTR *names;
    ULONG count, position;
} ENUMERATOR;

static SCHEDULE *schedule_of(ITaskScheduler *This) { return (SCHEDULE *)This; }

HRESULT MstaskTasksFolder(WCHAR folder[MAX_PATH])
{
    WCHAR value[MAX_PATH] = L"";
    DWORD size = sizeof(value) - sizeof(WCHAR);
    HKEY key;
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\SchedulingAgent", 0, KEY_READ, &key)) {
        DWORD type;
        if (RegQueryValueExW(key, L"TasksFolder", nullptr, &type, (BYTE *)value, &size) || (type != REG_SZ && type != REG_EXPAND_SZ))
            value[0] = 0;
        RegCloseKey(key);
    }
    if (!value[0])
        wcscpy(value, L"%WinDir%\\Tasks");
    DWORD length = ExpandEnvironmentStringsW(value, folder, MAX_PATH);
    if (!length || length > MAX_PATH)
        return length ? HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW) : HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

static HRESULT ensure_folder(const WCHAR *folder)
{
    if (GetFileAttributesW(folder) != INVALID_FILE_ATTRIBUTES)
        return S_OK;
    if (!CreateDirectoryW(folder, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());
    SetFileAttributesW(folder, FILE_ATTRIBUTE_SYSTEM);
    return S_OK;
}

/* the path of the file of a task: the name with the job extension, below the tasks folder when it has no path. */
static HRESULT job_path(SCHEDULE *schedule, LPCWSTR name, WCHAR **path)
{
    *path = nullptr;
    SIZE_T length = wcslen(name);
    if (!length || name[length - 1] == L'\\' || name[length - 1] == L'/')
        return E_INVALIDARG;
    BOOL relative = TRUE;
    if (length > 2 && (name[1] == L':' || (name[0] == L'\\' && name[1] == L'\\') || (name[0] == L'/' && name[1] == L'/')))
        relative = FALSE;
    const WCHAR *dot = wcsrchr(name, L'.');
    BOOL extension = dot && length - (dot - name) <= 4;
    if (extension && _wcsicmp(dot + 1, L"job"))
        return E_INVALIDARG;
    SIZE_T total = length + (extension ? 0 : 4) + (relative ? wcslen(schedule->folder) + 1 : 0) + 1;
    WCHAR *result = CoTaskMemAlloc(total * sizeof(WCHAR));
    if (!result)
        return E_OUTOFMEMORY;
    result[0] = 0;
    if (relative) {
        wcscpy(result, schedule->folder);
        wcscat(result, L"\\");
    }
    wcscat(result, name);
    if (!extension)
        wcscat(result, L".job");
    *path = result;
    return S_OK;
}

/* ------------------------------------------------------------------ the enumerator of the names */

static ENUMERATOR *enumerator_of(IEnumWorkItems *This) { return (ENUMERATOR *)This; }

static HRESULT STDMETHODCALLTYPE enumerator_query_interface(IEnumWorkItems *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IEnumWorkItems))
        return E_NOINTERFACE;
    *object = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE enumerator_add_ref(IEnumWorkItems *This)
{
    return InterlockedIncrement(&enumerator_of(This)->refs);
}

static ULONG STDMETHODCALLTYPE enumerator_release(IEnumWorkItems *This)
{
    ENUMERATOR *enumerator = enumerator_of(This);
    LONG refs = InterlockedDecrement(&enumerator->refs);
    if (!refs) {
        for (ULONG i = 0; i < enumerator->count; i++)
            HeapFree(GetProcessHeap(), 0, enumerator->names[i]);
        HeapFree(GetProcessHeap(), 0, enumerator->names);
        HeapFree(GetProcessHeap(), 0, enumerator);
        InterlockedDecrement(&g_mstask_objects);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE enumerator_next(IEnumWorkItems *This, ULONG count, LPWSTR **names, ULONG *fetched)
{
    ENUMERATOR *enumerator = enumerator_of(This);
    if (!names || (!fetched && count != 1))
        return E_INVALIDARG;
    *names = nullptr;
    if (fetched)
        *fetched = 0;
    ULONG available = enumerator->count - enumerator->position;
    ULONG given = count < available ? count : available;
    if (given) {
        LPWSTR *list = CoTaskMemAlloc(given * sizeof(LPWSTR));
        if (!list)
            return E_OUTOFMEMORY;
        for (ULONG i = 0; i < given; i++) {
            list[i] = MstaskDuplicate(enumerator->names[enumerator->position + i]);
            if (!list[i]) {
                while (i--)
                    CoTaskMemFree(list[i]);
                CoTaskMemFree(list);
                return E_OUTOFMEMORY;
            }
        }
        *names = list;
    }
    enumerator->position += given;
    if (fetched)
        *fetched = given;
    return given == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enumerator_skip(IEnumWorkItems *This, ULONG count)
{
    ENUMERATOR *enumerator = enumerator_of(This);
    if (count > enumerator->count - enumerator->position) {
        enumerator->position = enumerator->count;
        return S_FALSE;
    }
    enumerator->position += count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enumerator_reset(IEnumWorkItems *This)
{
    enumerator_of(This)->position = 0;
    return S_OK;
}

static const IEnumWorkItemsVtbl g_enumerator_vtbl;

static HRESULT enumerator_make(LPWSTR *names, ULONG count, ULONG position, IEnumWorkItems **out)
{
    ENUMERATOR *enumerator = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*enumerator));
    if (!enumerator)
        return E_OUTOFMEMORY;
    enumerator->items.lpVtbl = &g_enumerator_vtbl;
    enumerator->refs = 1;
    enumerator->names = names;
    enumerator->count = count;
    enumerator->position = position;
    InterlockedIncrement(&g_mstask_objects);
    *out = &enumerator->items;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enumerator_clone(IEnumWorkItems *This, IEnumWorkItems **out)
{
    ENUMERATOR *enumerator = enumerator_of(This);
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    LPWSTR *names = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (enumerator->count + 1) * sizeof(LPWSTR));
    if (!names)
        return E_OUTOFMEMORY;
    for (ULONG i = 0; i < enumerator->count; i++) {
        SIZE_T size = (wcslen(enumerator->names[i]) + 1) * sizeof(WCHAR);
        names[i] = HeapAlloc(GetProcessHeap(), 0, size);
        if (!names[i]) {
            while (i--)
                HeapFree(GetProcessHeap(), 0, names[i]);
            HeapFree(GetProcessHeap(), 0, names);
            return E_OUTOFMEMORY;
        }
        memcpy(names[i], enumerator->names[i], size);
    }
    HRESULT result = enumerator_make(names, enumerator->count, enumerator->position, out);
    if (FAILED(result)) {
        for (ULONG i = 0; i < enumerator->count; i++)
            HeapFree(GetProcessHeap(), 0, names[i]);
        HeapFree(GetProcessHeap(), 0, names);
    }
    return result;
}

static const IEnumWorkItemsVtbl g_enumerator_vtbl = {
    enumerator_query_interface, enumerator_add_ref, enumerator_release, enumerator_next, enumerator_skip,
    enumerator_reset, enumerator_clone,
};

/* ------------------------------------------------------------------ the scheduler */

static HRESULT STDMETHODCALLTYPE schedule_query_interface(ITaskScheduler *This, REFIID riid, void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_ITaskScheduler))
        return E_NOINTERFACE;
    *object = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE schedule_add_ref(ITaskScheduler *This)
{
    return InterlockedIncrement(&schedule_of(This)->refs);
}

static ULONG STDMETHODCALLTYPE schedule_release(ITaskScheduler *This)
{
    SCHEDULE *schedule = schedule_of(This);
    LONG refs = InterlockedDecrement(&schedule->refs);
    if (!refs) {
        HeapFree(GetProcessHeap(), 0, schedule);
        InterlockedDecrement(&g_mstask_objects);
    }
    return refs;
}

/* the name of this computer as the scheduler is told it, with two backslashes before it. */
static BOOL is_local_computer(LPCWSTR computer)
{
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = RTL_NUMBER_OF(name);
    if (!computer || !*computer)
        return TRUE;
    if (computer[0] == L'\\' && computer[1] == L'\\')
        computer += 2;
    return GetComputerNameW(name, &size) && !_wcsicmp(computer, name);
}

static HRESULT STDMETHODCALLTYPE schedule_set_target_computer(ITaskScheduler *This, LPCWSTR computer)
{
    return is_local_computer(computer) ? S_OK : HRESULT_FROM_WIN32(ERROR_BAD_NETPATH);
}

static HRESULT STDMETHODCALLTYPE schedule_get_target_computer(ITaskScheduler *This, LPWSTR *computer)
{
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 3] = L"\\\\";
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (!computer)
        return E_INVALIDARG;
    *computer = nullptr;
    if (!GetComputerNameW(name + 2, &size))
        return HRESULT_FROM_WIN32(GetLastError());
    *computer = MstaskDuplicate(name);
    return *computer ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE schedule_enum(ITaskScheduler *This, IEnumWorkItems **out)
{
    SCHEDULE *schedule = schedule_of(This);
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    WCHAR pattern[MAX_PATH + 8];
    _snwprintf(pattern, RTL_NUMBER_OF(pattern), L"%s\\*.job", schedule->folder);
    ULONG capacity = 16, count = 0;
    LPWSTR *names = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity * sizeof(LPWSTR));
    if (!names)
        return E_OUTOFMEMORY;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern, &data);
    HRESULT result = S_OK;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            if (count == capacity) {
                LPWSTR *grown = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, names, capacity * 2 * sizeof(LPWSTR));
                if (!grown) {
                    result = E_OUTOFMEMORY;
                    break;
                }
                names = grown;
                capacity *= 2;
            }
            SIZE_T size = (wcslen(data.cFileName) + 1) * sizeof(WCHAR);
            names[count] = HeapAlloc(GetProcessHeap(), 0, size);
            if (!names[count]) {
                result = E_OUTOFMEMORY;
                break;
            }
            memcpy(names[count++], data.cFileName, size);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    if (SUCCEEDED(result))
        result = enumerator_make(names, count, 0, out);
    if (FAILED(result)) {
        for (ULONG i = 0; i < count; i++)
            HeapFree(GetProcessHeap(), 0, names[i]);
        HeapFree(GetProcessHeap(), 0, names);
    }
    return result;
}

static HRESULT STDMETHODCALLTYPE schedule_activate(ITaskScheduler *This, LPCWSTR name, REFIID riid, IUnknown **unknown)
{
    WCHAR *path;
    if (!name || !unknown)
        return E_INVALIDARG;
    *unknown = nullptr;
    HRESULT result = job_path(schedule_of(This), name, &path);
    if (FAILED(result))
        return result;
    result = JobLoad(path, riid, (void **)unknown);
    CoTaskMemFree(path);
    return result;
}

static HRESULT STDMETHODCALLTYPE schedule_delete(ITaskScheduler *This, LPCWSTR name)
{
    WCHAR *path;
    if (!name)
        return E_INVALIDARG;
    HRESULT result = job_path(schedule_of(This), name, &path);
    if (FAILED(result))
        return result;
    if (!DeleteFileW(path))
        result = HRESULT_FROM_WIN32(GetLastError());
    else
        JobForgetAccount(path);
    CoTaskMemFree(path);
    return result;
}

static HRESULT STDMETHODCALLTYPE schedule_new_work_item(ITaskScheduler *This, LPCWSTR name, REFCLSID clsid, REFIID riid,
                                                        IUnknown **unknown)
{
    WCHAR *path;
    if (!name || !unknown)
        return E_INVALIDARG;
    *unknown = nullptr;
    if (!IsEqualCLSID(clsid, &CLSID_CTask))
        return REGDB_E_CLASSNOTREG;
    HRESULT result = job_path(schedule_of(This), name, &path);
    if (FAILED(result))
        return result;
    DWORD attributes = GetFileAttributesW(path);
    if (attributes != INVALID_FILE_ATTRIBUTES)
        result = HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);
    else if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
        result = HRESULT_FROM_WIN32(GetLastError());
    else
        result = JobCreate(path, riid, (void **)unknown);
    CoTaskMemFree(path);
    return result;
}

static HRESULT STDMETHODCALLTYPE schedule_add_work_item(ITaskScheduler *This, LPCWSTR name, IScheduledWorkItem *item)
{
    WCHAR *path;
    IPersistFile *persist;
    if (!name || !item)
        return E_INVALIDARG;
    HRESULT result = job_path(schedule_of(This), name, &path);
    if (FAILED(result))
        return result;
    result = item->lpVtbl->QueryInterface(item, &IID_IPersistFile, (void **)&persist);
    if (SUCCEEDED(result)) {
        result = persist->lpVtbl->Save(persist, path, TRUE);
        persist->lpVtbl->Release(persist);
    }
    CoTaskMemFree(path);
    return result;
}

static HRESULT STDMETHODCALLTYPE schedule_is_of_type(ITaskScheduler *This, LPCWSTR name, REFIID riid)
{
    WCHAR *path;
    if (!name)
        return E_INVALIDARG;
    HRESULT result = job_path(schedule_of(This), name, &path);
    if (FAILED(result))
        return result;
    DWORD attributes = GetFileAttributesW(path);
    CoTaskMemFree(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return HRESULT_FROM_WIN32(GetLastError());
    return IsEqualIID(riid, &IID_ITask) || IsEqualIID(riid, &IID_IScheduledWorkItem) ? S_OK : S_FALSE;
}

static const ITaskSchedulerVtbl g_schedule_vtbl = {
    schedule_query_interface, schedule_add_ref, schedule_release, schedule_set_target_computer,
    schedule_get_target_computer, schedule_enum, schedule_activate, schedule_delete, schedule_new_work_item,
    schedule_add_work_item, schedule_is_of_type,
};

HRESULT ScheduleCreate(REFIID riid, void **object)
{
    SCHEDULE *schedule = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*schedule));
    if (!schedule)
        return E_OUTOFMEMORY;
    schedule->scheduler.lpVtbl = &g_schedule_vtbl;
    schedule->refs = 1;
    HRESULT result = MstaskTasksFolder(schedule->folder);
    if (SUCCEEDED(result))
        result = ensure_folder(schedule->folder);
    if (FAILED(result)) {
        HeapFree(GetProcessHeap(), 0, schedule);
        return result;
    }
    InterlockedIncrement(&g_mstask_objects);
    result = schedule->scheduler.lpVtbl->QueryInterface(&schedule->scheduler, riid, object);
    schedule->scheduler.lpVtbl->Release(&schedule->scheduler);
    return result;
}
