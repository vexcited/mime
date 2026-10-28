/* What the rules of features need from the machine: the registry, the drive, the question and the steps. */
#include "featurep.h"

#define DECLINE_SECONDS 600

static ULONGLONG now_filetime(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (ULONGLONG)ft.dwHighDateTime << 32 | ft.dwLowDateTime;
}

void WidenName(const char *name, WCHAR *wide, SIZE_T chars)
{
    SIZE_T i = 0;
    for (; name[i] && i + 1 < chars; i++)
        wide[i] = (WCHAR)(unsigned char)name[i];
    wide[i] = 0;
}

/* ------------------------------------------------------------------ registry */

static BOOL read_dword(PCWSTR key, PCWSTR name, DWORD *value)
{
    DWORD size = sizeof(*value), type;
    return RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_DWORD, &type, value, &size) == ERROR_SUCCESS;
}

static BOOL read_qword(PCWSTR key, PCWSTR name, ULONGLONG *value)
{
    DWORD size = sizeof(*value), type;
    return RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_QWORD, &type, value, &size) == ERROR_SUCCESS;
}

static void write_value(PCWSTR key, PCWSTR name, DWORD type, const void *data, DWORD size)
{
    RegSetKeyValueW(HKEY_LOCAL_MACHINE, key, name, type, data, size);
}

static void state_key(const FEATURE_DEF *feature, WCHAR *key, SIZE_T chars)
{
    WCHAR name[64];
    WidenName(feature->Name, name, RTL_NUMBER_OF(name));
    _snwprintf(key, chars, MIME_FEATURE_STATE_KEY L"\\%ls", name);
    key[chars - 1] = 0;
}

static FEATURE_POLICY ops_policy(void *context, const FEATURE_DEF *feature)
{
    WCHAR key[160], name[64];
    WidenName(feature->Name, name, RTL_NUMBER_OF(name));
    _snwprintf(key, RTL_NUMBER_OF(key), MIME_FEATURE_POLICY_KEY L"\\%ls", name);
    key[RTL_NUMBER_OF(key) - 1] = 0;
    DWORD value;
    if (!read_dword(key, L"Consent", &value) && !read_dword(MIME_FEATURE_POLICY_KEY, L"Consent", &value))
        return FEATURE_POLICY_ASK;
    return value == 1 ? FEATURE_POLICY_ALLOW : value == 2 ? FEATURE_POLICY_DENY : FEATURE_POLICY_ASK;
}

BOOL FeatureLoadState(const FEATURE_DEF *feature, FEATURE_STATE *state, DWORD *error)
{
    WCHAR key[160];
    state_key(feature, key, RTL_NUMBER_OF(key));
    DWORD value = 0;
    read_dword(key, L"State", &value);
    *state = value == 1 ? FEATURE_STATE_ENABLED : value == 2 ? FEATURE_STATE_DISABLED : FEATURE_STATE_NONE;
    *error = 0;
    read_dword(key, L"LastError", error);
    return TRUE;
}

static FEATURE_STATE ops_get_state(void *context, const FEATURE_DEF *feature)
{
    FEATURE_STATE state;
    DWORD error;
    FeatureLoadState(feature, &state, &error);
    return state;
}

static void ops_set_state(void *context, const FEATURE_DEF *feature, FEATURE_STATE state, uint32_t error, bool from_drive)
{
    RUN *run = context;
    WCHAR key[160], version[64];
    state_key(feature, key, RTL_NUMBER_OF(key));
    DWORD value = state == FEATURE_STATE_ENABLED ? 1 : state == FEATURE_STATE_DISABLED ? 2 : 0;
    write_value(key, L"State", REG_DWORD, &value, sizeof(value));
    write_value(key, L"LastError", REG_DWORD, &error, sizeof(error));
    ULONGLONG time = now_filetime();
    write_value(key, L"Time", REG_QWORD, &time, sizeof(time));
    if (state == FEATURE_STATE_ENABLED && !error) {
        FeatureInstalledVersion(run, feature, version, RTL_NUMBER_OF(version));
        write_value(key, L"Version", REG_SZ, version, (DWORD)((wcslen(version) + 1) * sizeof(WCHAR)));
        PCWSTR source = from_drive ? L"drive" : run->Source[0] ? L"file" : L"download";
        write_value(key, L"Source", REG_SZ, source, (DWORD)((wcslen(source) + 1) * sizeof(WCHAR)));
    }
}

static bool ops_recently_declined(void *context, const FEATURE_DEF *feature)
{
    WCHAR key[160];
    state_key(feature, key, RTL_NUMBER_OF(key));
    ULONGLONG then, now = now_filetime();
    return read_qword(key, L"Declined", &then) && now >= then && (now - then) / 10000000ull < DECLINE_SECONDS;
}

static void ops_set_declined(void *context, const FEATURE_DEF *feature)
{
    WCHAR key[160];
    state_key(feature, key, RTL_NUMBER_OF(key));
    ULONGLONG time = now_filetime();
    write_value(key, L"Declined", REG_QWORD, &time, sizeof(time));
}

/* Counts a request of a program, whatever becomes of it: how often it was asked for, when, and by whom. */
void RecordRequest(const FEATURE_DEF *feature, PCWSTR caller)
{
    WCHAR key[160];
    state_key(feature, key, RTL_NUMBER_OF(key));
    DWORD count = 0;
    read_dword(key, L"Requests", &count);
    count++;
    write_value(key, L"Requests", REG_DWORD, &count, sizeof(count));
    ULONGLONG time = now_filetime();
    write_value(key, L"LastRequest", REG_QWORD, &time, sizeof(time));
    if (caller && caller[0])
        write_value(key, L"LastCaller", REG_SZ, caller, (DWORD)((wcslen(caller) + 1) * sizeof(WCHAR)));
}

DWORD FeatureRequestCount(const FEATURE_DEF *feature)
{
    WCHAR key[160];
    state_key(feature, key, RTL_NUMBER_OF(key));
    DWORD count = 0;
    read_dword(key, L"Requests", &count);
    return count;
}

/* ------------------------------------------------------------------ the run */

void RunInit(RUN *run, const FEATURE_DEF *feature)
{
    memset(run, 0, sizeof(*run));
    run->Feature = feature;
    InitializeCriticalSection(&run->Lock);
    WCHAR windows[MAX_PATH];
    UINT n = GetWindowsDirectoryW(windows, MAX_PATH);
    if (n >= 3 && windows[1] == L':') {
        memcpy(run->Root, windows, 3 * sizeof(WCHAR));
    } else {
        wcscpy(run->Root, L"C:\\");
    }
    run->Root[3] = 0;
    _snwprintf(run->Cache, RTL_NUMBER_OF(run->Cache), L"%ls" CACHE_FOLDER, run->Root);
    run->Cache[RTL_NUMBER_OF(run->Cache) - 1] = 0;
}

void RunReport(RUN *run, ULONGLONG done, ULONGLONG total, PCWSTR step)
{
    EnterCriticalSection(&run->Lock);
    run->Done = done;
    run->Total = total;
    if (step)
        lstrcpynW(run->Step, step, RTL_NUMBER_OF(run->Step));
    LeaveCriticalSection(&run->Lock);
    ConsoleProgress(run, FALSE);
}

void RunSnapshot(RUN *run, FEATURE_STAGE *stage, ULONGLONG *done, ULONGLONG *total, WCHAR *step, SIZE_T chars)
{
    EnterCriticalSection(&run->Lock);
    *stage = run->Stage;
    *done = run->Done;
    *total = run->Total;
    lstrcpynW(step, run->Step, (int)chars);
    LeaveCriticalSection(&run->Lock);
}

/* ------------------------------------------------------------------ ops of the run */

static uint32_t ops_available(void *context, const FEATURE_DEF *feature)
{
    return HandlerAvailable(feature);
}

static bool ops_present(void *context, const FEATURE_DEF *feature)
{
    return HandlerPresent(context, feature);
}

static uint32_t ops_consent(void *context, const FEATURE_DEF *feature)
{
    RUN *run = context;
    return run->Dialogs ? UiConsent(run) : FEATURE_OK;
}

static void ops_stage(void *context, const FEATURE_DEF *feature, FEATURE_STAGE stage)
{
    RUN *run = context;
    EnterCriticalSection(&run->Lock);
    run->Stage = stage;
    run->Done = run->Total = 0;
    run->Step[0] = 0;
    LeaveCriticalSection(&run->Lock);
    if (run->Console) {
        static const WCHAR *const texts[] = {L"Downloading the files", L"Downloading the files", L"Checking the files",
                                             L"Installing", L"Removing"};
        ConsoleProgressEnd(run);
        tu_print(L"%ls...\n", stage == FEATURE_STAGE_COLLECTING && run->Source[0] ? L"Reading the file" : texts[stage]);
        tu_flush();
        ConsoleProgress(run, TRUE);
    }
    UiStage(run);
}

static BOOL file_exists(PCWSTR path)
{
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static uint32_t ops_acquire(void *context, const FEATURE_DEF *feature, void **package)
{
    RUN *run = context;
    if (run->Source[0]) {
        if (!file_exists(run->Source))
            return ERROR_FILE_NOT_FOUND;
        lstrcpynW(run->Package, run->Source, RTL_NUMBER_OF(run->Package));
        run->PackageOwned = FALSE;
        *package = run;
        return FEATURE_OK;
    }
    const char *url8 = FeatureUrl(feature, MACHINE_ARCH);
    if (!url8 || !FeatureUrlAllowedFor(feature, url8))
        return ERROR_NOT_SUPPORTED;
    WCHAR url[256], name[64];
    WidenName(url8, url, RTL_NUMBER_OF(url));
    WidenName(feature->Name, name, RTL_NUMBER_OF(name));
    /* The folders below the drive, one level at a time. */
    WCHAR folder[MAX_PATH];
    _snwprintf(folder, RTL_NUMBER_OF(folder), L"%ls" L"ProgramData\\Mime", run->Root);
    folder[RTL_NUMBER_OF(folder) - 1] = 0;
    CreateDirectoryW(folder, nullptr);
    CreateDirectoryW(run->Cache, nullptr);
    PCWSTR extension = feature->Format == FEATURE_FORMAT_CABINET ? L"cab"
                       : feature->Format == FEATURE_FORMAT_PROGRAM ? L"exe" : L"pack";
    _snwprintf(run->Package, RTL_NUMBER_OF(run->Package), L"%ls\\%ls.%ls", run->Cache, name, extension);
    run->Package[RTL_NUMBER_OF(run->Package) - 1] = 0;
    DeleteFileW(run->Package);
    DWORD error;
    if (feature->Format == FEATURE_FORMAT_ZIP)
        error = AcquireZip(run, feature, url, run->Package);
    else if (feature->Format == FEATURE_FORMAT_WIM)
        error = AcquireWim(run, feature, url, run->Package);
    else
        error = DownloadPackage(run, feature, url, run->Package);
    if (error != ERROR_SUCCESS)
        return error;
    run->PackageOwned = TRUE;
    *package = run;
    return FEATURE_OK;
}

static uint32_t ops_verify(void *context, const FEATURE_DEF *feature, void *package)
{
    RUN *run = context;
    return VerifyPackage(run, feature, run->Package);
}

static uint32_t ops_install(void *context, const FEATURE_DEF *feature, void *package)
{
    RUN *run = context;
    EnterCriticalSection(&run->Lock);
    run->Done = run->Total = 0;
    LeaveCriticalSection(&run->Lock);
    return HandlerInstall(run, feature, run->Package);
}

static void ops_discard(void *context, const FEATURE_DEF *feature, void *package)
{
    RUN *run = context;
    if (run->PackageOwned)
        DeleteFileW(run->Package);
    run->PackageOwned = FALSE;
}

static uint32_t ops_remove(void *context, const FEATURE_DEF *feature)
{
    return HandlerRemove(context, feature);
}

void FillOps(FEATURE_OPS *ops, RUN *run)
{
    *ops = (FEATURE_OPS){
        .Context = run,
        .Policy = ops_policy,
        .Available = ops_available,
        .Present = ops_present,
        .GetState = ops_get_state,
        .SetState = ops_set_state,
        .RecentlyDeclined = ops_recently_declined,
        .SetDeclined = ops_set_declined,
        .Consent = ops_consent,
        .Stage = ops_stage,
        .Acquire = ops_acquire,
        .Verify = ops_verify,
        .Install = ops_install,
        .Discard = ops_discard,
        .Remove = ops_remove,
    };
}

void FeatureInstalledVersion(RUN *run, const FEATURE_DEF *feature, WCHAR *version, SIZE_T chars)
{
    version[0] = 0;
    HandlerVersion(run, feature, version, chars);
    if (!version[0])
        WidenName(feature->Version, version, chars);
}

BOOL FeatureFilesPresent(RUN *run, const FEATURE_DEF *feature)
{
    return HandlerPresent(run, feature);
}

/* ------------------------------------------------------------------ files */

/* Deletes a folder and what is in it. Read only files lose the attribute first. */
void DeleteTree(PCWSTR path)
{
    WCHAR pattern[MAX_PATH];
    _snwprintf(pattern, RTL_NUMBER_OF(pattern), L"%ls\\*", path);
    pattern[RTL_NUMBER_OF(pattern) - 1] = 0;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW(pattern, FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L".."))
                continue;
            WCHAR child[MAX_PATH];
            _snwprintf(child, RTL_NUMBER_OF(child), L"%ls\\%ls", path, data.cFileName);
            child[RTL_NUMBER_OF(child) - 1] = 0;
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                DeleteTree(child);
            } else {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                    SetFileAttributesW(child, FILE_ATTRIBUTE_NORMAL);
                if (!DeleteFileW(child) && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    RemoveDirectoryW(child);
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    RemoveDirectoryW(path);
}
