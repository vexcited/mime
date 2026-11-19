/*
 * svcctl and the service channel over ncalrpc:[ntsvcs], what genuine sechost and advapi32 speak to the manager. a call
 * becomes the message a pipe client sends, see main.c, so both reach the same handlers. the svcctl handles of the rpc
 * clients are context handles that hold the id of the manager's handle and the process it belongs to.
 *
 * a service process's dispatcher opens the channel, then answers each control and asks for the next in one call: the
 * call waits in RunRpcExchange until the manager posts a control to the process, see run.c.
 */
#include "services.h"
#include <mime/win32/rpc.h>
#include "services/svcctl.h"

#define SC_RPC_MAGIC 0x48435353 /* "SSCH" */

typedef struct SC_RPC_OBJECT {
    DWORD Magic;
    DWORD Pid;
    UINT64 Id;       /* the manager's handle, 0 for a status handle. */
    UINT64 StatusId; /* what a status handle reports with. */
} SC_RPC_OBJECT;

typedef struct SC_RPC_CHANNEL {
    DWORD Magic;
    DWORD Pid;
} SC_RPC_CHANNEL;

void *WINAPI MIDL_user_allocate(size_t size)
{
    return SvcAlloc(size ? size : 1);
}

void WINAPI MIDL_user_free(void *p)
{
    SvcFree(p);
}

static DWORD caller_pid(void)
{
    ULONG pid = 0;
    if (I_RpcBindingInqLocalClientPID(nullptr, &pid) != RPC_S_OK)
        return 0;
    return pid;
}

static SC_RPC_OBJECT *object_of(SC_RPC_HANDLE handle)
{
    SC_RPC_OBJECT *o = handle;
    return o && o->Magic == SC_RPC_MAGIC ? o : nullptr;
}

/* the request of an operation that takes one handle, with what the writer adds after it. */
static void request_with(SCM_WRITER *w, UINT64 id)
{
    ScmWriterInit(w, 0);
    ScmPutU64(w, id);
}

static DWORD call(DWORD op, SCM_WRITER *request, SCM_READER *reply_reader, SCM_WRITER *reply)
{
    DWORD error = MainRpcRequest(caller_pid(), op, request, reply);
    SvcFree(request->Data);
    UINT32 size = 0;
    UCHAR *data = ScmWriterFinish(reply, &size);
    ScmReaderInit(reply_reader, data, data ? size : 0);
    return error;
}

static SC_RPC_HANDLE new_object(UINT64 id, UINT64 status_id)
{
    SC_RPC_OBJECT *o = SvcAllocZero(sizeof(*o));
    if (!o)
        return nullptr;
    o->Magic = SC_RPC_MAGIC;
    o->Pid = caller_pid();
    o->Id = id;
    o->StatusId = status_id;
    return o;
}

static void close_object(SC_RPC_OBJECT *o, DWORD pid)
{
    if (o->Id) {
        SCM_WRITER w, reply;
        request_with(&w, o->Id);
        MainRpcRequest(pid, SCM_OP_CLOSE_HANDLE, &w, &reply);
        SvcFree(w.Data);
        SvcFree(reply.Data);
    }
    o->Magic = 0;
    SvcFree(o);
}

void WINAPI SC_RPC_HANDLE_rundown(void *handle)
{
    SC_RPC_OBJECT *o = object_of(handle);
    if (o)
        close_object(o, o->Pid);
}

/* the status of the manager's reply, in SERVICE_STATUS_PROCESS order. */
static void read_status(SCM_READER *r, DWORD *out, DWORD count)
{
    for (DWORD i = 0; i < count; i++)
        out[i] = ScmGetU32(r);
}

/* ------------------------------------------------------------------ svcctl */

unsigned long WINAPI svcctl_ROpenSCManager2_s(handle_t binding, unsigned short *database, unsigned long access,
                                              SC_RPC_HANDLE *out)
{
    *out = nullptr;
    SCM_WRITER w, reply;
    SCM_READER r;
    ScmWriterInit(&w, 0);
    ScmPutString(&w, (const WCHAR *)database);
    ScmPutU32(&w, access);
    DWORD error = call(SCM_OP_OPEN_MANAGER, &w, &r, &reply);
    UINT64 id = error ? 0 : ScmGetU64(&r);
    SvcFree(reply.Data);
    if (!error && !(*out = new_object(id, 0)))
        error = ERROR_NOT_ENOUGH_MEMORY;
    return error;
}

unsigned long WINAPI svcctl_ROpenServiceW_s(SC_RPC_HANDLE manager, unsigned short *name, unsigned long access,
                                            SC_RPC_HANDLE *out)
{
    *out = nullptr;
    SC_RPC_OBJECT *m = object_of(manager);
    if (!m || !m->Id)
        return ERROR_INVALID_HANDLE;
    SCM_WRITER w, reply;
    SCM_READER r;
    request_with(&w, m->Id);
    ScmPutString(&w, (const WCHAR *)name);
    ScmPutU32(&w, access);
    DWORD error = call(SCM_OP_OPEN_SERVICE, &w, &r, &reply);
    UINT64 id = error ? 0 : ScmGetU64(&r);
    SvcFree(reply.Data);
    if (!error && !(*out = new_object(id, 0)))
        error = ERROR_NOT_ENOUGH_MEMORY;
    return error;
}

unsigned long WINAPI svcctl_RCloseServiceHandle_s(SC_RPC_HANDLE *handle)
{
    SC_RPC_OBJECT *o = object_of(*handle);
    if (!o)
        return ERROR_INVALID_HANDLE;
    close_object(o, caller_pid());
    *handle = nullptr;
    return ERROR_SUCCESS;
}

unsigned long WINAPI svcctl_RQueryServiceStatus_s(SC_RPC_HANDLE service, SC_SERVICE_STATUS *status)
{
    memset(status, 0, sizeof(*status));
    SC_RPC_OBJECT *o = object_of(service);
    if (!o || !o->Id)
        return ERROR_INVALID_HANDLE;
    SCM_WRITER w, reply;
    SCM_READER r;
    request_with(&w, o->Id);
    DWORD error = call(SCM_OP_QUERY_STATUS, &w, &r, &reply);
    if (!error)
        read_status(&r, (DWORD *)status, 7);
    SvcFree(reply.Data);
    return error;
}

#define SC_STATUS_PROCESS_INFO 0

unsigned long WINAPI svcctl_RQueryServiceStatusEx_s(SC_RPC_HANDLE service, unsigned long level, byte *buffer,
                                                    unsigned long size, unsigned long *needed)
{
    *needed = sizeof(SERVICE_STATUS_PROCESS);
    if (level != SC_STATUS_PROCESS_INFO)
        return ERROR_INVALID_LEVEL;
    SC_RPC_OBJECT *o = object_of(service);
    if (!o || !o->Id)
        return ERROR_INVALID_HANDLE;
    if (size < sizeof(SERVICE_STATUS_PROCESS))
        return ERROR_INSUFFICIENT_BUFFER;
    SCM_WRITER w, reply;
    SCM_READER r;
    request_with(&w, o->Id);
    DWORD error = call(SCM_OP_QUERY_STATUS, &w, &r, &reply);
    if (!error)
        read_status(&r, (DWORD *)buffer, 9);
    SvcFree(reply.Data);
    return error;
}

unsigned long WINAPI svcctl_RControlService_s(SC_RPC_HANDLE service, unsigned long control, SC_SERVICE_STATUS *status)
{
    memset(status, 0, sizeof(*status));
    SC_RPC_OBJECT *o = object_of(service);
    if (!o || !o->Id)
        return ERROR_INVALID_HANDLE;
    SCM_WRITER w, reply;
    SCM_READER r;
    request_with(&w, o->Id);
    ScmPutU32(&w, control);
    ScmPutU32(&w, 0);
    DWORD error = call(SCM_OP_CONTROL_SERVICE, &w, &r, &reply);
    /* the status comes back whatever the control did. */
    read_status(&r, (DWORD *)status, 7);
    SvcFree(reply.Data);
    return error;
}

unsigned long WINAPI svcctl_RStartServiceW_s(SC_RPC_HANDLE service, unsigned long argc, SC_STRING_PTRW *argv)
{
    SC_RPC_OBJECT *o = object_of(service);
    if (!o || !o->Id)
        return ERROR_INVALID_HANDLE;
    SCM_WRITER w, reply;
    SCM_READER r;
    request_with(&w, o->Id);
    ScmPutU32(&w, argv ? argc : 0);
    for (DWORD i = 0; argv && i < argc; i++)
        ScmPutString(&w, (const WCHAR *)argv[i].StringPtr);
    DWORD error = call(SCM_OP_START_SERVICE, &w, &r, &reply);
    SvcFree(reply.Data);
    return error;
}

/* the handle a service reports its status with: sechost asks for it once the start control is taken. */
unsigned long WINAPI svcctl_RI_ScOpenServiceStatusHandle_s(SC_RPC_HANDLE manager, unsigned short *name, long long id,
                                                           SC_RPC_HANDLE *out)
{
    *out = nullptr;
    SC_RPC_OBJECT *m = object_of(manager);
    if (!m || !m->Id)
        return ERROR_INVALID_HANDLE;
    if (!(*out = new_object(0, (UINT64)id)))
        return ERROR_NOT_ENOUGH_MEMORY;
    return ERROR_SUCCESS;
}

unsigned long WINAPI svcctl_RSetServiceStatus_s(SC_RPC_HANDLE handle, SC_SERVICE_STATUS *status)
{
    SC_RPC_OBJECT *o = object_of(handle);
    if (!o || !o->StatusId)
        return ERROR_INVALID_HANDLE;
    SCM_WRITER w, reply;
    SCM_READER r;
    request_with(&w, o->StatusId);
    const DWORD *f = (const DWORD *)status;
    for (int i = 0; i < 7; i++)
        ScmPutU32(&w, f[i]);
    DWORD error = call(SCM_OP_SET_STATUS, &w, &r, &reply);
    SvcFree(reply.Data);
    return error;
}

#define NOT_SERVED(name)                                                                                               \
    unsigned long WINAPI svcctl_##name##_s(handle_t binding)                                                           \
    {                                                                                                                  \
        SvcLog("svcctl: %s is not served", #name);                                                                     \
        return ERROR_CALL_NOT_IMPLEMENTED;                                                                             \
    }

NOT_SERVED(RDeleteService)
NOT_SERVED(RLockServiceDatabase)
NOT_SERVED(RQueryServiceObjectSecurity)
NOT_SERVED(RSetServiceObjectSecurity)
NOT_SERVED(RUnlockServiceDatabase)
NOT_SERVED(RNotifyBootConfigStatus)
NOT_SERVED(RI_ScSetServiceBitsW)
NOT_SERVED(RChangeServiceConfigW)
NOT_SERVED(RCreateServiceW)
NOT_SERVED(REnumDependentServicesW)
NOT_SERVED(REnumServicesStatusW)
NOT_SERVED(ROpenSCManagerW)
NOT_SERVED(RQueryServiceConfigW)
NOT_SERVED(RQueryServiceLockStatusW)
NOT_SERVED(RGetServiceDisplayNameW)
NOT_SERVED(RGetServiceKeyNameW)
NOT_SERVED(RI_ScSetServiceBitsA)
NOT_SERVED(RChangeServiceConfigA)
NOT_SERVED(RCreateServiceA)
NOT_SERVED(REnumDependentServicesA)
NOT_SERVED(REnumServicesStatusA)
NOT_SERVED(ROpenSCManagerA)
NOT_SERVED(ROpenServiceA)
NOT_SERVED(RQueryServiceConfigA)
NOT_SERVED(RQueryServiceLockStatusA)
NOT_SERVED(RStartServiceA)
NOT_SERVED(RGetServiceDisplayNameA)
NOT_SERVED(RGetServiceKeyNameA)
NOT_SERVED(RI_ScGetCurrentGroupStateW)
NOT_SERVED(REnumServiceGroupW)
NOT_SERVED(RChangeServiceConfig2A)
NOT_SERVED(RChangeServiceConfig2W)
NOT_SERVED(RQueryServiceConfig2A)
NOT_SERVED(RQueryServiceConfig2W)
NOT_SERVED(REnumServicesStatusExA)
NOT_SERVED(REnumServicesStatusExW)
NOT_SERVED(RI_ScQueryServiceTagInfo)
NOT_SERVED(RCreateServiceWOW64A)
NOT_SERVED(RCreateServiceWOW64W)
NOT_SERVED(RI_ScQueryServiceTagInfoEx)
NOT_SERVED(RNotifyServiceStatusChange)
NOT_SERVED(RGetNotifyResults)
NOT_SERVED(RCloseNotifyHandle)
NOT_SERVED(RControlServiceExA)
NOT_SERVED(RControlServiceExW)
NOT_SERVED(RI_ScSendPnPMessage)
NOT_SERVED(RI_ScValidatePnPService)
NOT_SERVED(RI_ScQueryServiceConfig)
NOT_SERVED(RQueryServiceConfigEx)
NOT_SERVED(RI_ScRegisterPreshutdownRestart)
NOT_SERVED(RI_ScReparseServiceDatabase)
NOT_SERVED(RQueryUserServiceName)
NOT_SERVED(RCreateWowService)
NOT_SERVED(RGetServiceRegistryStateKey)
NOT_SERVED(RGetServiceDirectory)
NOT_SERVED(RGetServiceProcessToken)
NOT_SERVED(RGetSharedServiceRegistryStateKey)
NOT_SERVED(RGetSharedServiceDirectory)
NOT_SERVED(RReparseServiceConfig)
NOT_SERVED(RI_ScSendSynchronousPowerMessage)

/* ------------------------------------------------------------------ the channel */

#define SC_CHANNEL_MAGIC 0x4E484353 /* "SCHN" */

long WINAPI scchannel_ROpenServiceChannelHandle_s(handle_t binding, char reserved, SC_CHANNEL_HANDLE *out)
{
    *out = nullptr;
    DWORD pid = caller_pid();
    EnterCriticalSection(&g_lock);
    DWORD error = RunRpcOpenChannel(pid);
    LeaveCriticalSection(&g_lock);
    SvcLog("svcctl: process %lu opens its channel: %lu", pid, error);
    if (error)
        return error;
    SC_RPC_CHANNEL *c = SvcAllocZero(sizeof(*c));
    if (!c)
        return ERROR_NOT_ENOUGH_MEMORY;
    c->Magic = SC_CHANNEL_MAGIC;
    c->Pid = pid;
    *out = c;
    return ERROR_SUCCESS;
}

static SC_RPC_CHANNEL *channel_of(SC_CHANNEL_HANDLE handle)
{
    SC_RPC_CHANNEL *c = handle;
    return c && c->Magic == SC_CHANNEL_MAGIC ? c : nullptr;
}

/*
 * the message as sechost's dispatcher reads it: the control, the id a start reports its status with, the reason of a
 * start, and the arguments of a start or the data of a control's event in the payload. the service's name comes apart.
 */
error_status_t WINAPI scchannel_RSendResponseReceiveControls_s(SC_CHANNEL_HANDLE handle, SC_CONTROL_RESPONSE *response,
                                                                unsigned short **name, SC_CONTROL_MESSAGE *message)
{
    *name = nullptr;
    memset(message, 0, sizeof(*message));
    SC_RPC_CHANNEL *c = channel_of(handle);
    if (!c)
        return ERROR_INVALID_HANDLE;
    SCM_CONTROL_POST post = {};
    DWORD error = RunRpcExchange(c->Pid, response->Status, &post);
    SvcLog("svcctl: process %lu answers %lu, gets control %lu for %ls: %lu", c->Pid, response->Status, post.Control,
           post.Name ? post.Name : L"", error);
    if (error)
        return error;
    message->Control = post.Control;
    message->Id = (long long)post.StatusId;
    message->Reason = post.Reason;
    *name = (unsigned short *)post.Name;
    post.Name = nullptr;
    if (post.Control == SCM_CTRL_START_SHARE || post.Control == SCM_CTRL_START_OWN) {
        SC_CONTROL_ARGUMENTS *a = MIDL_user_allocate(offsetof(SC_CONTROL_ARGUMENTS, Strings) +
                                                     (post.Count ? post.Count : 1) * sizeof(SC_STRING_PTRW));
        if (a) {
            a->Count = post.Count;
            for (DWORD i = 0; i < post.Count; i++) {
                a->Strings[i].StringPtr = (unsigned short *)post.Arguments[i];
                post.Arguments[i] = nullptr;
            }
        }
        message->Kind = 1;
        message->Payload.Arguments = a;
    } else if (post.EventData) {
        SC_CONTROL_EVENT *e = MIDL_user_allocate(sizeof(*e));
        if (e) {
            e->Reserved = 0;
            e->Size = post.EventSize;
            e->Data = post.EventData;
            post.EventData = nullptr;
        }
        message->Kind = 2;
        message->Payload.Event = e;
    } else {
        message->Kind = 0;
        message->Payload.Value = nullptr;
    }
    SvcFree(post.EventData);
    for (DWORD i = 0; i < post.Count; i++)
        SvcFree(post.Arguments[i]);
    SvcFree(post.Arguments);
    return ERROR_SUCCESS;
}

long WINAPI scchannel_RCloseServiceChannelHandle_s(SC_CHANNEL_HANDLE *handle)
{
    SC_RPC_CHANNEL *c = channel_of(*handle);
    if (!c)
        return ERROR_INVALID_HANDLE;
    EnterCriticalSection(&g_lock);
    RunRpcChannelClosed(c->Pid);
    LeaveCriticalSection(&g_lock);
    c->Magic = 0;
    SvcFree(c);
    *handle = nullptr;
    return ERROR_SUCCESS;
}

void WINAPI SC_CHANNEL_HANDLE_rundown(void *handle)
{
    SC_RPC_CHANNEL *c = channel_of(handle);
    if (!c)
        return;
    EnterCriticalSection(&g_lock);
    RunRpcChannelClosed(c->Pid);
    LeaveCriticalSection(&g_lock);
    c->Magic = 0;
    SvcFree(c);
}

/* ------------------------------------------------------------------ the server */

/* any local caller may call: the handlers check rights against the descriptors of the manager and its services. */
static RPC_STATUS RPC_ENTRY allow_local(RPC_IF_HANDLE interface, void *context)
{
    return RPC_S_OK;
}

/*
 * the descriptor of the ntsvcs port and of the interfaces: everyone and app containers connect and call, the system and
 * administrators have it all.
 * without one the port would get the default of the manager's token, which lets only those two in, and the services
 * that run as LocalService or NetworkService could not reach the manager.
 */
static const BYTE g_endpoint_sd[] = {
    0x01, 0x00, 0x04, 0x80, 0x14, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x00,
    0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x12, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x05, 0x12, 0x00, 0x00, 0x00, 0x02, 0x00, 0x60, 0x00, 0x04, 0x00, 0x00, 0x00,
    /* system */
    0x00, 0x00, 0x14, 0x00, 0x01, 0x00, 0x1f, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x12, 0x00,
    0x00, 0x00,
    /* administrators */
    0x00, 0x00, 0x18, 0x00, 0x01, 0x00, 0x1f, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x20, 0x00,
    0x00, 0x00, 0x20, 0x02, 0x00, 0x00,
    /* everyone */
    0x00, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00,
    /* all application packages */
    0x00, 0x00, 0x18, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x02, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
};

void RpcStart(void)
{
    RPC_STATUS status = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
                                               (RPC_WSTR)L"ntsvcs", (void *)g_endpoint_sd);
    if (!status)
        status = RpcServerRegisterIf3(svcctl_s_ifspec, nullptr, nullptr, RPC_IF_AUTOLISTEN | RPC_IF_ALLOW_LOCAL_ONLY,
                                      RPC_C_LISTEN_MAX_CALLS_DEFAULT, (unsigned int)-1, allow_local,
                                      (void *)g_endpoint_sd);
    if (!status)
        status = RpcServerRegisterIf3(scchannel_s_ifspec, nullptr, nullptr, RPC_IF_AUTOLISTEN | RPC_IF_ALLOW_LOCAL_ONLY,
                                      RPC_C_LISTEN_MAX_CALLS_DEFAULT, (unsigned int)-1, allow_local,
                                      (void *)g_endpoint_sd);
    if (status)
        SvcLog("svcctl: the rpc server did not start: %u", status);
}
