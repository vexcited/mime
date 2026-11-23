/*
 * the task scheduler, ITaskSchedulerService of MS-TSCH, which genuine taskschd.dll calls over a dynamic endpoint of
 * ncalrpc the mapper names. windows serves it from the Schedule service, mime from the manager.
 *
 * a task is kept as windows keeps it: its xml, in the form taskxml.c writes, in a file of System32\Tasks named by its
 * path, and the keys of the task cache, the tree of its path with the id and the descriptor, the task's own key with
 * its path, its uri and DynamicInfo, which holds the time it was registered, the time it last ran and the result. what
 * the caller may do and what it reads back were measured with tsch.c and tfull.c of scratch/tasks in the research
 * repository, on windows 11 26300.
 */
#include "services.h"
#include <mime/win32/rpc.h>
#include "services/tsch.h"
#include "taskxml.h"

#define TASK_CACHE L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Schedule\\TaskCache"
#define TASK_VERSION 0x00010006

#define TASK_VALIDATE_ONLY 0x1
#define TASK_CREATE 0x2
#define TASK_UPDATE 0x4
#define TASK_DISABLE 0x8
#define TASK_IGNORE_REGISTRATION_TRIGGERS 0x20
#define TASK_ENUM_HIDDEN 0x1
#define TASK_RUN_IGNORE_CONSTRAINTS 0x2

#define TASK_LOGON_NONE 0
#define TASK_LOGON_PASSWORD 1
#define TASK_LOGON_S4U 2
#define TASK_LOGON_INTERACTIVE_TOKEN 3
#define TASK_LOGON_GROUP 4
#define TASK_LOGON_SERVICE_ACCOUNT 5
#define TASK_LOGON_INTERACTIVE_TOKEN_OR_PASSWORD 6

#define TASK_STATE_DISABLED 1
#define TASK_STATE_READY 3
#define TASK_STATE_RUNNING 4

#define SCHED_S_TASK_HAS_NOT_RUN_ ((HRESULT)0x00041303L)
#define SCHED_S_TASK_RUNNING_ ((HRESULT)0x00041301L)
#define SCHED_S_TASK_NOT_SCHEDULED_ ((HRESULT)0x00041305L)
#define SCHED_E_TASK_DISABLED_ ((HRESULT)0x80041326L)
#define SCHED_E_START_ON_DEMAND_ ((HRESULT)0x80041328L)
#define HR_WIN32(e) ((HRESULT)(((e) & 0xFFFF) | 0x80070000))

#define TokenUser_ 1
#define TokenElevationType_ 18
#define TokenLinkedToken_ 19
#define TokenElevation_ 20
#define TokenSessionId_ 12
#define TOKEN_ELEVATION_LIMITED 3
#define TOKEN_ELEVATION_FULL 2
#define SDDL_REVISION_1_ 1

WINBASEAPI BOOL WINAPI ConvertSecurityDescriptorToStringSecurityDescriptorW(PSECURITY_DESCRIPTOR sd, DWORD revision,
                                                                             DWORD information, LPWSTR *text,
                                                                             PULONG length);
WINBASEAPI DWORD WINAPI WTSGetActiveConsoleSessionId(void);

static CRITICAL_SECTION g_tsch_lock;
static HANDLE g_wake, g_registered;

/* what is known of a task besides its file. */
typedef struct TASK {
    struct TASK *Next;
    WCHAR *Path;
    WCHAR Id[40];
    ULONGLONG Created, LastRun;
    DWORD LastResult;
    BOOL HasRun;
    LONG Running;
    ULONGLONG Due; /* the next time a trigger starts it, 0 for none. */
} TASK;

/* a run of a task, which ends when its last action does. */
typedef struct INSTANCE {
    struct INSTANCE *Next;
    GUID Guid;
    TASK *Task;
    HANDLE Process;
    DWORD Pid;
    BOOL Stopped;
} INSTANCE;

static TASK *g_tasks;
static INSTANCE *g_instances;

/* ------------------------------------------------------------------ the caller */

typedef struct CALLER {
    HANDLE Token;    /* the caller's, for impersonation and to run as. */
    WCHAR Sid[96];
    BOOL Elevated;   /* an administrator with the full token, or the system. */
    BOOL Limited;    /* an administrator with the filtered one. */
} CALLER;

static void caller_free(CALLER *c)
{
    if (c->Token)
        CloseHandle(c->Token);
    c->Token = nullptr;
}

static BOOL caller_of(handle_t h, CALLER *c)
{
    memset(c, 0, sizeof(*c));
    if (RpcImpersonateClient(h))
        return FALSE;
    BOOL ok = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE, TRUE, &c->Token);
    RpcRevertToSelf();
    if (!ok)
        return FALSE;
    BYTE user[128];
    DWORD size = 0, type = 1, elevated = 0;
    WCHAR *text = nullptr;
    if (!GetTokenInformation(c->Token, TokenUser_, user, sizeof(user), &size) || !ConvertSidToStringSidW(*(PSID *)user, &text)) {
        caller_free(c);
        return FALSE;
    }
    lstrcpynW(c->Sid, text, RTL_NUMBER_OF(c->Sid));
    LocalFree(text);
    GetTokenInformation(c->Token, TokenElevationType_, &type, sizeof(type), &size);
    GetTokenInformation(c->Token, TokenElevation_, &elevated, sizeof(elevated), &size);
    c->Elevated = elevated || !wcscmp(c->Sid, L"S-1-5-18");
    c->Limited = type == TOKEN_ELEVATION_LIMITED;
    return TRUE;
}

/* ------------------------------------------------------------------ paths */

/* a path of the caller as the scheduler keeps it: rooted, no doubled or closing separator. */
static WCHAR *normal_path(const unsigned short *path)
{
    const WCHAR *p = (const WCHAR *)path;
    if (!p)
        return nullptr;
    SIZE_T n = wcslen(p);
    WCHAR *out = SvcAlloc((n + 2) * sizeof(WCHAR));
    if (!out)
        return nullptr;
    SIZE_T used = 0;
    out[used++] = L'\\';
    for (SIZE_T i = 0; i < n; i++) {
        WCHAR c = p[i] == L'/' ? L'\\' : p[i];
        if (c == L'\\' && out[used - 1] == L'\\')
            continue;
        out[used++] = c;
    }
    if (used > 1 && out[used - 1] == L'\\')
        used--;
    out[used] = 0;
    return out;
}

static BOOL valid_name(const WCHAR *path)
{
    for (const WCHAR *p = path; *p; p++)
        if (wcschr(L":*?\"<>|", *p) || *p < 0x20)
            return FALSE;
    return TRUE;
}

static WCHAR *file_of(const WCHAR *path)
{
    WCHAR dir[MAX_PATH];
    UINT n = GetSystemDirectoryW(dir, MAX_PATH);
    SIZE_T chars = n + 6 + wcslen(path) + 1;
    WCHAR *out = SvcAlloc(chars * sizeof(WCHAR));
    if (out)
        _snwprintf(out, chars, L"%ls\\Tasks%ls", dir, path[1] ? path : L"");
    return out;
}

static WCHAR *key_of(const WCHAR *part, const WCHAR *path)
{
    SIZE_T chars = wcslen(TASK_CACHE) + wcslen(part) + wcslen(path) + 4;
    WCHAR *out = SvcAlloc(chars * sizeof(WCHAR));
    if (out)
        _snwprintf(out, chars, L"%ls\\%ls%ls", TASK_CACHE, part, path[1] ? path : L"");
    return out;
}

static DWORD attributes(const WCHAR *path)
{
    WCHAR *file = file_of(path);
    DWORD a = file ? GetFileAttributesW(file) : INVALID_FILE_ATTRIBUTES;
    SvcFree(file);
    return a;
}

static BOOL is_folder(const WCHAR *path)
{
    DWORD a = attributes(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL is_task(const WCHAR *path)
{
    DWORD a = attributes(path);
    return path[1] && a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static WCHAR *parent_of(const WCHAR *path)
{
    const WCHAR *slash = wcsrchr(path, L'\\');
    return slash == path ? SvcDup(L"\\") : SvcDupN(path, (SIZE_T)(slash - path));
}

static WCHAR *rpc_string(const WCHAR *text)
{
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    WCHAR *out = MIDL_user_allocate(bytes);
    if (out)
        memcpy(out, text, bytes);
    return out;
}

/* ------------------------------------------------------------------ the cache */

static ULONGLONG now(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static TASK *task_find(const WCHAR *path)
{
    for (TASK *t = g_tasks; t; t = t->Next)
        if (!_wcsicmp(t->Path, path))
            return t;
    return nullptr;
}

/* DynamicInfo: a version of 3, the time of registration, the time of the last run, a state and the last result. */
typedef struct DYNAMIC_INFO {
    DWORD Version;
    ULONGLONG Created, LastRun;
    DWORD State, LastResult;
    ULONGLONG LastSuccess;
} __attribute__((packed)) DYNAMIC_INFO;
MIME_STATIC_ASSERT(sizeof(DYNAMIC_INFO) == 36, "DynamicInfo");

static void write_dynamic(const TASK *t)
{
    WCHAR key[160];
    _snwprintf(key, RTL_NUMBER_OF(key), TASK_CACHE L"\\Tasks\\%ls", t->Id);
    HKEY k;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr))
        return;
    DYNAMIC_INFO d = {3, t->Created, t->HasRun ? t->LastRun : 0, 0, t->HasRun ? t->LastResult : 0, 0};
    RegSetValueExW(k, L"DynamicInfo", 0, REG_BINARY, (const BYTE *)&d, sizeof(d));
    RegCloseKey(k);
}

static TASK *task_load(const WCHAR *path)
{
    TASK *t = task_find(path);
    if (t)
        return t;
    t = SvcAllocZero(sizeof(*t));
    if (!t || !(t->Path = SvcDup(path))) {
        SvcFree(t);
        return nullptr;
    }
    WCHAR *tree = key_of(L"Tree", path);
    HKEY k;
    DWORD size = sizeof(t->Id), type;
    if (tree && !RegOpenKeyExW(HKEY_LOCAL_MACHINE, tree, 0, KEY_READ, &k)) {
        if (RegQueryValueExW(k, L"Id", nullptr, &type, (BYTE *)t->Id, &size) || type != REG_SZ)
            t->Id[0] = 0;
        RegCloseKey(k);
    }
    SvcFree(tree);
    if (t->Id[0]) {
        WCHAR key[160];
        _snwprintf(key, RTL_NUMBER_OF(key), TASK_CACHE L"\\Tasks\\%ls", t->Id);
        DYNAMIC_INFO d = {};
        size = sizeof(d);
        if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &k)) {
            if (!RegQueryValueExW(k, L"DynamicInfo", nullptr, &type, (BYTE *)&d, &size) && size >= 28) {
                t->Created = d.Created;
                t->LastRun = d.LastRun;
                t->LastResult = d.LastResult;
                t->HasRun = d.LastRun != 0;
            }
            RegCloseKey(k);
        }
    }
    t->Next = g_tasks;
    g_tasks = t;
    return t;
}

static void task_forget(TASK *t)
{
    for (TASK **link = &g_tasks; *link; link = &(*link)->Next)
        if (*link == t) {
            *link = t->Next;
            break;
        }
    for (INSTANCE *i = g_instances; i; i = i->Next)
        if (i->Task == t)
            i->Task = nullptr;
    SvcFree(t->Path);
    SvcFree(t);
}

/* ------------------------------------------------------------------ files */

static WCHAR *read_task_file(const WCHAR *path)
{
    WCHAR *file = file_of(path);
    HANDLE f = file ? CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr)
                    : INVALID_HANDLE_VALUE;
    SvcFree(file);
    if (f == INVALID_HANDLE_VALUE)
        return nullptr;
    DWORD size = GetFileSize(f, nullptr), got = 0;
    WCHAR *text = size != INVALID_FILE_SIZE ? SvcAlloc(size + sizeof(WCHAR)) : nullptr;
    if (text && (!ReadFile(f, text, size, &got, nullptr) || got != size)) {
        SvcFree(text);
        text = nullptr;
    }
    CloseHandle(f);
    if (!text)
        return nullptr;
    text[got / sizeof(WCHAR)] = 0;
    if (text[0] == 0xFEFF)
        memmove(text, text + 1, got);
    return text;
}

/* the file in utf-16 with its mark, as windows writes it. */
static HRESULT write_task_file(const WCHAR *path, const WCHAR *text)
{
    WCHAR *file = file_of(path);
    HANDLE f = file ? CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)
                    : INVALID_HANDLE_VALUE;
    SvcFree(file);
    if (f == INVALID_HANDLE_VALUE)
        return HR_WIN32(GetLastError());
    static const WCHAR mark = 0xFEFF;
    DWORD written;
    BOOL ok = WriteFile(f, &mark, sizeof(mark), &written, nullptr) &&
              WriteFile(f, text, (DWORD)(wcslen(text) * sizeof(WCHAR)), &written, nullptr);
    CloseHandle(f);
    return ok ? S_OK : HR_WIN32(GetLastError());
}

/* the folders of a path, made when they are not there, each with its key of the tree. */
static HRESULT make_folders(const WCHAR *path, BOOL last)
{
    SIZE_T n = wcslen(path);
    WCHAR *part = SvcDup(path);
    if (!part)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    for (SIZE_T i = 1; i <= n && SUCCEEDED(hr); i++) {
        if (i < n && part[i] != L'\\')
            continue;
        if (i == n && !last)
            break;
        WCHAR saved = part[i];
        part[i] = 0;
        if (!is_folder(part)) {
            WCHAR *dir = file_of(part), *tree = key_of(L"Tree", part);
            HKEY k;
            if (!dir || !tree || !CreateDirectoryW(dir, nullptr))
                hr = dir ? HR_WIN32(GetLastError()) : E_OUTOFMEMORY;
            else if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, tree, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr))
                RegCloseKey(k);
            SvcFree(dir);
            SvcFree(tree);
        }
        part[i] = saved;
    }
    SvcFree(part);
    return hr;
}

static void format_guid(const GUID *g, WCHAR *out, SIZE_T chars)
{
    _snwprintf(out, chars, L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g->Data1, g->Data2, g->Data3,
               g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

/*
 * the keys of the cache: the tree's with the id, the index of the kind of task and the descriptor, the task's own,
 * and the one of its kind, Boot, Logon or Plain.
 */
static void write_cache(TASK *t, const TX_NODE *task, const WCHAR *user_sid)
{
    if (!t->Id[0]) {
        GUID g;
        UuidCreate((UUID *)&g);
        format_guid(&g, t->Id, RTL_NUMBER_OF(t->Id));
    }
    const WCHAR *kind = TxHasTrigger(task, L"BootTrigger") ? L"Boot" : TxHasTrigger(task, L"LogonTrigger") ? L"Logon" : L"Plain";
    DWORD index = kind[0] == L'B' ? 1 : kind[0] == L'L' ? 2 : 3;
    WCHAR *tree = key_of(L"Tree", t->Path);
    HKEY k;
    if (tree && !RegCreateKeyExW(HKEY_LOCAL_MACHINE, tree, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr)) {
        RegSetValueExW(k, L"Id", 0, REG_SZ, (const BYTE *)t->Id, (DWORD)((wcslen(t->Id) + 1) * sizeof(WCHAR)));
        RegSetValueExW(k, L"Index", 0, REG_DWORD, (const BYTE *)&index, sizeof(index));
        WCHAR sddl[200];
        _snwprintf(sddl, RTL_NUMBER_OF(sddl), L"O:BAG:SYD:AI(A;ID;0x1f019f;;;BA)(A;ID;0x1f019f;;;SY)(A;ID;FA;;;BA)(A;;0x120089;;;%ls)",
                   user_sid ? user_sid : L"SY");
        PSECURITY_DESCRIPTOR sd = nullptr;
        ULONG length = 0;
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1_, &sd, &length)) {
            RegSetValueExW(k, L"SD", 0, REG_BINARY, sd, length);
            LocalFree(sd);
        }
        RegCloseKey(k);
    }
    SvcFree(tree);
    WCHAR key[200];
    _snwprintf(key, RTL_NUMBER_OF(key), TASK_CACHE L"\\Tasks\\%ls", t->Id);
    if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr)) {
        DWORD bytes = (DWORD)((wcslen(t->Path) + 1) * sizeof(WCHAR)), schema = 65538;
        RegSetValueExW(k, L"Path", 0, REG_SZ, (const BYTE *)t->Path, bytes);
        RegSetValueExW(k, L"URI", 0, REG_SZ, (const BYTE *)t->Path, bytes);
        RegSetValueExW(k, L"Schema", 0, REG_DWORD, (const BYTE *)&schema, sizeof(schema));
        const TX_NODE *info = TxChild(task, L"RegistrationInfo");
        static const WCHAR *const names[] = {L"Author", L"Description", L"Date", L"Source", L"Version", L"Documentation"};
        for (ULONG i = 0; i < RTL_NUMBER_OF(names); i++) {
            const WCHAR *value = TxText(info, names[i]);
            if (value)
                RegSetValueExW(k, names[i], 0, REG_SZ, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
            else
                RegDeleteValueW(k, names[i]);
        }
        RegCloseKey(k);
    }
    static const WCHAR *const kinds[] = {L"Boot", L"Logon", L"Plain"};
    for (ULONG i = 0; i < RTL_NUMBER_OF(kinds); i++) {
        _snwprintf(key, RTL_NUMBER_OF(key), TASK_CACHE L"\\%ls\\%ls", kinds[i], t->Id);
        if (!wcscmp(kinds[i], kind)) {
            if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr))
                RegCloseKey(k);
        } else {
            RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);
        }
    }
    write_dynamic(t);
}

static void delete_cache(TASK *t)
{
    WCHAR *tree = key_of(L"Tree", t->Path);
    if (tree)
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, tree);
    SvcFree(tree);
    if (!t->Id[0])
        return;
    static const WCHAR *const kinds[] = {L"Tasks", L"Boot", L"Logon", L"Plain"};
    for (ULONG i = 0; i < RTL_NUMBER_OF(kinds); i++) {
        WCHAR key[200];
        _snwprintf(key, RTL_NUMBER_OF(key), TASK_CACHE L"\\%ls\\%ls", kinds[i], t->Id);
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);
    }
}

/* ------------------------------------------------------------------ what a stored task says */

typedef struct STORED {
    TX_NODE *Xml;
    WCHAR *UserId;    /* a sid, of the principal's user, null for a group. */
    BOOL Enabled, Hidden, Highest;
} STORED;

static void stored_free(STORED *s)
{
    TxFree(s->Xml);
    SvcFree(s->UserId);
    memset(s, 0, sizeof(*s));
}

static HRESULT stored_load(const WCHAR *path, STORED *s)
{
    memset(s, 0, sizeof(*s));
    WCHAR *text = read_task_file(path);
    if (!text)
        return HR_WIN32(ERROR_FILE_NOT_FOUND);
    TX_ERROR info;
    HRESULT hr = TxRead(text, &s->Xml, &info);
    TxFreeError(&info);
    SvcFree(text);
    if (FAILED(hr))
        return SCHED_E_MALFORMEDXML_;
    const TX_NODE *principal = TxChild(TxChild(s->Xml, L"Principals"), L"Principal");
    const WCHAR *user = TxText(principal, L"UserId");
    s->UserId = user ? SvcDup(user) : nullptr;
    const WCHAR *level = TxText(principal, L"RunLevel");
    s->Highest = level && !wcscmp(level, L"HighestAvailable");
    s->Enabled = TxSettingsFlag(s->Xml, L"Enabled", TRUE);
    s->Hidden = TxSettingsFlag(s->Xml, L"Hidden", FALSE);
    return S_OK;
}

static BOOL may_change(const CALLER *c, const STORED *s)
{
    return c->Elevated || (s->UserId && !_wcsicmp(s->UserId, c->Sid));
}

#define TASK_CRED_DEFAULT 0x1
#define TASK_LOGON_SERVICE_ACCOUNT_ 5

/* the system, local service and network service accounts, which windows keeps with no logon type. */
static BOOL is_service_sid(const WCHAR *sid)
{
    return !_wcsicmp(sid, L"S-1-5-18") || !_wcsicmp(sid, L"S-1-5-19") || !_wcsicmp(sid, L"S-1-5-20");
}

/* the sid of a name or a sid as text, null when nobody has it. */
static WCHAR *sid_of(const WCHAR *name)
{
    PSID sid = nullptr;
    if (ConvertStringSidToSidW(name, &sid)) {
        LocalFree(sid);
        return SvcDup(name);
    }
    BYTE buffer[SECURITY_MAX_SID_SIZE_];
    WCHAR domain[256];
    DWORD size = sizeof(buffer), chars = RTL_NUMBER_OF(domain), use;
    WCHAR *text = nullptr;
    if (!LookupAccountNameW(nullptr, name, buffer, &size, domain, &chars, &use) || !ConvertSidToStringSidW(buffer, &text))
        return nullptr;
    WCHAR *out = SvcDup(text);
    LocalFree(text);
    return out;
}

static void hand_error(const TX_ERROR *info, TASK_XML_ERROR_INFO **out)
{
    TASK_XML_ERROR_INFO *e = MIDL_user_allocate(sizeof(*e));
    if (!e)
        return;
    e->line = info->Line;
    e->column = info->Column;
    e->node = (unsigned short *)rpc_string(info->Node ? info->Node : L"");
    e->value = (unsigned short *)rpc_string(info->Value ? info->Value : L"");
    *out = e;
}

static void wake(void)
{
    if (g_wake)
        SetEvent(g_wake);
}

static HRESULT run_task(TASK *t, const STORED *s, const CALLER *c, DWORD count, const WCHAR *const *args, GUID *guid,
                        DWORD delay_ms);

/* ------------------------------------------------------------------ procedures */

long WINAPI tsch_SchRpcHighestVersion_s(handle_t h, unsigned long *pVersion)
{
    *pVersion = TASK_VERSION;
    return S_OK;
}

static const WCHAR *logon_name(DWORD type)
{
    switch (type) {
    case TASK_LOGON_PASSWORD:
        return L"Password";
    case TASK_LOGON_S4U:
        return L"S4U";
    case TASK_LOGON_GROUP:
        return L"Group";
    case TASK_LOGON_SERVICE_ACCOUNT:
        return L"ServiceAccount";
    case TASK_LOGON_INTERACTIVE_TOKEN_OR_PASSWORD:
        return L"InteractiveTokenOrPassword";
    default:
        return L"InteractiveToken";
    }
}

long WINAPI tsch_SchRpcRegisterTask_s(handle_t h, unsigned short *path, unsigned short *xml, unsigned long flags,
                                      unsigned short *sddl, unsigned long logonType, unsigned long cCreds,
                                      TASK_USER_CRED *pCreds, unsigned short **pActualPath,
                                      TASK_XML_ERROR_INFO **pErrorInfo)
{
    *pActualPath = nullptr;
    *pErrorInfo = nullptr;
    CALLER c;
    if (!caller_of(h, &c))
        return E_ACCESSDENIED;
    TX_NODE *task = nullptr;
    TX_ERROR info;
    HRESULT hr = TxRead((const WCHAR *)xml, &task, &info);
    if (FAILED(hr)) {
        hand_error(&info, pErrorInfo);
        TxFreeError(&info);
        caller_free(&c);
        return hr;
    }
    TxFreeError(&info);
    WCHAR *full = nullptr;
    if (path) {
        full = normal_path(path);
    } else {
        GUID g;
        WCHAR name[48];
        UuidCreate((UUID *)&g);
        format_guid(&g, name, RTL_NUMBER_OF(name));
        full = normal_path((unsigned short *)name);
    }
    if (!full || !full[1] || !valid_name(full)) {
        TxFree(task);
        SvcFree(full);
        caller_free(&c);
        return full ? HR_WIN32(ERROR_INVALID_NAME) : E_OUTOFMEMORY;
    }
    /*
     * the principal: credentials given, else the one of the xml, else the caller. taskschd sends the caller as
     * default credentials when none were given, and those give way to the xml.
     */
    const TX_NODE *principal = TxChild(TxChild(task, L"Principals"), L"Principal");
    const WCHAR *group = TxText(principal, L"GroupId");
    BOOL given = cCreds && pCreds && pCreds[0].userId && pCreds[0].userId[0] &&
                 (!(pCreds[0].flags & TASK_CRED_DEFAULT) || (!group && !TxText(principal, L"UserId")));
    const WCHAR *user = given ? (const WCHAR *)pCreds[0].userId : TxText(principal, L"UserId");
    if (given)
        group = nullptr;
    const WCHAR *level = TxText(principal, L"RunLevel");
    const WCHAR *logon = TxText(principal, L"LogonType");
    WCHAR *user_sid = group ? nullptr : user ? sid_of(user) : SvcDup(c.Sid);
    BOOL service = user_sid && is_service_sid(user_sid);
    EnterCriticalSection(&g_tsch_lock);
    if (!group && !user_sid) {
        hr = HR_WIN32(ERROR_NONE_MAPPED);
    } else if (logonType == TASK_LOGON_SERVICE_ACCOUNT_ && !service) {
        /* measured: a service logon for an account that is no service is a wrong value of the task. */
        TX_ERROR wrong = {task->EndLine, task->EndColumn, (WCHAR *)L"Task", (WCHAR *)L""};
        hand_error(&wrong, pErrorInfo);
        hr = SCHED_E_INVALIDVALUE_;
    }
    /*
     * measured: a caller that is not elevated registers tasks that run as itself at the least privilege only, and an
     * administrator's filtered token without a run level asks for the highest one, which it does not have.
     */
    else if (SUCCEEDED(hr) && !c.Elevated && ((level && !wcscmp(level, L"HighestAvailable")) || (!level && c.Limited) ||
                             group || _wcsicmp(user_sid, c.Sid)))
        hr = E_ACCESSDENIED;
    BOOL exists = is_task(full);
    STORED old = {};
    if (SUCCEEDED(hr) && exists && SUCCEEDED(stored_load(full, &old)) && !may_change(&c, &old))
        hr = E_ACCESSDENIED;
    if (SUCCEEDED(hr) && exists && !(flags & TASK_UPDATE) && (flags & TASK_CREATE))
        hr = HR_WIN32(ERROR_ALREADY_EXISTS);
    else if (SUCCEEDED(hr) && !exists && (flags & TASK_UPDATE) && !(flags & TASK_CREATE))
        hr = HR_WIN32(ERROR_FILE_NOT_FOUND);
    else if (SUCCEEDED(hr) && is_folder(full))
        hr = HR_WIN32(ERROR_ALREADY_EXISTS);
    stored_free(&old);
    WCHAR *text = nullptr;
    BOOL enabled = TxSettingsFlag(task, L"Enabled", TRUE) && !(flags & TASK_DISABLE);
    if (SUCCEEDED(hr)) {
        TX_PRINCIPAL who = {TxAttr(principal, L"id") ? TxAttr(principal, L"id") : L"Author",
                            user_sid,
                            group,
                            TxText(principal, L"DisplayName"),
                            logon ? logon : service ? nullptr : logon_name(logonType),
                            level && !wcscmp(level, L"HighestAvailable")};
        hr = TxWrite(task, &who, full, enabled, &text);
    }
    if (SUCCEEDED(hr) && !(flags & TASK_VALIDATE_ONLY)) {
        WCHAR *parent = parent_of(full);
        hr = parent ? make_folders(parent, TRUE) : E_OUTOFMEMORY;
        SvcFree(parent);
        if (SUCCEEDED(hr))
            hr = write_task_file(full, text);
        if (SUCCEEDED(hr)) {
            TASK *t = task_load(full);
            if (t) {
                if (!exists || !t->Created)
                    t->Created = now();
                write_cache(t, task, user_sid);
                if (!(flags & TASK_IGNORE_REGISTRATION_TRIGGERS) && enabled && TxHasTrigger(task, L"RegistrationTrigger")) {
                    STORED s;
                    if (SUCCEEDED(stored_load(full, &s))) {
                        GUID g;
                        run_task(t, &s, nullptr, 0, nullptr, &g, (DWORD)(TxTriggerDelay(task, L"RegistrationTrigger") * 1000));
                        stored_free(&s);
                    }
                }
            }
        }
    }
    LeaveCriticalSection(&g_tsch_lock);
    if (SUCCEEDED(hr)) {
        *pActualPath = (unsigned short *)rpc_string(full);
        wake();
    }
    SvcFree(text);
    SvcFree(user_sid);
    SvcFree(full);
    TxFree(task);
    caller_free(&c);
    return hr;
}

long WINAPI tsch_SchRpcRetrieveTask_s(handle_t h, unsigned short *path, unsigned short *lpcwszLanguagesBuffer,
                                      unsigned long *pulNumLanguages, unsigned short **pXml)
{
    *pXml = nullptr;
    WCHAR *full = normal_path(path);
    if (!full)
        return E_OUTOFMEMORY;
    EnterCriticalSection(&g_tsch_lock);
    WCHAR *text = is_task(full) ? read_task_file(full) : nullptr;
    LeaveCriticalSection(&g_tsch_lock);
    SvcFree(full);
    if (!text)
        return HR_WIN32(ERROR_FILE_NOT_FOUND);
    *pXml = (unsigned short *)rpc_string(text);
    SvcFree(text);
    return *pXml ? S_OK : E_OUTOFMEMORY;
}

long WINAPI tsch_SchRpcCreateFolder_s(handle_t h, unsigned short *path, unsigned short *sddl, unsigned long flags)
{
    WCHAR *full = normal_path(path);
    if (!full)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    EnterCriticalSection(&g_tsch_lock);
    if (!full[1] || !valid_name(full))
        hr = full[1] ? HR_WIN32(ERROR_INVALID_NAME) : HR_WIN32(ERROR_ALREADY_EXISTS);
    else if (is_folder(full) || is_task(full))
        hr = HR_WIN32(ERROR_ALREADY_EXISTS);
    else
        hr = make_folders(full, TRUE);
    LeaveCriticalSection(&g_tsch_lock);
    SvcFree(full);
    return hr;
}

long WINAPI tsch_SchRpcSetSecurity_s(handle_t h, unsigned short *path, unsigned short *sddl, unsigned long flags)
{
    WCHAR *full = normal_path(path);
    CALLER c;
    if (!full || !caller_of(h, &c)) {
        SvcFree(full);
        return E_ACCESSDENIED;
    }
    HRESULT hr = S_OK;
    PSECURITY_DESCRIPTOR sd = nullptr;
    ULONG length = 0;
    EnterCriticalSection(&g_tsch_lock);
    if (!is_folder(full) && !is_task(full))
        hr = HR_WIN32(ERROR_FILE_NOT_FOUND);
    else if (!c.Elevated)
        hr = E_ACCESSDENIED;
    else if (!ConvertStringSecurityDescriptorToSecurityDescriptorW((const WCHAR *)sddl, SDDL_REVISION_1_, &sd, &length))
        hr = HR_WIN32(GetLastError());
    if (SUCCEEDED(hr)) {
        WCHAR *tree = key_of(L"Tree", full);
        HKEY k;
        if (tree && !RegCreateKeyExW(HKEY_LOCAL_MACHINE, tree, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr)) {
            RegSetValueExW(k, L"SD", 0, REG_BINARY, sd, length);
            RegCloseKey(k);
        }
        SvcFree(tree);
    }
    LeaveCriticalSection(&g_tsch_lock);
    if (sd)
        LocalFree(sd);
    SvcFree(full);
    caller_free(&c);
    return hr;
}

/* the descriptor of a folder or a task: the one of its key, else the one of the root of windows. */
long WINAPI tsch_SchRpcGetSecurity_s(handle_t h, unsigned short *path, unsigned long securityInformation,
                                     unsigned short **sddl)
{
    *sddl = nullptr;
    WCHAR *full = normal_path(path);
    if (!full)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    BYTE *sd = nullptr;
    EnterCriticalSection(&g_tsch_lock);
    if (!is_folder(full) && !is_task(full)) {
        hr = HR_WIN32(ERROR_FILE_NOT_FOUND);
    } else {
        WCHAR *tree = key_of(L"Tree", full);
        HKEY k;
        DWORD size = 0, type;
        if (tree && !RegOpenKeyExW(HKEY_LOCAL_MACHINE, tree, 0, KEY_READ, &k)) {
            if (!RegQueryValueExW(k, L"SD", nullptr, &type, nullptr, &size) && size && (sd = SvcAlloc(size)) &&
                RegQueryValueExW(k, L"SD", nullptr, &type, sd, &size)) {
                SvcFree(sd);
                sd = nullptr;
            }
            RegCloseKey(k);
        }
        SvcFree(tree);
    }
    LeaveCriticalSection(&g_tsch_lock);
    SvcFree(full);
    if (FAILED(hr))
        return hr;
    PSECURITY_DESCRIPTOR fallback = nullptr;
    if (!sd && ConvertStringSecurityDescriptorToSecurityDescriptorW(
                   L"O:SYG:SYD:PAI(A;CI;FA;;;BA)(A;OI;0x1f019f;;;BA)(A;CI;FA;;;SY)(A;OI;0x1f019f;;;SY)(A;CI;FW;;;AU)"
                   L"(A;CI;FW;;;NS)(A;CI;FW;;;LS)(A;OICIIO;FA;;;CO)",
                   SDDL_REVISION_1_, &fallback, nullptr))
        sd = fallback;
    WCHAR *text = nullptr;
    if (!sd || !ConvertSecurityDescriptorToStringSecurityDescriptorW(sd, SDDL_REVISION_1_, securityInformation, &text, nullptr))
        hr = HR_WIN32(GetLastError());
    if (fallback)
        LocalFree(fallback);
    else
        SvcFree(sd);
    if (text) {
        *sddl = (unsigned short *)rpc_string(text);
        LocalFree(text);
    }
    return hr;
}

/* the names of the folders or the tasks of a folder, from the start the caller holds, as many as it asks. */
static long enumerate(unsigned short *path, BOOL folders, unsigned long flags, unsigned long *start, unsigned long requested,
                      unsigned long *count, unsigned short ***names)
{
    *count = 0;
    *names = nullptr;
    WCHAR *full = normal_path(path);
    if (!full)
        return E_OUTOFMEMORY;
    EnterCriticalSection(&g_tsch_lock);
    if (!is_folder(full)) {
        LeaveCriticalSection(&g_tsch_lock);
        SvcFree(full);
        return HR_WIN32(ERROR_FILE_NOT_FOUND);
    }
    WCHAR *dir = file_of(full);
    SIZE_T chars = wcslen(dir) + 3;
    WCHAR *pattern = SvcAlloc(chars * sizeof(WCHAR));
    _snwprintf(pattern, chars, L"%ls\\*", dir);
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern, &data);
    ULONG room = requested ? requested : 0x7FFFFFFF;
    ULONG index = 0, made = 0;
    unsigned short **list = nullptr;
    ULONG capacity = 0;
    while (find != INVALID_HANDLE_VALUE) {
        BOOL dot = data.cFileName[0] == L'.' && (!data.cFileName[1] || (data.cFileName[1] == L'.' && !data.cFileName[2]));
        BOOL folder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        BOOL take = !dot && folder == folders;
        if (take && !folders && !(flags & TASK_ENUM_HIDDEN)) {
            SIZE_T n = wcslen(full) + wcslen(data.cFileName) + 2;
            WCHAR *child = SvcAlloc(n * sizeof(WCHAR));
            _snwprintf(child, n, L"%ls\\%ls", full[1] ? full : L"", data.cFileName);
            STORED s;
            if (SUCCEEDED(stored_load(child, &s))) {
                take = !s.Hidden;
                stored_free(&s);
            }
            SvcFree(child);
        }
        if (take) {
            if (index++ >= *start && made < room) {
                if (made == capacity) {
                    capacity = capacity ? capacity * 2 : 16;
                    unsigned short **bigger = MIDL_user_allocate(capacity * sizeof(*bigger));
                    if (list)
                        memcpy(bigger, list, made * sizeof(*bigger));
                    MIDL_user_free(list);
                    list = bigger;
                }
                list[made++] = (unsigned short *)rpc_string(data.cFileName);
            }
        }
        if (!FindNextFileW(find, &data)) {
            FindClose(find);
            break;
        }
    }
    LeaveCriticalSection(&g_tsch_lock);
    SvcFree(pattern);
    SvcFree(dir);
    SvcFree(full);
    *start += made;
    *count = made;
    *names = list;
    return requested && made == requested ? S_OK : S_FALSE;
}

long WINAPI tsch_SchRpcEnumFolders_s(handle_t h, unsigned short *path, unsigned long flags, unsigned long *pStartIndex,
                                     unsigned long cRequested, unsigned long *pcNames, unsigned short ***pNames)
{
    return enumerate(path, TRUE, flags, pStartIndex, cRequested, pcNames, pNames);
}

long WINAPI tsch_SchRpcEnumTasks_s(handle_t h, unsigned short *path, unsigned long flags, unsigned long *pStartIndex,
                                   unsigned long cRequested, unsigned long *pcNames, unsigned short ***pNames)
{
    return enumerate(path, FALSE, flags, pStartIndex, cRequested, pcNames, pNames);
}

static BOOL under(const WCHAR *path, const WCHAR *folder)
{
    SIZE_T n = wcslen(folder);
    return !folder[1] || (!_wcsnicmp(path, folder, n) && (path[n] == L'\\' || !path[n]));
}

long WINAPI tsch_SchRpcEnumInstances_s(handle_t h, unsigned short *path, unsigned long flags, unsigned long *pcGuids,
                                       TSCH_GUID **pGuids)
{
    *pcGuids = 0;
    *pGuids = nullptr;
    WCHAR *full = path ? normal_path(path) : SvcDup(L"\\");
    EnterCriticalSection(&g_tsch_lock);
    ULONG n = 0;
    for (INSTANCE *i = g_instances; i; i = i->Next)
        n += i->Task && full && under(i->Task->Path, full);
    TSCH_GUID *list = n ? MIDL_user_allocate(n * sizeof(*list)) : nullptr;
    ULONG made = 0;
    for (INSTANCE *i = g_instances; i && list; i = i->Next)
        if (i->Task && under(i->Task->Path, full))
            memcpy(&list[made++], &i->Guid, sizeof(GUID));
    LeaveCriticalSection(&g_tsch_lock);
    SvcFree(full);
    *pcGuids = made;
    *pGuids = list;
    return S_OK;
}

static INSTANCE *instance_find(const TSCH_GUID *g)
{
    for (INSTANCE *i = g_instances; i; i = i->Next)
        if (!memcmp(&i->Guid, g, sizeof(GUID)))
            return i;
    return nullptr;
}

long WINAPI tsch_SchRpcGetInstanceInfo_s(handle_t h, TSCH_GUID guid, unsigned short **pPath, unsigned long *pState,
                                         unsigned short **pCurrentAction, unsigned short **pInfo,
                                         unsigned long *pcGroupInstances, TSCH_GUID **pGroupInstances,
                                         unsigned long *pEnginePID)
{
    *pPath = *pCurrentAction = *pInfo = nullptr;
    *pState = 0;
    *pcGroupInstances = 0;
    *pGroupInstances = nullptr;
    *pEnginePID = 0;
    EnterCriticalSection(&g_tsch_lock);
    INSTANCE *i = instance_find(&guid);
    HRESULT hr = i && i->Task ? S_OK : HR_WIN32(ERROR_FILE_NOT_FOUND);
    if (SUCCEEDED(hr)) {
        *pPath = (unsigned short *)rpc_string(i->Task->Path);
        *pState = TASK_STATE_RUNNING;
        *pCurrentAction = (unsigned short *)rpc_string(L"");
        *pInfo = (unsigned short *)rpc_string(L"");
        *pEnginePID = i->Pid;
    }
    LeaveCriticalSection(&g_tsch_lock);
    return hr;
}

static void stop_instance(INSTANCE *i)
{
    i->Stopped = TRUE;
    if (i->Process)
        TerminateProcess(i->Process, 0x41306 /* SCHED_S_TASK_TERMINATED */);
}

long WINAPI tsch_SchRpcStopInstance_s(handle_t h, TSCH_GUID guid, unsigned long flags)
{
    EnterCriticalSection(&g_tsch_lock);
    INSTANCE *i = instance_find(&guid);
    if (i)
        stop_instance(i);
    LeaveCriticalSection(&g_tsch_lock);
    return i ? S_OK : HR_WIN32(ERROR_FILE_NOT_FOUND);
}

long WINAPI tsch_SchRpcStop_s(handle_t h, unsigned short *path, unsigned long flags)
{
    WCHAR *full = path ? normal_path(path) : SvcDup(L"\\");
    EnterCriticalSection(&g_tsch_lock);
    for (INSTANCE *i = g_instances; i && full; i = i->Next)
        if (i->Task && under(i->Task->Path, full))
            stop_instance(i);
    LeaveCriticalSection(&g_tsch_lock);
    SvcFree(full);
    return S_OK;
}

long WINAPI tsch_SchRpcRun_s(handle_t h, unsigned short *path, unsigned long cArgs, unsigned short **pArgs,
                             unsigned long flags, unsigned long sessionId, unsigned short *user, TSCH_GUID *pGuid)
{
    memset(pGuid, 0, sizeof(*pGuid));
    WCHAR *full = normal_path(path);
    CALLER c;
    if (!full || !caller_of(h, &c)) {
        SvcFree(full);
        return E_ACCESSDENIED;
    }
    STORED s;
    EnterCriticalSection(&g_tsch_lock);
    HRESULT hr = is_task(full) ? stored_load(full, &s) : HR_WIN32(ERROR_FILE_NOT_FOUND);
    if (SUCCEEDED(hr)) {
        if (!may_change(&c, &s))
            hr = E_ACCESSDENIED;
        else if (!(flags & TASK_RUN_IGNORE_CONSTRAINTS) && !s.Enabled)
            hr = SCHED_E_TASK_DISABLED_;
        else if (!(flags & TASK_RUN_IGNORE_CONSTRAINTS) && !TxSettingsFlag(s.Xml, L"AllowStartOnDemand", TRUE))
            hr = SCHED_E_START_ON_DEMAND_;
        TASK *t = SUCCEEDED(hr) ? task_load(full) : nullptr;
        if (SUCCEEDED(hr))
            hr = t ? run_task(t, &s, &c, cArgs, (const WCHAR *const *)pArgs, (GUID *)pGuid, 0) : E_OUTOFMEMORY;
        stored_free(&s);
    }
    LeaveCriticalSection(&g_tsch_lock);
    SvcFree(full);
    caller_free(&c);
    return hr;
}

/* a folder must be empty to go, as the root never does. */
long WINAPI tsch_SchRpcDelete_s(handle_t h, unsigned short *path, unsigned long flags)
{
    WCHAR *full = normal_path(path);
    CALLER c;
    if (!full || !caller_of(h, &c)) {
        SvcFree(full);
        return E_ACCESSDENIED;
    }
    HRESULT hr = S_OK;
    EnterCriticalSection(&g_tsch_lock);
    WCHAR *file = file_of(full);
    if (!full[1]) {
        hr = E_ACCESSDENIED;
    } else if (is_task(full)) {
        STORED s;
        if (SUCCEEDED(stored_load(full, &s)) && !may_change(&c, &s))
            hr = E_ACCESSDENIED;
        stored_free(&s);
        if (SUCCEEDED(hr) && !DeleteFileW(file))
            hr = HR_WIN32(GetLastError());
        if (SUCCEEDED(hr)) {
            TASK *t = task_load(full);
            if (t) {
                delete_cache(t);
                task_forget(t);
            }
        }
    } else if (is_folder(full)) {
        if (!RemoveDirectoryW(file))
            hr = HR_WIN32(GetLastError());
        if (SUCCEEDED(hr)) {
            WCHAR *tree = key_of(L"Tree", full);
            if (tree)
                RegDeleteKeyW(HKEY_LOCAL_MACHINE, tree);
            SvcFree(tree);
        }
    } else {
        hr = HR_WIN32(ERROR_FILE_NOT_FOUND);
    }
    LeaveCriticalSection(&g_tsch_lock);
    SvcFree(file);
    SvcFree(full);
    caller_free(&c);
    wake();
    return hr;
}

/* MS-TSCH: the server does not rename. */
long WINAPI tsch_SchRpcRename_s(handle_t h, unsigned short *path, unsigned short *newName, unsigned long flags)
{
    return E_NOTIMPL;
}

static BOOL to_local(ULONGLONG utc, TSCH_SYSTEMTIME *out)
{
    SYSTEMTIME local;
    if (!TxUtcToLocal(utc, &local))
        return FALSE;
    memcpy(out, &local, sizeof(*out));
    return TRUE;
}

static ULONGLONG from_local(const TSCH_SYSTEMTIME *in)
{
    SYSTEMTIME local;
    ULONGLONG utc = 0;
    memcpy(&local, in, sizeof(local));
    local.wDayOfWeek = 0;
    return TxLocalToUtc(&local, &utc) ? utc : 0;
}

long WINAPI tsch_SchRpcScheduledRuntimes_s(handle_t h, unsigned short *path, TSCH_SYSTEMTIME *start,
                                           TSCH_SYSTEMTIME *end, unsigned long flags, unsigned long cRequested,
                                           unsigned long *pcRuntimes, TSCH_SYSTEMTIME **pRuntimes)
{
    *pcRuntimes = 0;
    *pRuntimes = nullptr;
    WCHAR *full = normal_path(path);
    if (!full)
        return E_OUTOFMEMORY;
    STORED s;
    EnterCriticalSection(&g_tsch_lock);
    HRESULT hr = is_task(full) ? stored_load(full, &s) : HR_WIN32(ERROR_FILE_NOT_FOUND);
    LeaveCriticalSection(&g_tsch_lock);
    SvcFree(full);
    if (FAILED(hr))
        return hr;
    ULONG room = cRequested ? (cRequested < 1440 ? cRequested : 1440) : 1440;
    ULONGLONG *times = SvcAlloc(room * sizeof(ULONGLONG));
    ULONGLONG from = start ? from_local(start) : now(), to = end ? from_local(end) : ~0ull >> 2;
    ULONG n = times ? TxRunTimes(s.Xml, from, to, times, room) : 0;
    /* measured: no run in the window of a task that has a time to run at is no run, a task without one is not scheduled. */
    BOOL timed = TxHasTrigger(s.Xml, L"TimeTrigger") || TxHasTrigger(s.Xml, L"CalendarTrigger");
    stored_free(&s);
    if (!n) {
        SvcFree(times);
        return timed ? S_OK : SCHED_S_TASK_NOT_SCHEDULED_;
    }
    TSCH_SYSTEMTIME *list = MIDL_user_allocate(n * sizeof(*list));
    for (ULONG i = 0; list && i < n; i++)
        to_local(times[i], &list[i]);
    SvcFree(times);
    *pcRuntimes = list ? n : 0;
    *pRuntimes = list;
    return cRequested && n >= cRequested ? S_FALSE : S_OK;
}

long WINAPI tsch_SchRpcGetLastRunInfo_s(handle_t h, unsigned short *path, TSCH_SYSTEMTIME *pLastRuntime,
                                        unsigned long *pLastReturnCode)
{
    memset(pLastRuntime, 0, sizeof(*pLastRuntime));
    *pLastReturnCode = 0;
    WCHAR *full = normal_path(path);
    if (!full)
        return E_OUTOFMEMORY;
    EnterCriticalSection(&g_tsch_lock);
    HRESULT hr = S_OK;
    TASK *t = is_task(full) ? task_load(full) : nullptr;
    if (!t)
        hr = HR_WIN32(ERROR_FILE_NOT_FOUND);
    else if (!t->HasRun) {
        hr = SCHED_S_TASK_HAS_NOT_RUN_;
        *pLastReturnCode = (unsigned long)SCHED_S_TASK_HAS_NOT_RUN_;
    } else {
        to_local(t->LastRun, pLastRuntime);
        *pLastReturnCode = t->Running ? (unsigned long)SCHED_S_TASK_RUNNING_ : t->LastResult;
    }
    LeaveCriticalSection(&g_tsch_lock);
    SvcFree(full);
    return hr;
}

long WINAPI tsch_SchRpcGetTaskInfo_s(handle_t h, unsigned short *path, unsigned long flags, unsigned long *pEnabled,
                                     unsigned long *pState)
{
    *pEnabled = 0;
    *pState = 0;
    WCHAR *full = normal_path(path);
    if (!full)
        return E_OUTOFMEMORY;
    STORED s;
    EnterCriticalSection(&g_tsch_lock);
    HRESULT hr = is_task(full) ? stored_load(full, &s) : HR_WIN32(ERROR_FILE_NOT_FOUND);
    if (SUCCEEDED(hr)) {
        TASK *t = task_load(full);
        *pEnabled = s.Enabled;
        *pState = t && t->Running ? TASK_STATE_RUNNING : s.Enabled ? TASK_STATE_READY : TASK_STATE_DISABLED;
        stored_free(&s);
    }
    LeaveCriticalSection(&g_tsch_lock);
    SvcFree(full);
    return hr;
}

long WINAPI tsch_SchRpcGetNumberOfMissedRuns_s(handle_t h, unsigned short *path, unsigned long *pNumberOfMissedRuns)
{
    *pNumberOfMissedRuns = 0;
    WCHAR *full = normal_path(path);
    if (!full)
        return E_OUTOFMEMORY;
    EnterCriticalSection(&g_tsch_lock);
    TASK *t = is_task(full) ? task_load(full) : nullptr;
    HRESULT hr = !t ? HR_WIN32(ERROR_FILE_NOT_FOUND) : t->HasRun ? S_OK : SCHED_S_TASK_HAS_NOT_RUN_;
    LeaveCriticalSection(&g_tsch_lock);
    SvcFree(full);
    return hr;
}

long WINAPI tsch_SchRpcEnableTask_s(handle_t h, unsigned short *path, unsigned long enabled)
{
    WCHAR *full = normal_path(path);
    CALLER c;
    if (!full || !caller_of(h, &c)) {
        SvcFree(full);
        return E_ACCESSDENIED;
    }
    STORED s;
    EnterCriticalSection(&g_tsch_lock);
    HRESULT hr = is_task(full) ? stored_load(full, &s) : HR_WIN32(ERROR_FILE_NOT_FOUND);
    if (SUCCEEDED(hr)) {
        if (!may_change(&c, &s)) {
            hr = E_ACCESSDENIED;
        } else {
            const TX_NODE *principal = TxChild(TxChild(s.Xml, L"Principals"), L"Principal");
            TX_PRINCIPAL who = {TxAttr(principal, L"id") ? TxAttr(principal, L"id") : L"Author",
                                TxText(principal, L"UserId"),
                                TxText(principal, L"GroupId"),
                                TxText(principal, L"DisplayName"),
                                TxText(principal, L"LogonType"),
                                s.Highest};
            WCHAR *text = nullptr;
            hr = TxWrite(s.Xml, &who, full, enabled != 0, &text);
            if (SUCCEEDED(hr))
                hr = write_task_file(full, text);
            SvcFree(text);
        }
        stored_free(&s);
    }
    LeaveCriticalSection(&g_tsch_lock);
    SvcFree(full);
    caller_free(&c);
    wake();
    return hr;
}

/* ------------------------------------------------------------------ runs */

typedef struct RUN_ARGS {
    INSTANCE *Instance;
    HANDLE Token;       /* null for the system's own. */
    WCHAR **Commands;   /* the command lines and the folders of the actions, in pairs. */
    ULONG Count;
    DWORD Delay;
} RUN_ARGS;

/* a token of the user of the principal in the session of the console: the caller's when it is that user. */
/* the user's half of a token pair the run level asks for: the elevated one for highest, else the filtered one. */
static HANDLE level_of(HANDLE logon, BOOL highest)
{
    DWORD type = 0, size;
    HANDLE linked = nullptr, token = nullptr;
    if (GetTokenInformation(logon, TokenElevationType_, &type, sizeof(type), &size) &&
        type == (highest ? TOKEN_ELEVATION_LIMITED : TOKEN_ELEVATION_FULL) &&
        GetTokenInformation(logon, TokenLinkedToken_, &linked, sizeof(linked), &size)) {
        DuplicateTokenEx(linked, TOKEN_ALL_ACCESS, nullptr, 2, TokenPrimary, &token);
        CloseHandle(linked);
    } else {
        DuplicateTokenEx(logon, TOKEN_ALL_ACCESS, nullptr, 2, TokenPrimary, &token);
    }
    return token;
}

static BOOL token_is(HANDLE token, const WCHAR *sid)
{
    BYTE user[128];
    DWORD size;
    WCHAR *text = nullptr;
    BOOL same = GetTokenInformation(token, TokenUser_, user, sizeof(user), &size) &&
                ConvertSidToStringSidW(*(PSID *)user, &text) && !_wcsicmp(text, sid);
    LocalFree(text);
    return same;
}

/*
 * an interactive task runs in the logon session of its user, which must be logged on. the other logon types need a
 * logon of their own.
 */
static HANDLE user_token(const STORED *s, const CALLER *c)
{
    if (!s->UserId || !_wcsicmp(s->UserId, L"S-1-5-18"))
        return nullptr;
    const WCHAR *type = TxText(TxChild(TxChild(s->Xml, L"Principals"), L"Principal"), L"LogonType");
    if (c && !_wcsicmp(s->UserId, c->Sid) && (!type || wcsncmp(type, L"Interactive", 11))) {
        HANDLE token = nullptr;
        DuplicateTokenEx(c->Token, TOKEN_ALL_ACCESS, nullptr, 2, TokenPrimary, &token);
        return token;
    }
    if (!type || !wcsncmp(type, L"Interactive", 11)) {
        static BOOL(WINAPI * query)(ULONG, PHANDLE);
        if (!query) {
            HMODULE wts = LoadLibraryW(L"wtsapi32.dll");
            query = wts ? (BOOL(WINAPI *)(ULONG, PHANDLE))GetProcAddress(wts, "WTSQueryUserToken") : nullptr;
        }
        HANDLE logon = nullptr, token = nullptr;
        if (query && query(WTSGetActiveConsoleSessionId(), &logon) && token_is(logon, s->UserId))
            token = level_of(logon, s->Highest);
        if (logon)
            CloseHandle(logon);
        if (token || (type && wcscmp(type, L"InteractiveTokenOrPassword")))
            return token ? token : INVALID_HANDLE_VALUE;
    }
    PSID sid = nullptr;
    WCHAR name[256], domain[256];
    DWORD chars = RTL_NUMBER_OF(name), dchars = RTL_NUMBER_OF(domain), use;
    if (!ConvertStringSidToSidW(s->UserId, &sid))
        return INVALID_HANDLE_VALUE;
    BOOL found = LookupAccountSidW(nullptr, sid, name, &chars, domain, &dchars, &use);
    LocalFree(sid);
    HANDLE logon = nullptr;
    if (!found || !LogonUserW(name, L".", L"", 4 /* LOGON32_LOGON_BATCH */, 0, &logon))
        return INVALID_HANDLE_VALUE;
    HANDLE token = level_of(logon, s->Highest);
    CloseHandle(logon);
    DWORD session = WTSGetActiveConsoleSessionId();
    if (token && session != 0xFFFFFFFF)
        SetTokenInformation(token, TokenSessionId_, &session, sizeof(session));
    return token ? token : INVALID_HANDLE_VALUE;
}

/* the variables of an environment block in a text, as the scheduler expands the parts of an action. */
static WCHAR *expand(const WCHAR *text, const WCHAR *block)
{
    TX_BUF b = {};
    for (const WCHAR *p = text; p && *p;) {
        const WCHAR *close = p[0] == L'%' ? wcschr(p + 1, L'%') : nullptr;
        const WCHAR *value = nullptr;
        SIZE_T name_chars = close ? (SIZE_T)(close - p - 1) : 0;
        for (const WCHAR *e = block; close && name_chars && e && *e; e += wcslen(e) + 1)
            if (!_wcsnicmp(e, p + 1, name_chars) && e[name_chars] == L'=')
                value = e + name_chars + 1;
        if (value) {
            TxBufPut(&b, value);
            p = close + 1;
            continue;
        }
        WCHAR one[2] = {*p++, 0};
        TxBufPut(&b, one);
    }
    if (!b.Text)
        b.Text = SvcDup(L"");
    return b.Text;
}

/* $(Arg0) to $(Arg31) of the arguments of a run. */
static WCHAR *with_arguments(const WCHAR *text, DWORD count, const WCHAR *const *args)
{
    TX_BUF b = {};
    for (const WCHAR *p = text; p && *p;) {
        if (!wcsncmp(p, L"$(Arg", 5) && p[5] >= L'0' && p[5] <= L'9') {
            int n = p[5] - L'0';
            const WCHAR *q = p + 6;
            if (*q >= L'0' && *q <= L'9')
                n = n * 10 + (*q++ - L'0');
            if (*q == L')') {
                if ((DWORD)n < count && args && args[n])
                    TxBufPut(&b, args[n]);
                p = q + 1;
                continue;
            }
        }
        WCHAR one[2] = {*p++, 0};
        TxBufPut(&b, one);
    }
    if (!b.Text)
        b.Text = SvcDup(L"");
    return b.Text;
}

static void finish(INSTANCE *done, DWORD result)
{
    EnterCriticalSection(&g_tsch_lock);
    TASK *t = done->Task;
    if (t) {
        t->LastResult = result;
        t->Running--;
        write_dynamic(t);
    }
    for (INSTANCE **link = &g_instances; *link; link = &(*link)->Next)
        if (*link == done) {
            *link = done->Next;
            break;
        }
    LeaveCriticalSection(&g_tsch_lock);
    SvcFree(done);
}

static DWORD WINAPI run_thread(void *context)
{
    RUN_ARGS *a = context;
    if (a->Delay)
        Sleep(a->Delay);
    LPVOID environment = nullptr;
    HMODULE userenv = LoadLibraryW(L"userenv.dll");
    BOOL(WINAPI * create_environment)(LPVOID *, HANDLE, BOOL) =
        userenv ? (BOOL(WINAPI *)(LPVOID *, HANDLE, BOOL))GetProcAddress(userenv, "CreateEnvironmentBlock") : nullptr;
    BOOL(WINAPI * destroy_environment)(LPVOID) =
        userenv ? (BOOL(WINAPI *)(LPVOID))GetProcAddress(userenv, "DestroyEnvironmentBlock") : nullptr;
    HANDLE own = nullptr;
    HANDLE source = a->Token;
    if (!source && OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &own))
        source = own;
    BOOL made = create_environment && destroy_environment && source && create_environment(&environment, source, FALSE);
    if (own)
        CloseHandle(own);
    DWORD result = 0;
    for (ULONG i = 0; i < a->Count && !a->Instance->Stopped; i++) {
        WCHAR *line = expand(a->Commands[2 * i], made ? environment : nullptr);
        WCHAR *folder = a->Commands[2 * i + 1] ? expand(a->Commands[2 * i + 1], made ? environment : nullptr) : nullptr;
        STARTUPINFOW si = {sizeof(si)};
        si.lpDesktop = (LPWSTR)L"winsta0\\default";
        PROCESS_INFORMATION pi = {};
        DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW;
        BOOL ok;
        if (a->Token) {
            ok = CreateProcessAsUserW(a->Token, nullptr, line, nullptr, nullptr, FALSE, flags, made ? environment : nullptr,
                                      folder && *folder ? folder : nullptr, &si, &pi);
        } else {
            ok = CreateProcessW(nullptr, line, nullptr, nullptr, FALSE, flags, made ? environment : nullptr,
                                folder && *folder ? folder : nullptr, &si, &pi);
        }
        result = ok ? 0 : (DWORD)HR_WIN32(GetLastError());
        if (ok) {
            CloseHandle(pi.hThread);
            EnterCriticalSection(&g_tsch_lock);
            a->Instance->Process = pi.hProcess;
            a->Instance->Pid = pi.dwProcessId;
            LeaveCriticalSection(&g_tsch_lock);
            WaitForSingleObject(pi.hProcess, INFINITE);
            GetExitCodeProcess(pi.hProcess, &result);
            EnterCriticalSection(&g_tsch_lock);
            a->Instance->Process = nullptr;
            LeaveCriticalSection(&g_tsch_lock);
            CloseHandle(pi.hProcess);
        } else {
            SvcLog("schedule: an action of %ls did not start: %lu", a->Instance->Task ? a->Instance->Task->Path : L"?",
                   GetLastError());
        }
        SvcFree(line);
        SvcFree(folder);
        if (!ok)
            break;
    }
    if (made)
        destroy_environment(environment);
    if (userenv)
        FreeLibrary(userenv);
    finish(a->Instance, result);
    for (ULONG i = 0; i < 2 * a->Count; i++)
        SvcFree(a->Commands[i]);
    SvcFree(a->Commands);
    if (a->Token)
        CloseHandle(a->Token);
    SvcFree(a);
    return 0;
}

/* a run of the exec actions of a stored task, on a thread of its own, with the lock held. */
static HRESULT run_task(TASK *t, const STORED *s, const CALLER *c, DWORD count, const WCHAR *const *args, GUID *guid,
                        DWORD delay_ms)
{
    const WCHAR *policy = TxText(TxChild(s->Xml, L"Settings"), L"MultipleInstancesPolicy");
    if (t->Running && (!policy || !wcscmp(policy, L"IgnoreNew") || !wcscmp(policy, L"Queue")))
        return S_OK;
    if (t->Running && policy && !wcscmp(policy, L"StopExisting"))
        for (INSTANCE *i = g_instances; i; i = i->Next)
            if (i->Task == t)
                stop_instance(i);
    HANDLE token = user_token(s, c);
    if (token == INVALID_HANDLE_VALUE)
        return HR_WIN32(1245 /* ERROR_NOT_LOGGED_ON */);
    RUN_ARGS *a = SvcAllocZero(sizeof(*a));
    INSTANCE *i = SvcAllocZero(sizeof(*i));
    const TX_NODE *actions = TxChild(s->Xml, L"Actions");
    ULONG n = 0;
    for (const TX_NODE *e = actions ? actions->First : nullptr; e; e = e->Next)
        n += !wcscmp(e->Name, L"Exec");
    if (a)
        a->Commands = SvcAllocZero((2 * n + 1) * sizeof(WCHAR *));
    if (!a || !i || !a->Commands) {
        if (a)
            SvcFree(a->Commands);
        SvcFree(a);
        SvcFree(i);
        if (token)
            CloseHandle(token);
        return E_OUTOFMEMORY;
    }
    for (const TX_NODE *e = actions ? actions->First : nullptr; e; e = e->Next) {
        if (wcscmp(e->Name, L"Exec"))
            continue;
        WCHAR *command = with_arguments(TxText(e, L"Command"), count, args);
        WCHAR *arguments = with_arguments(TxText(e, L"Arguments"), count, args);
        const WCHAR *dir = TxText(e, L"WorkingDirectory");
        SIZE_T chars = wcslen(command) + wcslen(arguments) + 4;
        WCHAR *line = SvcAlloc(chars * sizeof(WCHAR));
        if (line)
            _snwprintf(line, chars, command[0] == L'"' ? L"%ls %ls" : L"\"%ls\" %ls", command, arguments);
        a->Commands[2 * a->Count] = line;
        a->Commands[2 * a->Count + 1] = dir ? with_arguments(dir, count, args) : nullptr;
        a->Count++;
        SvcFree(command);
        SvcFree(arguments);
    }
    UuidCreate((UUID *)&i->Guid);
    i->Task = t;
    a->Instance = i;
    a->Token = token;
    a->Delay = delay_ms;
    i->Next = g_instances;
    g_instances = i;
    t->Running++;
    t->HasRun = TRUE;
    t->LastRun = now();
    t->LastResult = (DWORD)SCHED_S_TASK_RUNNING_;
    write_dynamic(t);
    *guid = i->Guid;
    HANDLE thread = CreateThread(nullptr, 0, run_thread, a, 0, nullptr);
    if (!thread) {
        g_instances = i->Next;
        t->Running--;
        for (ULONG k = 0; k < 2 * a->Count; k++)
            SvcFree(a->Commands[k]);
        SvcFree(a->Commands);
        if (token)
            CloseHandle(token);
        SvcFree(a);
        SvcFree(i);
        return HR_WIN32(GetLastError());
    }
    CloseHandle(thread);
    return S_OK;
}

/* ------------------------------------------------------------------ triggers */

/* every task below a folder of the store, each given to the visitor with its path. */
static void each_task(const WCHAR *folder, void (*visit)(const WCHAR *path, void *context), void *context)
{
    WCHAR *dir = file_of(folder);
    if (!dir)
        return;
    SIZE_T chars = wcslen(dir) + 3;
    WCHAR *pattern = SvcAlloc(chars * sizeof(WCHAR));
    _snwprintf(pattern, chars, L"%ls\\*", dir);
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern, &data);
    while (find != INVALID_HANDLE_VALUE) {
        if (data.cFileName[0] != L'.') {
            SIZE_T n = wcslen(folder) + wcslen(data.cFileName) + 2;
            WCHAR *child = SvcAlloc(n * sizeof(WCHAR));
            if (child) {
                _snwprintf(child, n, L"%ls\\%ls", folder[1] ? folder : L"", data.cFileName);
                if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    each_task(child, visit, context);
                else
                    visit(child, context);
                SvcFree(child);
            }
        }
        if (!FindNextFileW(find, &data)) {
            FindClose(find);
            break;
        }
    }
    SvcFree(pattern);
    SvcFree(dir);
}

typedef struct SWEEP {
    const WCHAR *Trigger; /* runs the tasks of a trigger of this kind, else the time and calendar ones that are due. */
    ULONGLONG Now, Next;
} SWEEP;

static void sweep_task(const WCHAR *path, void *context)
{
    SWEEP *w = context;
    STORED s;
    if (FAILED(stored_load(path, &s)))
        return;
    TASK *t = task_load(path);
    GUID g;
    if (t && s.Enabled) {
        if (w->Trigger) {
            if (TxHasTrigger(s.Xml, w->Trigger))
                run_task(t, &s, nullptr, 0, nullptr, &g, (DWORD)(TxTriggerDelay(s.Xml, w->Trigger) * 1000));
        } else {
            /* a run that is due since the last look, then the next one after now. */
            ULONGLONG times[1];
            if (t->Due && t->Due <= w->Now)
                run_task(t, &s, nullptr, 0, nullptr, &g, 0);
            t->Due = TxRunTimes(s.Xml, w->Now + 1, ~0ull >> 2, times, 1) ? times[0] : 0;
            if (t->Due && (!w->Next || t->Due < w->Next))
                w->Next = t->Due;
        }
    }
    stored_free(&s);
}

static DWORD WINAPI trigger_thread(void *unused)
{
    EnterCriticalSection(&g_tsch_lock);
    /* the machine started and its user logged on, both before the first look. */
    SWEEP boot = {L"BootTrigger", now(), 0}, logon = {L"LogonTrigger", now(), 0};
    each_task(L"\\", sweep_task, &boot);
    each_task(L"\\", sweep_task, &logon);
    LeaveCriticalSection(&g_tsch_lock);
    for (;;) {
        EnterCriticalSection(&g_tsch_lock);
        SWEEP w = {nullptr, now(), 0};
        each_task(L"\\", sweep_task, &w);
        LeaveCriticalSection(&g_tsch_lock);
        ULONGLONG wait = w.Next > w.Now ? (w.Next - w.Now) / 10000 + 1 : 3600000;
        WaitForSingleObject(g_wake, wait > 3600000 ? 3600000 : (DWORD)wait);
    }
    return 0;
}

/* ------------------------------------------------------------------ start */

static RPC_STATUS RPC_ENTRY allow_local(RPC_IF_HANDLE interface, void *context)
{
    return RPC_S_OK;
}

/* D:(A;;0x1;;;WD)(A;;0x1;;;AC): every local caller connects and calls, app containers included. */
static const BYTE g_endpoint_sd[] = {
    0x01, 0x00, 0x04, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x34, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00, 0x18, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x02, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
};

static DWORD WINAPI register_endpoint(void *context)
{
    RPC_BINDING_VECTOR *bindings = context;
    RPC_STATUS status = RPC_S_OK;
    for (int attempt = 0; attempt < 3000; attempt++) {
        status = RpcEpRegisterW(tsch_s_ifspec, bindings, nullptr, (RPC_WSTR)L"Task Scheduler Service");
        if (!status)
            break;
        Sleep(20);
    }
    RpcBindingVectorFree(&bindings);
    SetEvent(g_registered);
    if (status)
        SvcLog("schedule: the endpoint mapper did not take the endpoint: %u", status);
    return 0;
}

void ScheduleWaitRegistered(DWORD milliseconds)
{
    if (g_registered)
        WaitForSingleObject(g_registered, milliseconds);
}

void ScheduleStart(void)
{
    InitializeCriticalSection(&g_tsch_lock);
    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_registered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    WCHAR *root = file_of(L"\\");
    if (root)
        CreateDirectoryW(root, nullptr);
    SvcFree(root);
    RPC_BINDING_VECTOR *bindings = nullptr;
    RPC_STATUS status = RpcServerUseProtseqW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT, (void *)g_endpoint_sd);
    if (!status)
        status = RpcServerRegisterIf3(tsch_s_ifspec, nullptr, nullptr, RPC_IF_AUTOLISTEN | RPC_IF_ALLOW_LOCAL_ONLY,
                                      RPC_C_LISTEN_MAX_CALLS_DEFAULT, (unsigned int)-1, allow_local,
                                      (void *)g_endpoint_sd);
    if (!status)
        status = RpcServerInqBindings(&bindings);
    HANDLE thread = !status ? CreateThread(nullptr, 0, register_endpoint, bindings, 0, nullptr) : nullptr;
    if (thread)
        CloseHandle(thread);
    else
        SetEvent(g_registered);
    if (status)
        SvcLog("schedule: the rpc server did not start: %u", status);
    thread = CreateThread(nullptr, 0, trigger_thread, nullptr, 0, nullptr);
    if (thread)
        CloseHandle(thread);
}
