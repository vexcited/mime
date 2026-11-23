/*
 * the start triggers of services that fire when a client asks for a network endpoint. read from windows 11 26300: a
 * trigger is a key TriggerInfo\<n> of the service with the values Type, Action, GUID, Data<i> and DataType<i>. the
 * manager lists the ones of type 6 as keys named by their data below
 * HKLM\Software\Microsoft\Windows\CurrentVersion\NetworkServiceTriggers\Triggers\<GUID>, where the endpoint mapper
 * finds the interface a client asked for and that no server registered. for the event of rpc interfaces,
 * bc90d167-9470-4139-a9ba-be0bbbf5b74d, the data is the uuid of the interface, or the uuid and an object uuid with a
 * colon between.
 *
 * on windows the hop from the mapper to the manager goes through two more processes. ept_map_auth_sd of RpcEpMap.dll
 * keeps the call of the client pending, NtrbEnqueueRequest, and writes event 1 of the provider
 * Microsoft-Windows-EndpointTriggerProvider, {92aab24d-d9a9-4a60-9f94-201fed3e3e88}, with the subtype and the
 * interface as two strings. ScExtRegisterTriggerConsumer of services.exe gave each trigger of types 2 to 6 to the data
 * activation broker, DabRegisterTriggerConsumer of dabapi.dll over ncalrpc:[dabrpc], with the provider of its type,
 * and the broker answered with a wnf state name the manager waits on through EACreateAggregateEvent of
 * EventAggregation.dll. dab.dll, the server of dabrpc, consumes the providers in a real time tracing session of its
 * own, StartTraceW, EnableTraceEx2 and ProcessTrace, and publishes the state when an event matches. the
 * pending call completes once the started service registers its endpoint. mime has neither real time sessions nor
 * the broker, so the mapper raises the event over the interface of trigger_idl.py instead: one call over
 * ncalrpc:[sctrigger] that returns when the services the trigger starts are running.
 */
#include "services.h"
#include <mime/win32/rpc.h>
#include "services/trigger.h"

#define SERVICE_TRIGGER_TYPE_NETWORK_ENDPOINT 6
#define SERVICE_TRIGGER_ACTION_SERVICE_START 1
#define SERVICE_TRIGGER_DATA_TYPE_STRING 2
#define SERVICES L"SYSTEM\\CurrentControlSet\\Services"
#define PUBLISHED L"Software\\Microsoft\\Windows\\CurrentVersion\\NetworkServiceTriggers\\Triggers"
#define TRIGGER_START_TIMEOUT 30000

typedef struct TRIGGER {
    struct TRIGGER *Next;
    WCHAR Service[SVC_NAME_MAX + 1];
    GUID Event;
    WCHAR *Data;
} TRIGGER;

static TRIGGER *g_triggers;
static SRWLOCK g_triggers_lock = SRWLOCK_INIT;

static void guid_text(const GUID *g, WCHAR *out, size_t chars)
{
    _snwprintf(out, chars, L"%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", g->Data1, g->Data2, g->Data3,
               g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

static void publish(const TRIGGER *t)
{
    WCHAR path[512], event[40];
    guid_text(&t->Event, event, RTL_NUMBER_OF(event));
    _snwprintf(path, RTL_NUMBER_OF(path), PUBLISHED L"\\%ls\\%ls", event, t->Data);
    path[RTL_NUMBER_OF(path) - 1] = 0;
    HKEY key;
    if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, REG_OPTION_VOLATILE, KEY_WRITE, nullptr, &key, nullptr))
        RegCloseKey(key);
}

/* the network endpoint triggers of one service that start it, each with every string of its data. */
static void read_service(HKEY services, const WCHAR *name)
{
    WCHAR path[SVC_NAME_MAX + 16];
    _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\TriggerInfo", name);
    HKEY info;
    if (RegOpenKeyExW(services, path, 0, KEY_READ, &info))
        return;
    for (DWORD i = 0;; i++) {
        WCHAR index[16];
        DWORD chars = RTL_NUMBER_OF(index);
        if (RegEnumKeyExW(info, i, index, &chars, nullptr, nullptr, nullptr, nullptr))
            break;
        HKEY key;
        if (RegOpenKeyExW(info, index, 0, KEY_READ, &key))
            continue;
        DWORD type = 0, action = 0, size = sizeof(GUID);
        GUID event;
        if (DbReadDword(key, L"Type", &type) && DbReadDword(key, L"Action", &action) &&
            type == SERVICE_TRIGGER_TYPE_NETWORK_ENDPOINT && action == SERVICE_TRIGGER_ACTION_SERVICE_START &&
            !RegQueryValueExW(key, L"GUID", nullptr, nullptr, (BYTE *)&event, &size) && size == sizeof(GUID)) {
            for (DWORD item = 0;; item++) {
                WCHAR value[24], data[160];
                DWORD kind = 0, bytes = sizeof(data) - sizeof(WCHAR);
                _snwprintf(value, RTL_NUMBER_OF(value), L"DataType%lu", item);
                if (!DbReadDword(key, value, &kind))
                    break;
                _snwprintf(value, RTL_NUMBER_OF(value), L"Data%lu", item);
                if (kind != SERVICE_TRIGGER_DATA_TYPE_STRING ||
                    RegQueryValueExW(key, value, nullptr, nullptr, (BYTE *)data, &bytes))
                    continue;
                data[bytes / sizeof(WCHAR)] = 0;
                TRIGGER *t = SvcAllocZero(sizeof(*t));
                SIZE_T length = wcslen(data);
                if (!t || !length || !(t->Data = SvcAlloc((length + 1) * sizeof(WCHAR)))) {
                    SvcFree(t);
                    continue;
                }
                wcscpy(t->Data, data);
                wcscpy(t->Service, name);
                t->Event = event;
                publish(t);
                AcquireSRWLockExclusive(&g_triggers_lock);
                t->Next = g_triggers;
                g_triggers = t;
                ReleaseSRWLockExclusive(&g_triggers_lock);
            }
        }
        RegCloseKey(key);
    }
    RegCloseKey(info);
}

/* a value of the type asked for that fills at most the buffer, as NtQueryValueKey into a buffer of that size. */
static DWORD read_value(HKEY key, const WCHAR *name, DWORD type, void *data, DWORD *size)
{
    DWORD kind = 0;
    DWORD error = RegQueryValueExW(key, name, nullptr, &kind, data, size);
    if (!error && kind != type)
        error = ERROR_INVALID_DATA;
    return error;
}

static BOOL known_type(DWORD type)
{
    return (type >= 1 && type <= 7) || type == 20 || type == 30;
}

/*
 * ScGetTriggerInfo of services.exe: the keys 0, 1 and on of TriggerInfo up to the first one missing, at most 64, each
 * with an Action of 1 or 2, a Type the manager knows, a binary Guid of 16 bytes, then DataType<i> of 1 to 5 with a
 * binary Data<i> of at most 512 bytes, up to the first DataType missing. Anything else fails the whole query, and so
 * does a TriggerInfo key without a key 0. The answer: the count, then per trigger the type, the action, the guid and
 * the items, each its type and its bytes.
 */
DWORD TriggerWriteInfo(HKEY service, SCM_WRITER *w)
{
    HKEY info;
    DWORD error = RegOpenKeyExW(service, L"TriggerInfo", 0, KEY_READ, &info);
    if (error == ERROR_FILE_NOT_FOUND) {
        ScmPutU32(w, 0);
        return 0;
    }
    if (error)
        return error;
    UINT32 count_at = w->Size, count = 0;
    ScmPutU32(w, 0);
    for (; count <= 64; count++) {
        WCHAR index[4];
        _snwprintf(index, RTL_NUMBER_OF(index), L"%u", count);
        HKEY key;
        error = RegOpenKeyExW(info, index, 0, KEY_READ, &key);
        if (error == ERROR_FILE_NOT_FOUND) {
            error = count ? 0 : ERROR_INVALID_DATA;
            break;
        }
        if (!error && count == 64)
            error = ERROR_INVALID_DATA;
        if (error)
            break;
        DWORD action = 0, type = 0, size = sizeof(action);
        GUID guid;
        error = read_value(key, L"Action", REG_DWORD, &action, &size);
        if (!error && action != SERVICE_TRIGGER_ACTION_SERVICE_START && action != 2)
            error = ERROR_INVALID_DATA;
        size = sizeof(type);
        if (!error)
            error = read_value(key, L"Type", REG_DWORD, &type, &size);
        if (!error && !known_type(type))
            error = ERROR_INVALID_DATA;
        size = sizeof(guid);
        if (!error)
            error = read_value(key, L"Guid", REG_BINARY, &guid, &size);
        if (!error && size != sizeof(guid))
            error = ERROR_INVALID_DATA;
        if (!error) {
            ScmPutU32(w, type);
            ScmPutU32(w, action);
            ScmPutBytes(w, &guid, sizeof(guid));
            UINT32 items_at = w->Size, items = 0;
            ScmPutU32(w, 0);
            for (;; items++) {
                WCHAR name[16];
                DWORD kind = 0;
                BYTE data[512];
                _snwprintf(name, RTL_NUMBER_OF(name), L"DataType%u", items);
                size = sizeof(kind);
                error = read_value(key, name, REG_DWORD, &kind, &size);
                if (error == ERROR_FILE_NOT_FOUND) {
                    error = 0;
                    break;
                }
                if (!error && (items == 64 || kind < 1 || kind > 5))
                    error = ERROR_INVALID_DATA;
                _snwprintf(name, RTL_NUMBER_OF(name), L"Data%u", items);
                size = sizeof(data);
                if (!error)
                    error = read_value(key, name, REG_BINARY, data, &size);
                if (error)
                    break;
                ScmPutU32(w, kind);
                ScmPutBytes(w, data, size);
            }
            if (!w->Failed)
                memcpy(w->Data + items_at, &items, sizeof(items));
        }
        RegCloseKey(key);
        if (error)
            break;
    }
    RegCloseKey(info);
    if (!error && !w->Failed)
        memcpy(w->Data + count_at, &count, sizeof(count));
    return error;
}

static void read_all(void)
{
    HKEY services;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, SERVICES, 0, KEY_READ, &services))
        return;
    for (DWORD i = 0;; i++) {
        WCHAR name[SVC_NAME_MAX + 2];
        DWORD chars = RTL_NUMBER_OF(name);
        LSTATUS status = RegEnumKeyExW(services, i, name, &chars, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS)
            break;
        if (!status && chars <= SVC_NAME_MAX)
            read_service(services, name);
    }
    RegCloseKey(services);
}

/* the mapper is the only caller there is: a service of the network service account, or the system. */
static BOOL caller_is_mapper(void)
{
    static const BYTE system_sid[] = {1, 1, 0, 0, 0, 0, 0, 5, 18, 0, 0, 0};
    static const BYTE network_sid[] = {1, 1, 0, 0, 0, 0, 0, 5, 20, 0, 0, 0};
    if (RpcImpersonateClient(nullptr))
        return FALSE;
    HANDLE token = nullptr;
    BYTE user[96];
    DWORD size = 0;
    BOOL ok = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token) &&
              GetTokenInformation(token, TokenUser, user, sizeof(user), &size) &&
              (EqualSid(*(PSID *)user, (PSID)system_sid) || EqualSid(*(PSID *)user, (PSID)network_sid));
    if (token)
        CloseHandle(token);
    RpcRevertToSelf();
    return ok;
}

/*
 * starts every service a trigger of this event and data names and waits for each to run, so that the mapper finds the
 * endpoint when the call returns.
 */
unsigned long WINAPI sctrigger_Raise_s(handle_t h, GUID *event, unsigned short *data)
{
    if (!caller_is_mapper())
        return ERROR_ACCESS_DENIED;
    DWORD result = ERROR_NOT_FOUND;
    for (DWORD pass = 0;; pass++) {
        WCHAR name[SVC_NAME_MAX + 1] = L"";
        DWORD seen = 0;
        AcquireSRWLockShared(&g_triggers_lock);
        for (TRIGGER *t = g_triggers; t; t = t->Next) {
            if (memcmp(&t->Event, event, sizeof(GUID)) || _wcsicmp(t->Data, data))
                continue;
            if (seen++ == pass) {
                wcscpy(name, t->Service);
                break;
            }
        }
        ReleaseSRWLockShared(&g_triggers_lock);
        if (!name[0])
            break;
        EnterCriticalSection(&g_lock);
        SERVICE *s = DbFind(name);
        DWORD error = s && DbLoadRecord(s) ? RunStart(s, 0, nullptr, START_CLIENT) : ERROR_SERVICE_DOES_NOT_EXIST;
        if (!error || error == ERROR_SERVICE_ALREADY_RUNNING)
            error = RunWaitState(s, SERVICE_RUNNING, TRIGGER_START_TIMEOUT);
        LeaveCriticalSection(&g_lock);
        if (result)
            result = error;
    }
    return result;
}

static RPC_STATUS RPC_ENTRY allow_local(RPC_IF_HANDLE interface, void *context)
{
    return RPC_S_OK;
}

void TriggerStart(void)
{
    read_all();
    PSECURITY_DESCRIPTOR sd = nullptr;
    ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;SY)(A;;GA;;;NS)", 1, &sd, nullptr);
    RPC_STATUS status = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
                                               (RPC_WSTR)L"sctrigger", sd);
    if (!status)
        status = RpcServerRegisterIf3(sctrigger_s_ifspec, nullptr, nullptr, RPC_IF_AUTOLISTEN | RPC_IF_ALLOW_LOCAL_ONLY,
                                      RPC_C_LISTEN_MAX_CALLS_DEFAULT, (unsigned int)-1, allow_local, sd);
    if (sd)
        LocalFree(sd);
    if (status)
        SvcLog("triggers: the rpc server did not start: %u", status);
}
