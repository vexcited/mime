#include "dosvc.h"

#define ROOT_PATH L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\DeliveryOptimization"
#define STATE_DIR L"%SystemRoot%\\ServiceProfiles\\NetworkService\\AppData\\Local\\Microsoft\\Windows\\DeliveryOptimization\\State"

static HKEY g_root, g_state;
static SRWLOCK g_store_lock = SRWLOCK_INIT;

/*
 * the registry of the service is in the network service's hive, or below HKLM where RedirectionKey points; the jobs that
 * persist are in a private hive of the service's profile, with the registry as the place they go when it cannot load.
 */
HKEY StoreRoot(void)
{
    AcquireSRWLockExclusive(&g_store_lock);
    if (!g_root) {
        WCHAR redirect[MAX_PATH];
        DWORD size = sizeof(redirect);
        HKEY key = nullptr;
        if (!RegGetValueW(HKEY_LOCAL_MACHINE, ROOT_PATH, L"RedirectionKey", RRF_RT_REG_SZ, nullptr, redirect, &size) &&
            redirect[0])
            RegCreateKeyExW(HKEY_LOCAL_MACHINE, redirect, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr);
        if (!key)
            RegCreateKeyExW(HKEY_USERS, L"S-1-5-20\\" ROOT_PATH, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key,
                            nullptr);
        if (!key)
            RegCreateKeyExW(HKEY_LOCAL_MACHINE, ROOT_PATH, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr);
        g_root = key ? key : HKEY_LOCAL_MACHINE;
    }
    HKEY root = g_root;
    ReleaseSRWLockExclusive(&g_store_lock);
    return root;
}

static void make_dirs(WCHAR *path)
{
    for (WCHAR *p = path + 3; *p; p++) {
        if (*p != '\\')
            continue;
        *p = 0;
        CreateDirectoryW(path, nullptr);
        *p = '\\';
    }
    CreateDirectoryW(path, nullptr);
}

static HKEY state_key(void)
{
    AcquireSRWLockExclusive(&g_store_lock);
    if (!g_state) {
        WCHAR dir[MAX_PATH], file[MAX_PATH];
        HKEY hive = nullptr, key = nullptr;
        if (ExpandEnvironmentStringsW(STATE_DIR, dir, ARRAYSIZE(dir))) {
            make_dirs(dir);
            _snwprintf(file, ARRAYSIZE(file), L"%s\\dosvcState.dat", dir);
            file[ARRAYSIZE(file) - 1] = 0;
            if (!RegLoadAppKeyW(file, &hive, KEY_ALL_ACCESS, 0, 0)) {
                RegCreateKeyExW(hive, L"DeliveryOptimization\\Jobs", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr);
                RegCloseKey(hive);
            }
        }
        if (!key) {
            ReleaseSRWLockExclusive(&g_store_lock);
            HKEY root = StoreRoot();
            AcquireSRWLockExclusive(&g_store_lock);
            RegCreateKeyExW(root, L"Jobs", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr);
        }
        g_state = key;
    }
    HKEY key = g_state;
    ReleaseSRWLockExclusive(&g_store_lock);
    return key;
}

static void id_name(const GUID *g, WCHAR *out, size_t count)
{
    StringFromGUID2(g, out, (int)count);
}

static void put_text(HKEY key, const WCHAR *name, const WCHAR *text)
{
    if (text)
        RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)text, (DWORD)((wcslen(text) + 1) * sizeof(WCHAR)));
    else
        RegDeleteValueW(key, name);
}

static void put_dword(HKEY key, const WCHAR *name, DWORD value)
{
    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
}

static void put_qword(HKEY key, const WCHAR *name, UINT64 value)
{
    RegSetValueExW(key, name, 0, REG_QWORD, (const BYTE *)&value, sizeof(value));
}

/* the properties kept for a job that outlives the service: the plain values, by their ids. */
static void put_props(HKEY key, DO_JOB *job)
{
    HKEY props;
    if (RegCreateKeyExW(key, L"Properties", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &props, nullptr))
        return;
    for (int i = 0; i < P_COUNT; i++) {
        WCHAR name[8];
        _snwprintf(name, ARRAYSIZE(name), L"%d", i);
        name[ARRAYSIZE(name) - 1] = 0;
        const VARIANT *v = &job->props[i];
        switch (V_VT(v)) {
        case VT_BSTR:
            put_text(props, name, V_BSTR(v));
            break;
        case VT_UI4:
            put_dword(props, name, V_UI4(v));
            break;
        case VT_BOOL:
            RegSetValueExW(props, name, 0, REG_BINARY, (const BYTE *)&V_BOOL(v), sizeof(V_BOOL(v)));
            break;
        case VT_UI8:
            put_qword(props, name, V_UI8(v));
            break;
        default:
            RegDeleteValueW(props, name);
        }
    }
    RegCloseKey(props);
}

void StoreSaveJob(DO_JOB *job)
{
    HKEY jobs = state_key();
    if (!jobs)
        return;
    WCHAR name[40];
    id_name(&job->id, name, ARRAYSIZE(name));
    HKEY key;
    if (RegCreateKeyExW(jobs, name, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr))
        return;
    AcquireSRWLockShared(&job->lock);
    int state = job->state == JOB_TRANSFERRING ? JOB_PAUSED : job->state;
    put_dword(key, L"State", (DWORD)state);
    put_dword(key, L"Error", (DWORD)job->error);
    put_dword(key, L"ExtendedError", (DWORD)job->ext_error);
    put_text(key, L"Owner", job->owner_sid);
    put_text(key, L"RemoteFile", job->remote);
    put_text(key, L"LocalFile", job->local);
    put_text(key, L"TempLocalFile", job->temp);
    put_qword(key, L"FileSize", job->size_known ? job->total : (UINT64)-1);
    put_qword(key, L"Transferred", job->transferred);
    put_dword(key, L"HasFile", job->has_file);
    put_qword(key, L"Created", (UINT64)job->created.dwHighDateTime << 32 | job->created.dwLowDateTime);
    if (job->range_count)
        RegSetValueExW(key, L"Ranges", 0, REG_BINARY, (const BYTE *)job->ranges,
                       job->range_count * (DWORD)sizeof(DO_DOWNLOAD_RANGE));
    else
        RegDeleteValueW(key, L"Ranges");
    put_props(key, job);
    ReleaseSRWLockShared(&job->lock);
    RegCloseKey(key);
}

void StoreDeleteJob(DO_JOB *job)
{
    HKEY jobs = state_key();
    if (!jobs)
        return;
    WCHAR name[40];
    id_name(&job->id, name, ARRAYSIZE(name));
    RegDeleteTreeW(jobs, name);
}

static WCHAR *get_text(HKEY key, const WCHAR *name)
{
    DWORD size = 0;
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) || !size)
        return nullptr;
    WCHAR *text = LocalAlloc(LMEM_FIXED, size);
    if (text && RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, text, &size)) {
        LocalFree(text);
        return nullptr;
    }
    return text;
}

static DWORD get_dword(HKEY key, const WCHAR *name, DWORD fallback)
{
    DWORD value, size = sizeof(value);
    return RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) ? fallback : value;
}

static UINT64 get_qword(HKEY key, const WCHAR *name, UINT64 fallback)
{
    UINT64 value;
    DWORD size = sizeof(value);
    return RegGetValueW(key, nullptr, name, RRF_RT_REG_QWORD, nullptr, &value, &size) ? fallback : value;
}

static void get_props(HKEY key, DO_JOB *job)
{
    HKEY props;
    if (RegOpenKeyExW(key, L"Properties", 0, KEY_READ, &props))
        return;
    for (DWORD i = 0;; i++) {
        WCHAR name[16];
        BYTE data[8192];
        DWORD name_size = ARRAYSIZE(name), size = sizeof(data) - sizeof(WCHAR), type;
        if (RegEnumValueW(props, i, name, &name_size, nullptr, &type, data, &size))
            break;
        int id = 0;
        for (const WCHAR *p = name; *p >= '0' && *p <= '9'; p++)
            id = id * 10 + (*p - '0');
        if (id < 0 || id >= P_COUNT)
            continue;
        VARIANT *v = &job->props[id];
        VariantClear(v);
        if (type == REG_SZ) {
            data[size] = data[size + 1] = 0;
            V_VT(v) = VT_BSTR;
            V_BSTR(v) = SysAllocString((const WCHAR *)data);
        } else if (type == REG_DWORD && size == 4) {
            V_VT(v) = VT_UI4;
            memcpy(&V_UI4(v), data, 4);
        } else if (type == REG_QWORD && size == 8) {
            V_VT(v) = VT_UI8;
            memcpy(&V_UI8(v), data, 8);
        } else if (type == REG_BINARY && size == sizeof(VARIANT_BOOL)) {
            V_VT(v) = VT_BOOL;
            memcpy(&V_BOOL(v), data, sizeof(VARIANT_BOOL));
        }
    }
    RegCloseKey(props);
}

/* the jobs that persisted come back paused where they were transferring, and wait for their owner to start them. */
void StoreLoadJobs(void)
{
    HKEY jobs = state_key();
    if (!jobs)
        return;
    for (DWORD i = 0;; i++) {
        WCHAR name[64];
        DWORD length = ARRAYSIZE(name);
        if (RegEnumKeyExW(jobs, i, name, &length, nullptr, nullptr, nullptr, nullptr))
            break;
        GUID id;
        HKEY key;
        if (FAILED(CLSIDFromString(name, &id)) || RegOpenKeyExW(jobs, name, 0, KEY_READ, &key))
            continue;
        DO_JOB *job;
        if (SUCCEEDED(JobCreate(&job))) {
            job->id = id;
            job->state = (int)get_dword(key, L"State", JOB_PAUSED);
            if (job->state == JOB_TRANSFERRING)
                job->state = JOB_PAUSED;
            job->error = (HRESULT)get_dword(key, L"Error", 0);
            job->ext_error = (HRESULT)get_dword(key, L"ExtendedError", 0);
            if (job->owner_sid)
                CoTaskMemFree(job->owner_sid);
            WCHAR *owner = get_text(key, L"Owner");
            job->owner_sid = nullptr;
            if (owner) {
                size_t size = (wcslen(owner) + 1) * sizeof(WCHAR);
                job->owner_sid = CoTaskMemAlloc(size);
                if (job->owner_sid)
                    memcpy(job->owner_sid, owner, size);
                LocalFree(owner);
            }
            job->remote = get_text(key, L"RemoteFile");
            job->local = get_text(key, L"LocalFile");
            job->temp = get_text(key, L"TempLocalFile");
            UINT64 size = get_qword(key, L"FileSize", (UINT64)-1);
            job->size_known = size != (UINT64)-1;
            job->total = job->size_known ? size : 0;
            job->transferred = get_qword(key, L"Transferred", 0);
            job->has_file = get_dword(key, L"HasFile", 0) != 0;
            UINT64 created = get_qword(key, L"Created", 0);
            job->created.dwLowDateTime = (DWORD)created;
            job->created.dwHighDateTime = (DWORD)(created >> 32);
            DWORD bytes = 0;
            if (!RegGetValueW(key, nullptr, L"Ranges", RRF_RT_REG_BINARY, nullptr, nullptr, &bytes) && bytes) {
                job->ranges = LocalAlloc(LMEM_FIXED, bytes);
                if (job->ranges &&
                    !RegGetValueW(key, nullptr, L"Ranges", RRF_RT_REG_BINARY, nullptr, job->ranges, &bytes))
                    job->range_count = bytes / sizeof(DO_DOWNLOAD_RANGE);
            }
            get_props(key, job);
            JobRelease(job);
        }
        RegCloseKey(key);
    }
}
