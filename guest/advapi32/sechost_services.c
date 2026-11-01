/*
 * the service control functions windows keeps in sechost.dll that advapi32 lacked: creating a service with extra
 * options, the start reason of a running one, the names of per user services and the private entry points of the
 * service control manager. the ones that ask the manager for something its protocol here does not carry check
 * their arguments as windows does and then fail with ERROR_CALL_NOT_IMPLEMENTED, the answer of a manager that has
 * no such call.
 */
#define MIME_BUILD_ADVAPI32 1
#include "advapi32_private.h"
#include <mime/win32/winsvc.h>

#define SC_MANAGER_CONNECT_ 0x0001
#define SC_MANAGER_CREATE_SERVICE_ 0x0002
#define SC_MANAGER_ALL_ACCESS_ 0xF003F
#define SERVICE_START_REASON_INFO 1
#define MACHINE_I386 0x014C
#define USER_SERVICE_NAME_CHARS 257
#define RPC_S_SERVER_UNAVAILABLE_ 1722

ADVAPI SC_HANDLE WINAPI OpenSCManagerA(LPCSTR machine, LPCSTR database, DWORD access);
ADVAPI BOOL WINAPI CloseServiceHandle(SC_HANDLE handle);

static volatile LONG g_security_process;

/* what a program passes to CreateServiceEx besides the arguments of CreateServiceW. */
typedef struct SERVICE_CREATE_OPTIONS {
    BYTE Version;
    BYTE Reserved;
    WORD Machine;
} SERVICE_CREATE_OPTIONS;

ADVAPI SC_HANDLE WINAPI CreateServiceEx(SC_HANDLE manager, LPCWSTR name, LPCWSTR display, DWORD access, DWORD type,
                                        DWORD start, DWORD error_control, LPCWSTR path, LPCWSTR group, LPDWORD tag,
                                        LPCWSTR dependencies, LPCWSTR account, LPCWSTR password,
                                        const SERVICE_CREATE_OPTIONS *options)
{
    if (options) {
        if (options->Version != 1) {
            AdvSetError(ERROR_INVALID_PARAMETER);
            return nullptr;
        }
        /* a service of another machine type is not one this manager can run, and a 32 bit one is a service like any. */
        if (options->Machine && options->Machine != MACHINE_I386) {
            AdvSetError(ERROR_INVALID_PARAMETER);
            return nullptr;
        }
    }
    return CreateServiceW(manager, name, display, access, type, start, error_control, path, group, tag, dependencies,
                          account, password);
}

/* the start reason of the running service the status handle belongs to. */
ADVAPI BOOL WINAPI QueryServiceDynamicInformation(SERVICE_STATUS_HANDLE handle, DWORD level, PVOID *information)
{
    DWORD reason;
    DWORD error = AdvScStatusHandleInfo(handle, nullptr, 0, &reason);
    if (!error && (!information || level != SERVICE_START_REASON_INFO))
        error = ERROR_INVALID_PARAMETER;
    if (error) {
        AdvSetError(error);
        return FALSE;
    }
    DWORD *copy = LocalAlloc(LMEM_FIXED, sizeof(DWORD));
    if (!copy)
        return FALSE;
    *copy = reason;
    *information = copy;
    return TRUE;
}

/* the name of the instance of a per user service template for a user and session. a user service manager is not here. */
ADVAPI DWORD WINAPI QueryUserServiceName(PCWSTR template, PCWSTR user_sid, DWORD session, PWSTR name, DWORD *size)
{
    DWORD needed = USER_SERVICE_NAME_CHARS, error;
    (void)session;
    SC_HANDLE manager = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT_);
    if (!manager) {
        error = GetLastError();
    } else if (!template || !user_sid || !size || (!name && *size)) {
        error = ERROR_INVALID_PARAMETER;
    } else {
        error = ERROR_NOT_SUPPORTED;
    }
    if (manager)
        CloseServiceHandle(manager);
    if (size)
        *size = needed;
    return error;
}

ADVAPI DWORD WINAPI QueryLocalUserServiceName(PCWSTR template, PWSTR name, DWORD *size)
{
    DWORD error = ERROR_SUCCESS;
    SC_HANDLE manager = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT_);
    TOKEN_USER *user = nullptr;
    if (!manager) {
        error = GetLastError();
    } else if (!template || !size || (!name && *size)) {
        error = ERROR_INVALID_PARAMETER;
    } else {
        user = AdvQueryTokenUser(nullptr);
        if (!user)
            error = GetLastError();
        else
            error = ERROR_NOT_SUPPORTED;
    }
    AdvFree(user);
    if (manager)
        CloseServiceHandle(manager);
    if (size)
        *size = USER_SERVICE_NAME_CHARS;
    return error;
}

/* "template_context" with the context in lower case hex. */
ADVAPI DWORD WINAPI QueryUserServiceNameForContext(PCWSTR template, ULONGLONG context, PWSTR name, DWORD *size)
{
    if (!template || !context || !size)
        return ERROR_INVALID_PARAMETER;
    if (!name) {
        if (*size)
            return ERROR_INVALID_PARAMETER;
        *size = USER_SERVICE_NAME_CHARS;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    if (*size < USER_SERVICE_NAME_CHARS) {
        *size = USER_SERVICE_NAME_CHARS;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    WCHAR hex[16];
    DWORD digits = 0;
    for (ULONGLONG v = context; v; v >>= 4)
        hex[digits++] = L"0123456789abcdef"[v & 15];
    SIZE_T length = wcslen(template);
    /* the text and its terminator must fit the 257 characters. */
    if (length + 1 + digits >= USER_SERVICE_NAME_CHARS) {
        memcpy(name, template, (USER_SERVICE_NAME_CHARS - 1) * sizeof(WCHAR));
        name[USER_SERVICE_NAME_CHARS - 1] = 0;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    memcpy(name, template, length * sizeof(WCHAR));
    name[length++] = L'_';
    while (digits)
        name[length++] = hex[--digits];
    name[length] = 0;
    *size = (DWORD)length + 1;
    return ERROR_SUCCESS;
}

/* lsass marks its process with this, which only matters to it. */
ADVAPI void WINAPI I_ScIsSecurityProcess(void)
{
    InterlockedExchange(&g_security_process, 1);
}

/* the name of the service of this process the status handle belongs to. */
ADVAPI DWORD WINAPI I_ScPnPGetServiceName(HANDLE handle, PWSTR name, DWORD size)
{
    return AdvScStatusHandleInfo(handle, name, size, nullptr);
}

/* ------------------------------------------------------------------ calls only a full manager answers */

static DWORD manager_call(DWORD access)
{
    SC_HANDLE manager = OpenSCManagerA(nullptr, nullptr, access);
    if (!manager)
        return GetLastError();
    CloseServiceHandle(manager);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

ADVAPI DWORD WINAPI ReparseServiceConfig(PVOID service, PVOID a2, PVOID a3)
{
    return service && a2 && a3 ? ERROR_CALL_NOT_IMPLEMENTED : ERROR_INVALID_PARAMETER;
}

ADVAPI DWORD WINAPI I_ScReparseServiceDatabase(DWORD flags)
{
    (void)flags;
    return manager_call(SC_MANAGER_CREATE_SERVICE_);
}

ADVAPI DWORD WINAPI I_ScRegisterPreshutdownRestart(PCWSTR name, DWORD flags)
{
    (void)name;
    (void)flags;
    return manager_call(SC_MANAGER_CONNECT_);
}

ADVAPI DWORD WINAPI I_ScBroadcastServiceControlMessage(DWORD a1, DWORD a2, DWORD a3, PVOID a4)
{
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    return manager_call(SC_MANAGER_ALL_ACCESS_);
}

ADVAPI DWORD WINAPI I_ScSendTSMessage(DWORD a1, DWORD a2, DWORD a3, PVOID a4)
{
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    return manager_call(SC_MANAGER_CONNECT_);
}

/* the manager here has no per service tag table, process id tags and the like belong to a full one. */
typedef struct TAG_NAME_FROM_TAG {
    DWORD ProcessId;
    DWORD Tag;
    DWORD Type;
    PWSTR Name;
} TAG_NAME_FROM_TAG;

typedef struct TAG_NAMES_REFERENCING_MODULE {
    DWORD ProcessId;
    PWSTR ModuleName;
    DWORD Type;
    PWSTR Names;
} TAG_NAMES_REFERENCING_MODULE;

typedef struct TAG_NAME_TAG_MAPPING {
    DWORD ProcessId;
    DWORD Type;
    PVOID Elements;
} TAG_NAME_TAG_MAPPING;

ADVAPI DWORD WINAPI I_QueryTagInformation(PVOID machine, DWORD level, PVOID parameters)
{
    (void)machine;
    if (level == 1) {
        const TAG_NAME_FROM_TAG *p = parameters;
        if (!p || !p->ProcessId || !p->Tag || p->Name)
            return ERROR_INVALID_PARAMETER;
    } else if (level == 2) {
        const TAG_NAMES_REFERENCING_MODULE *p = parameters;
        if (!p || !p->ProcessId || !p->ModuleName || p->Names)
            return ERROR_INVALID_PARAMETER;
    } else if (level == 3) {
        const TAG_NAME_TAG_MAPPING *p = parameters;
        if (!p || p->Elements)
            return ERROR_INVALID_PARAMETER;
    } else {
        return ERROR_INVALID_PARAMETER;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}

ADVAPI DWORD WINAPI I_ScQueryServiceConfig(PVOID handle, DWORD level, PVOID *config)
{
    (void)handle;
    (void)level;
    (void)config;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

ADVAPI DWORD WINAPI I_ScSendPnPMessage(PVOID a1, ULONGLONG a2, DWORD message, DWORD flags, int *data, PVOID a6)
{
    (void)a1;
    (void)a2;
    (void)a6;
    SC_HANDLE manager = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT_);
    if (!manager)
        return GetLastError();
    CloseServiceHandle(manager);
    (void)flags;
    if (message == 12 || (message == 13) || (message == 11 && data))
        return ERROR_CALL_NOT_IMPLEMENTED;
    return ERROR_INVALID_PARAMETER;
}

typedef DWORD(WINAPI *RPC_IMPERSONATE)(PVOID binding);
typedef DWORD(WINAPI *RPC_REVERT)(PVOID binding);

ADVAPI DWORD WINAPI I_ScValidatePnPService(PVOID a1, PCWSTR name, BOOL impersonate, ULONGLONG *result)
{
    (void)a1;
    (void)name;
    (void)result;
    SC_HANDLE manager = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT_);
    if (!manager)
        return GetLastError();
    DWORD error = ERROR_CALL_NOT_IMPLEMENTED;
    HMODULE rpcrt4 = impersonate ? LoadLibraryW(L"rpcrt4.dll") : nullptr;
    if (impersonate) {
        RPC_IMPERSONATE function = rpcrt4 ? (RPC_IMPERSONATE)(void *)GetProcAddress(rpcrt4, "RpcImpersonateClient") : nullptr;
        error = function ? function(nullptr) : ERROR_PROC_NOT_FOUND;
        if (!error) {
            RPC_REVERT revert = (RPC_REVERT)(void *)GetProcAddress(rpcrt4, "RpcRevertToSelf");
            if (revert)
                revert(nullptr);
            error = ERROR_CALL_NOT_IMPLEMENTED;
        }
    }
    CloseServiceHandle(manager);
    return error;
}

/* the answer of the manager is ignored, so a message that was sent only counts as handed to it. */
ADVAPI DWORD WINAPI ScSendSynchronousPowerMessage(ULONGLONG a1, DWORD event, PVOID data, PVOID a4)
{
    (void)a1;
    (void)event;
    (void)data;
    (void)a4;
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT_);
    if (!manager)
        return GetLastError();
    CloseServiceHandle(manager);
    return ERROR_SUCCESS;
}

/* directories and registry keys of services, and their process tokens, are the manager's to hand out. */
ADVAPI DWORD WINAPI GetServiceDirectory(PVOID service, DWORD type, PVOID buffer, DWORD size, PVOID needed)
{
    (void)buffer;
    (void)size;
    return service && type == 0 && needed ? ERROR_CALL_NOT_IMPLEMENTED : ERROR_INVALID_PARAMETER;
}

ADVAPI DWORD WINAPI GetSharedServiceDirectory(PVOID service, DWORD type, PVOID buffer, DWORD size, PVOID needed)
{
    (void)buffer;
    (void)size;
    if (!service || !needed)
        return ERROR_INVALID_PARAMETER;
    return type ? ERROR_NOT_SUPPORTED : ERROR_CALL_NOT_IMPLEMENTED;
}

ADVAPI DWORD WINAPI GetServiceRegistryStateKey(PVOID service, DWORD type, DWORD access, PVOID key)
{
    (void)access;
    return service && type <= 1 && key ? ERROR_CALL_NOT_IMPLEMENTED : ERROR_INVALID_PARAMETER;
}

ADVAPI DWORD WINAPI GetSharedServiceRegistryStateKey(PVOID service, DWORD type, DWORD access, PVOID key)
{
    (void)access;
    if (!service || !key)
        return ERROR_INVALID_PARAMETER;
    return type ? ERROR_NOT_SUPPORTED : ERROR_CALL_NOT_IMPLEMENTED;
}

ADVAPI DWORD WINAPI GetServiceProcessToken(PVOID service, PVOID manager, PVOID token)
{
    return service && manager && token ? ERROR_CALL_NOT_IMPLEMENTED : ERROR_INVALID_PARAMETER;
}

/* a subscription needs the change notification state of the service, which the manager here does not publish. */
ADVAPI DWORD WINAPI SubscribeServiceChangeNotifications(PVOID service, DWORD type, PVOID callback, PVOID context,
                                                        PVOID *subscription)
{
    (void)service;
    (void)callback;
    (void)context;
    (void)subscription;
    if (type > 2)
        return ERROR_INVALID_PARAMETER;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

ADVAPI PVOID WINAPI UnsubscribeServiceChangeNotifications(PVOID subscription)
{
    return subscription ? LocalFree(subscription) : nullptr;
}

/* a device notification goes through the configuration manager to the handler of the service. */
ADVAPI PVOID WINAPI I_ScRegisterDeviceNotification(HANDLE handle, PVOID filter, DWORD flags)
{
    DWORD error = ERROR_INVALID_FLAGS;
    (void)filter;
    if ((flags & 3) == 1)
        error = AdvScStatusHandleInfo(handle, nullptr, 0, nullptr) ? ERROR_INVALID_HANDLE : ERROR_CALL_NOT_IMPLEMENTED;
    else if ((flags & 3) == 2)
        error = ERROR_CALL_NOT_IMPLEMENTED;
    AdvSetError(error);
    return nullptr;
}

/* no registration is ever handed out, so there is none to take back. */
ADVAPI BOOL WINAPI I_ScUnregisterDeviceNotification(PVOID registration)
{
    (void)registration;
    AdvSetError(ERROR_INVALID_HANDLE);
    return FALSE;
}

/* the binding of the service control manager is an rpc handle, and the manager here is not reached through rpc. */
ADVAPI PVOID WINAPI I_ScRpcBindA(PCSTR server)
{
    (void)server;
    AdvSetError(RPC_S_SERVER_UNAVAILABLE_);
    return nullptr;
}

ADVAPI PVOID WINAPI I_ScRpcBindW(PCWSTR server)
{
    (void)server;
    AdvSetError(RPC_S_SERVER_UNAVAILABLE_);
    return nullptr;
}
