/*
 * httpapi.dll: the HTTP Server API. It is the library of programs that serve http, .NET's HttpListener among them;
 * the work is the driver's, \Device\Http in guest/ntos/io/http.c, which the library opens by name and talks to with
 * the device controls of Windows' http.sys. The calls are those of the library of Windows 11, and each checks what
 * Windows checks before it asks the driver.
 *
 * what the driver does not do yet fails with the error Windows gives for a feature that is not there, never with a
 * success: the response cache, server push, delegation, client certificates, https and the service configuration.
 */
#include <mime/win32/winbase.h>
#include <mime/ntapi.h>
#include <mime/http.h>

#define ERROR_HTTP_VERSION_MISMATCH_ 1154
#define ERROR_DLL_INIT_FAILED_ 1114
#define ERROR_INVALID_PARAMETER_ 87
#define ERROR_NOT_SUPPORTED_ 50
#define ERROR_IO_PENDING_ 997

typedef struct {
    USHORT Major, Minor;
} HTTPAPI_VERSION;

typedef ULONGLONG HTTP_REQUEST_ID, HTTP_CONNECTION_ID, HTTP_SERVER_SESSION_ID, HTTP_URL_GROUP_ID, HTTP_URL_CONTEXT;

typedef struct {
    ULONG Policy;
    ULONG SecondsToLive;
} HTTP_CACHE_POLICY;

typedef struct {
    ULONG Type;
} HTTP_LOG_DATA;

#define HTTP_LOG_DATA_TYPE_FIELDS 2

static CRITICAL_SECTION g_init_lock;
static HANDLE g_channel;
static LONG g_listener, g_config, g_resources;
static DWORD g_tls = TLS_OUT_OF_INDEXES;

typedef struct EVENT_CACHE {
    struct EVENT_CACHE *next;
    HANDLE event;
} EVENT_CACHE;

static CRITICAL_SECTION g_event_lock;
static EVENT_CACHE *g_events;

/* ------------------------------------------------------------ the driver */

/* a thread's own event for the calls that wait. */
static NTSTATUS acquire_event(HANDLE *out)
{
    HANDLE e = TlsGetValue(g_tls);
    if (e) {
        *out = e;
        return STATUS_SUCCESS;
    }
    NTSTATUS status = NtCreateEvent(&e, 0x1F0003, nullptr, SynchronizationEvent, FALSE);
    if (!NT_SUCCESS(status))
        return status;
    EVENT_CACHE *node = HeapAlloc(GetProcessHeap(), 0, sizeof(*node));
    if (!node || !TlsSetValue(g_tls, e)) {
        if (node)
            HeapFree(GetProcessHeap(), 0, node);
        NtClose(e);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    node->event = e;
    EnterCriticalSection(&g_event_lock);
    node->next = g_events;
    g_events = node;
    LeaveCriticalSection(&g_event_lock);
    *out = e;
    return STATUS_SUCCESS;
}

static ULONG synchronous_control(HANDLE h, ULONG code, PVOID input, ULONG in_len, PVOID output, ULONG out_len,
                                 PULONG returned)
{
    HANDLE event;
    IO_STATUS_BLOCK io = {};
    if (returned)
        *returned = 0;
    NTSTATUS status = acquire_event(&event);
    if (NT_SUCCESS(status)) {
        status = NtDeviceIoControlFile(h, event, nullptr, nullptr, &io, code, input, in_len, output, out_len);
        if (status == STATUS_PENDING) {
            LARGE_INTEGER forever;
            forever.QuadPart = 0x7FFFFFFFFFFFFFFFLL;
            NtWaitForSingleObject(event, FALSE, &forever);
            status = io.Status;
        }
        if (((ULONG)status & 0xC0000000) != 0xC0000000 && returned)
            *returned = (ULONG)io.Information;
    }
    return RtlNtStatusToDosError(status);
}

static ULONG overlapped_control(HANDLE h, LPOVERLAPPED overlapped, ULONG code, PVOID input, ULONG in_len, PVOID output,
                                ULONG out_len, PULONG returned)
{
    PIO_STATUS_BLOCK io = (PIO_STATUS_BLOCK)overlapped;
    io->Status = STATUS_PENDING;
    if (returned)
        *returned = 0;
    HANDLE event = overlapped->hEvent;
    /* a low bit on the event handle keeps the request off the completion port. */
    PVOID context = ((ULONG_PTR)event & 1) ? nullptr : overlapped;
    NTSTATUS status = NtDeviceIoControlFile(h, event, nullptr, context, io, code, input, in_len, output, out_len);
    if (status != STATUS_PENDING && ((ULONG)status & 0xC0000000) != 0xC0000000 && returned)
        *returned = (ULONG)io->Information;
    return RtlNtStatusToDosError(status);
}

static ULONG device_control(HANDLE h, LPOVERLAPPED overlapped, ULONG code, PVOID input, ULONG in_len, PVOID output,
                            ULONG out_len, PULONG returned)
{
    if (overlapped)
        return overlapped_control(h, overlapped, code, input, in_len, output, out_len, returned);
    return synchronous_control(h, code, input, in_len, output, out_len, returned);
}

/* \Device\Http\<what>[\name] opened with the extended attribute that carries the version, the flags and the channel. */
static ULONG create_handle(PHANDLE out, ULONG version, ACCESS_MASK access, ULONG kind, PCWSTR name, ULONG flags,
                           ULONG disposition, ULONG options, const SECURITY_ATTRIBUTES *attributes, ULONG extra_length,
                           const void *extra)
{
    WCHAR path[288];
    static const PCWSTR devices[] = {HTTP_DEVICE_COMMUNICATION, HTTP_DEVICE_REQUEST_QUEUE, HTTP_DEVICE_CLIENT_SESSION};
    if (!out || kind > 2)
        return STATUS_INVALID_PARAMETER;
    *out = nullptr;
    wcscpy(path, devices[kind]);
    if (name) {
        SIZE_T have = wcslen(path), add = wcslen(name);
        if (have + add + 2 > 288)
            return STATUS_INVALID_PARAMETER;
        path[have] = '\\';
        wcscpy(path + have + 1, name);
    }
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, path);
    SIZE_T wide = sizeof(void *) == 8;
    ULONG value_length = (wide ? 24 : 20) + extra_length;
    ULONG size = 8 + 16 + value_length + 8;
    PUCHAR ea = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!ea)
        return STATUS_INSUFFICIENT_RESOURCES;
    ea[5] = 15;
    *(USHORT *)(ea + 6) = (USHORT)value_length;
    memcpy(ea + 8, HTTP_OPEN_PACKET_NAME, 16);
    PUCHAR v = ea + 24;
    *(ULONG *)v = version;
    *(ULONG *)(v + 4) = flags;
    if (wide) {
        *(ULONGLONG *)(v + 8) = (ULONGLONG)(ULONG_PTR)g_channel;
        *(ULONG *)(v + 16) = extra_length;
        if (extra_length)
            memcpy(v + 20, extra, extra_length);
    } else {
        *(ULONG *)(v + 8) = (ULONG)(ULONG_PTR)g_channel;
        *(ULONG *)(v + 12) = extra_length;
        if (extra_length)
            memcpy(v + 16, extra, extra_length);
    }
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    if (attributes) {
        oa.SecurityDescriptor = attributes->lpSecurityDescriptor;
        if (attributes->bInheritHandle)
            oa.Attributes |= OBJ_INHERIT;
    }
    IO_STATUS_BLOCK io;
    HANDLE h = nullptr;
    NTSTATUS status = NtCreateFile(&h, access, &oa, &io, nullptr, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, disposition,
                                   options, ea, 24 + value_length);
    HeapFree(GetProcessHeap(), 0, ea);
    if (NT_SUCCESS(status))
        *out = h;
    return status;
}

static ULONG open_handle(PHANDLE out, ULONG version, ACCESS_MASK access, ULONG kind, PCWSTR name, ULONG flags,
                         ULONG disposition, ULONG options, const SECURITY_ATTRIBUTES *attributes, ULONG extra_length,
                         const void *extra)
{
    NTSTATUS status = create_handle(out, version, access, kind, name, flags, disposition, options, attributes,
                                    extra_length, extra);
    return RtlNtStatusToDosError(status);
}

/* ------------------------------------------------------------ initialization */

static BOOL initialized(ULONG flags)
{
    BOOL r = FALSE;
    EnterCriticalSection(&g_init_lock);
    if (!flags) {
        r = g_resources != 0;
    } else if (flags & 0x17) {
        r = g_resources != 0;
        if (flags & 1)
            r = r && g_listener != 0;
        if (flags & 2)
            r = r && g_config != 0;
    }
    LeaveCriticalSection(&g_init_lock);
    return r;
}

static ULONG initialize_resources(ULONG flags)
{
    if (!((flags & 0x17) || !flags))
        return ERROR_INVALID_PARAMETER_;
    LONG add = flags ? ((flags & 1) ? 2 : 0) + ((flags & 2) ? 2 : 0) : 1;
    if (g_resources > 0x7FFFFFFF - add)
        return 103;
    g_resources += add;
    return 0;
}

static void terminate_resources(ULONG flags)
{
    LONG sub = flags ? ((flags & 1) ? 2 : 0) + ((flags & 2) ? 2 : 0) : 1;
    g_resources = g_resources > sub ? g_resources - sub : 0;
}

static ULONG initialize_listener(void)
{
    if (!g_listener) {
        ULONG e = open_handle(&g_channel, 2, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, 0, nullptr, 0, FILE_CREATE, 0,
                              nullptr, 0, nullptr);
        if (e) {
            g_channel = nullptr;
            return e;
        }
    }
    g_listener++;
    return 0;
}

static ULONG terminate_listener(void)
{
    if (!g_listener)
        return ERROR_DLL_INIT_FAILED_;
    if (--g_listener == 0 && g_channel) {
        CloseHandle(g_channel);
        g_channel = nullptr;
    }
    return 0;
}

ULONG WINAPI HttpInitialize(HTTPAPI_VERSION version, ULONG flags, PVOID reserved)
{
    if (!((version.Major == 1 && version.Minor == 0) || (version.Major == 2 && version.Minor == 0)))
        return ERROR_HTTP_VERSION_MISMATCH_;
    if (reserved || (flags & 0xFFFFFFE8) || ((flags & 4) && !(flags & 1)))
        return ERROR_INVALID_PARAMETER_;
    EnterCriticalSection(&g_init_lock);
    ULONG e = initialize_resources(flags);
    if (!e) {
        if (flags & 2)
            g_config++;
        if (flags & 1) {
            e = initialize_listener();
            if (e) {
                if (flags & 2)
                    g_config--;
                terminate_resources(flags);
            }
        }
    }
    LeaveCriticalSection(&g_init_lock);
    return e;
}

ULONG WINAPI HttpTerminate(ULONG flags, PVOID reserved)
{
    if (reserved || (flags & 0xFFFFFFE8) || (flags & 5) == 4)
        return ERROR_INVALID_PARAMETER_;
    ULONG e = 0;
    EnterCriticalSection(&g_init_lock);
    if (flags & 1)
        e = terminate_listener();
    if (flags & 2) {
        if (g_config)
            g_config--;
        else if (!e)
            e = ERROR_DLL_INIT_FAILED_;
    }
    terminate_resources(flags);
    LeaveCriticalSection(&g_init_lock);
    return e;
}

/* ------------------------------------------------------------ request queues */

ULONG WINAPI HttpCreateRequestQueueEx(ULONG version, PCWSTR name, const SECURITY_ATTRIBUTES *attributes, ULONG flags,
                                      ULONG extra_version, const ULONG *extra, PHANDLE out)
{
    if (!out)
        return ERROR_INVALID_PARAMETER_;
    *out = nullptr;
    if ((flags & 0xFFFFFFE0) || (flags & 5) == 4 || (flags & 9) == 8 || (flags & 3) == 3)
        return ERROR_INVALID_PARAMETER_;
    if (extra) {
        if (extra_version != 1 || *extra != 1 || !(flags & 1))
            return ERROR_INVALID_PARAMETER_;
    }
    BOOL open_existing = (flags & 1) != 0;
    if (!initialized(1))
        return ERROR_DLL_INIT_FAILED_;
    ULONG access, disposition, options = 0;
    if (open_existing) {
        if (!name || !*name || attributes)
            return ERROR_INVALID_PARAMETER_;
        disposition = FILE_OPEN;
        access = 0x80200000;
    } else {
        access = name ? 0xC01C0000 : 0xC0100000;
        disposition = FILE_CREATE;
    }
    if (flags & 4)
        access |= 0x20;
    if (flags & 8)
        access |= 0x40000000;
    if (flags & 0x10) {
        if (!open_existing || (flags & 2) || (flags & 4) || (flags & 8))
            return ERROR_INVALID_PARAMETER_;
        options = 0x4000;
        access = 0xE0000;
    }
    ULONG extra_length = 0;
    const void *extra_data = nullptr;
    if (extra) {
        extra_length = extra[1];
        extra_data = *(void *const *)(extra + 2);
    }
    HANDLE h;
    ULONG e = open_handle(&h, version, access, 1, name, flags, disposition, options, attributes, extra_length, extra_data);
    if (!e)
        *out = h;
    return e;
}

ULONG WINAPI HttpCreateRequestQueue(HTTPAPI_VERSION version, PCWSTR name, const SECURITY_ATTRIBUTES *attributes, ULONG flags,
                                    PHANDLE out)
{
    return HttpCreateRequestQueueEx(*(ULONG *)&version, name, attributes, flags, 0, nullptr, out);
}

ULONG WINAPI HttpCreateHttpHandle(PHANDLE out, ULONG reserved)
{
    if (!initialized(1))
        return ERROR_DLL_INIT_FAILED_;
    if (!out)
        return ERROR_INVALID_PARAMETER_;
    *out = nullptr;
    if (reserved)
        return ERROR_INVALID_PARAMETER_;
    return HttpCreateRequestQueueEx(1, nullptr, nullptr, 0, 0, nullptr, out);
}

ULONG WINAPI HttpCloseRequestQueue(HANDLE queue)
{
    return CloseHandle(queue) ? 0 : GetLastError();
}

ULONG WINAPI HttpShutdownRequestQueue(HANDLE queue)
{
    return synchronous_control(queue, IOCTL_HTTP_SHUTDOWN_REQUEST_QUEUE, nullptr, 0, nullptr, 0, nullptr);
}

ULONG WINAPI HttpSetRequestQueueProperty(HANDLE queue, ULONG property, PVOID data, ULONG length, ULONG reserved1,
                                         PVOID reserved2)
{
    if (!data || !length || reserved1 || reserved2)
        return ERROR_INVALID_PARAMETER_;
    if (!initialized(1))
        return ERROR_DLL_INIT_FAILED_;
    return synchronous_control(queue, IOCTL_HTTP_SET_REQUEST_QUEUE, &property, 4, data, length, nullptr);
}

ULONG WINAPI HttpQueryRequestQueueProperty(HANDLE queue, ULONG property, PVOID data, ULONG length, ULONG reserved1,
                                           PULONG returned, PVOID reserved2)
{
    if (returned)
        *returned = 0;
    if (reserved1 || reserved2)
        return ERROR_INVALID_PARAMETER_;
    if (!initialized(1))
        return ERROR_DLL_INIT_FAILED_;
    return synchronous_control(queue, IOCTL_HTTP_QUERY_REQUEST_QUEUE, &property, 4, data, length, returned);
}

/* ------------------------------------------------------------ server sessions and url groups */

ULONG WINAPI HttpCreateServerSession(HTTPAPI_VERSION version, HTTP_SERVER_SESSION_ID *id, ULONG reserved)
{
    UCHAR in[24] = {}, out[24] = {};
    if (!id || reserved)
        return ERROR_INVALID_PARAMETER_;
    *id = 0;
    *(USHORT *)in = version.Major;
    *(USHORT *)(in + 2) = version.Minor;
    ULONG e = synchronous_control(g_channel, IOCTL_HTTP_CREATE_SERVER_SESSION, in, 24, out, 24, nullptr);
    if (!e)
        memcpy(id, out + 8, 8);
    return e;
}

ULONG WINAPI HttpCloseServerSession(HTTP_SERVER_SESSION_ID id)
{
    UCHAR in[24] = {};
    memcpy(in + 8, &id, 8);
    return synchronous_control(g_channel, IOCTL_HTTP_CLOSE_SERVER_SESSION, in, 24, nullptr, 0, nullptr);
}

ULONG WINAPI HttpCreateUrlGroup(HTTP_SERVER_SESSION_ID session, HTTP_URL_GROUP_ID *group, ULONG reserved)
{
    UCHAR in[32] = {}, out[32] = {};
    if (!group || reserved)
        return ERROR_INVALID_PARAMETER_;
    memcpy(in + 8, &session, 8);
    ULONG e = synchronous_control(g_channel, IOCTL_HTTP_CREATE_URL_GROUP, in, 32, out, 32, nullptr);
    if (!e)
        memcpy(group, out + 16, 8);
    return e;
}

ULONG WINAPI HttpCloseUrlGroup(HTTP_URL_GROUP_ID group)
{
    UCHAR in[32] = {};
    memcpy(in + 16, &group, 8);
    return synchronous_control(g_channel, IOCTL_HTTP_CLOSE_URL_GROUP, in, 32, nullptr, 0, nullptr);
}

/* the record of adding or removing a prefix: the group, the context, the prefix and flags. */
typedef struct URL_CONTROL {
    ULONG Flags;
    ULONGLONG Group;
    ULONGLONG Context;
    ULONG A;
    PVOID B;
    UNICODE_STRING Url;
    ULONG Extra;
} URL_CONTROL;

static ULONG add_url_to_group(ULONG flags, ULONGLONG group, PCWSTR url, ULONGLONG context)
{
    URL_CONTROL c = {};
    c.Flags = flags;
    c.Group = group;
    c.Context = context;
    NTSTATUS status = RtlInitUnicodeStringEx(&c.Url, url);
    if (!NT_SUCCESS(status))
        return RtlNtStatusToDosError(status);
    return synchronous_control(g_channel, IOCTL_HTTP_ADD_URL, &c, sizeof(c), nullptr, 0, nullptr);
}

static ULONG remove_url_from_group(ULONG flags, ULONGLONG group, PCWSTR url, ULONG extra)
{
    URL_CONTROL c = {};
    c.Flags = flags;
    c.Group = group;
    c.Extra = extra;
    NTSTATUS status = RtlInitUnicodeStringEx(&c.Url, url);
    if (!NT_SUCCESS(status))
        return RtlNtStatusToDosError(status);
    return synchronous_control(g_channel, IOCTL_HTTP_REMOVE_URL, &c, sizeof(c), nullptr, 0, nullptr);
}

ULONG WINAPI HttpAddUrlToUrlGroup(HTTP_URL_GROUP_ID group, PCWSTR url, HTTP_URL_CONTEXT context, ULONG reserved)
{
    if (reserved)
        return ERROR_INVALID_PARAMETER_;
    return add_url_to_group(1, group, url, context);
}

ULONG WINAPI HttpRemoveUrlFromUrlGroup(HTTP_URL_GROUP_ID group, PCWSTR url, ULONG flags)
{
    if ((flags & 0xFFFFFFFE) || ((flags & 1) && url))
        return ERROR_INVALID_PARAMETER_;
    return remove_url_from_group(1, group, url, flags);
}

/* the group of a queue made by HttpCreateHttpHandle. */
static ULONG group_of_queue(HANDLE queue, ULONGLONG *group)
{
    ULONG property = HTTP_PROP_BINDING;
    UCHAR out[32] = {};
    ULONG returned;
    *group = 0;
    ULONG e = synchronous_control(queue, IOCTL_HTTP_QUERY_REQUEST_QUEUE, &property, 4, out, 32, &returned);
    if (!e)
        memcpy(group, out + 16, 8);
    return e;
}

ULONG WINAPI HttpAddUrl(HANDLE queue, PCWSTR url, PVOID reserved)
{
    if (!initialized(1))
        return ERROR_DLL_INIT_FAILED_;
    if (!url || !*url || reserved)
        return ERROR_INVALID_PARAMETER_;
    ULONGLONG group;
    ULONG e = group_of_queue(queue, &group);
    return e ? e : add_url_to_group(1, group, url, 0);
}

ULONG WINAPI HttpRemoveUrl(HANDLE queue, PCWSTR url)
{
    if (!initialized(1))
        return ERROR_DLL_INIT_FAILED_;
    if (!queue || !url || !*url)
        return ERROR_INVALID_PARAMETER_;
    ULONGLONG group;
    ULONG e = group_of_queue(queue, &group);
    if (e)
        return e;
    return remove_url_from_group(1, group, url, 0) ? 2 : 0;
}

ULONG WINAPI HttpFindUrlGroupId(PCWSTR url, HANDLE queue, HTTP_URL_GROUP_ID *group)
{
    struct {
        ULONGLONG Queue;
        UNICODE_STRING Url;
        ULONGLONG Reserved;
    } in = {};
    UCHAR out[32] = {};
    if (!queue || !group)
        return ERROR_INVALID_PARAMETER_;
    *group = 0;
    in.Queue = (ULONGLONG)(ULONG_PTR)queue;
    NTSTATUS status = RtlInitUnicodeStringEx(&in.Url, url);
    if (!NT_SUCCESS(status))
        return RtlNtStatusToDosError(status);
    ULONG size = sizeof(void *) == 8 ? 32 : 24;
    ULONG e = synchronous_control(g_channel, IOCTL_HTTP_FIND_URL_GROUP_ID, &in, sizeof(in), out, size, nullptr);
    if (!e)
        memcpy(group, out + (size - 8), 8);
    return e;
}

/* the properties of sessions and url groups; the logging one is checked the way Windows checks it. */
static BOOL invalid_property(ULONG property)
{
    return property == 13 || property == 14;
}

ULONG WINAPI HttpSetUrlGroupProperty(HTTP_URL_GROUP_ID group, ULONG property, PVOID data, ULONG length)
{
    UCHAR in[32] = {};
    if (!data || !length || invalid_property(property))
        return ERROR_INVALID_PARAMETER_;
    memcpy(in + 16, &group, 8);
    *(ULONG *)(in + 24) = property;
    return synchronous_control(g_channel, IOCTL_HTTP_SET_URL_GROUP, in, 32, data, length, nullptr);
}

ULONG WINAPI HttpQueryUrlGroupProperty(HTTP_URL_GROUP_ID group, ULONG property, PVOID data, ULONG length, PULONG returned)
{
    UCHAR in[32] = {};
    if (returned)
        *returned = 0;
    if (invalid_property(property))
        return ERROR_INVALID_PARAMETER_;
    memcpy(in + 16, &group, 8);
    *(ULONG *)(in + 24) = property;
    return synchronous_control(g_channel, IOCTL_HTTP_QUERY_URL_GROUP, in, 32, data, length, returned);
}

ULONG WINAPI HttpSetServerSessionProperty(HTTP_SERVER_SESSION_ID session, ULONG property, PVOID data, ULONG length)
{
    UCHAR in[24] = {};
    if (!data || !length || invalid_property(property))
        return ERROR_INVALID_PARAMETER_;
    memcpy(in + 8, &session, 8);
    *(ULONG *)(in + 16) = property;
    return synchronous_control(g_channel, IOCTL_HTTP_SET_SERVER_SESSION, in, 24, data, length, nullptr);
}

ULONG WINAPI HttpQueryServerSessionProperty(HTTP_SERVER_SESSION_ID session, ULONG property, PVOID data, ULONG length,
                                            PULONG returned)
{
    UCHAR in[24] = {};
    if (returned)
        *returned = 0;
    if (invalid_property(property))
        return ERROR_INVALID_PARAMETER_;
    memcpy(in + 8, &session, 8);
    *(ULONG *)(in + 16) = property;
    return synchronous_control(g_channel, IOCTL_HTTP_QUERY_SERVER_SESSION, in, 24, data, length, returned);
}

/* ------------------------------------------------------------ requests and responses */

ULONG WINAPI HttpReceiveHttpRequest(HANDLE queue, HTTP_REQUEST_ID id, ULONG flags, PVOID buffer, ULONG length,
                                    PULONG returned, LPOVERLAPPED overlapped)
{
    struct {
        ULONGLONG Id;
        ULONG Flags;
        ULONG Pad;
    } in = {id, flags, 0};
    if (returned)
        *returned = 0;
    return device_control(queue, overlapped, IOCTL_HTTP_RECEIVE_REQUEST, &in, sizeof(in), buffer, length, returned);
}

ULONG WINAPI HttpReceiveRequestEntityBody(HANDLE queue, HTTP_REQUEST_ID id, ULONG flags, PVOID buffer, ULONG length,
                                          PULONG returned, LPOVERLAPPED overlapped)
{
    struct {
        ULONGLONG Id;
        ULONG Flags;
        ULONG Pad;
    } in = {id, flags, 0};
    if (returned)
        *returned = 0;
    return device_control(queue, overlapped, IOCTL_HTTP_RECEIVE_ENTITY_BODY, &in, sizeof(in), buffer, length, returned);
}

/* what a send carries to the driver, whose pointers are the process's own. */
typedef struct SEND_CONTROL {
    PVOID Response;
    USHORT ChunkCount;
    PVOID Chunks;
    HTTP_CACHE_POLICY Policy;
    HTTP_REQUEST_ID Request;
    ULONG Flags;
    PVOID LogData;
} SEND_CONTROL;

ULONG WINAPI HttpSendHttpResponse(HANDLE queue, HTTP_REQUEST_ID id, ULONG flags, PVOID response,
                                  const HTTP_CACHE_POLICY *policy, PULONG sent, PVOID reserved1, ULONG reserved2,
                                  LPOVERLAPPED overlapped, HTTP_LOG_DATA *log)
{
    SEND_CONTROL in = {};
    if (sent)
        *sent = 0;
    if (reserved1 || reserved2 || !response)
        return ERROR_INVALID_PARAMETER_;
    /* the entity chunks sit at the end of the version 1 part of the response. */
    ULONG chunk_count_at = sizeof(void *) == 8 ? 536 : 272;
    in.Response = response;
    in.ChunkCount = *(USHORT *)((PUCHAR)response + chunk_count_at);
    in.Chunks = *(PVOID *)((PUCHAR)response + chunk_count_at + sizeof(void *));
    in.Request = id;
    in.Flags = flags;
    if (policy)
        in.Policy = *policy;
    if (log) {
        if (log->Type != HTTP_LOG_DATA_TYPE_FIELDS)
            return ERROR_INVALID_PARAMETER_;
        in.LogData = log;
    }
    if (!overlapped)
        in.Flags |= HTTP_SEND_SYNCHRONOUS;
    return device_control(queue, overlapped, IOCTL_HTTP_SEND_RESPONSE, &in, sizeof(in), nullptr, 0, sent);
}

ULONG WINAPI HttpSendResponseEntityBody(HANDLE queue, HTTP_REQUEST_ID id, ULONG flags, USHORT count, PVOID chunks,
                                        PULONG sent, PVOID reserved1, ULONG reserved2, LPOVERLAPPED overlapped,
                                        HTTP_LOG_DATA *log)
{
    SEND_CONTROL in = {};
    if (sent)
        *sent = 0;
    if (reserved1 || reserved2)
        return ERROR_INVALID_PARAMETER_;
    if (log) {
        if (log->Type != HTTP_LOG_DATA_TYPE_FIELDS)
            return ERROR_INVALID_PARAMETER_;
        in.LogData = log;
    }
    in.ChunkCount = count;
    in.Chunks = chunks;
    in.Request = id;
    in.Flags = flags;
    if (!overlapped)
        in.Flags |= HTTP_SEND_SYNCHRONOUS;
    return device_control(queue, overlapped, IOCTL_HTTP_SEND_ENTITY_BODY, &in, sizeof(in), nullptr, 0, sent);
}

ULONG WINAPI HttpWaitForDisconnect(HANDLE queue, HTTP_CONNECTION_ID id, LPOVERLAPPED overlapped)
{
    UCHAR in[16] = {};
    memcpy(in, &id, 8);
    return device_control(queue, overlapped, IOCTL_HTTP_WAIT_FOR_DISCONNECT, in, 16, nullptr, 0, nullptr);
}

ULONG WINAPI HttpWaitForDisconnectEx(HANDLE queue, HTTP_CONNECTION_ID id, ULONG reserved, LPOVERLAPPED overlapped)
{
    UCHAR in[16] = {};
    if (reserved)
        return ERROR_INVALID_PARAMETER_;
    memcpy(in, &id, 8);
    in[8] = 1;
    return device_control(queue, overlapped, IOCTL_HTTP_WAIT_FOR_DISCONNECT, in, 16, nullptr, 0, nullptr);
}

ULONG WINAPI HttpCancelHttpRequest(HANDLE queue, HTTP_REQUEST_ID id, LPOVERLAPPED overlapped)
{
    return device_control(queue, overlapped, IOCTL_HTTP_CANCEL_REQUEST, &id, 8, nullptr, 0, nullptr);
}

/* ------------------------------------------------------------ what is not there yet */

ULONG WINAPI HttpAddFragmentToCache(HANDLE queue, PCWSTR prefix, PVOID chunk, PVOID policy, LPOVERLAPPED overlapped)
{
    if (!chunk || !policy)
        return ERROR_INVALID_PARAMETER_;
    return ERROR_NOT_SUPPORTED_;
}

ULONG WINAPI HttpReadFragmentFromCache(HANDLE queue, PCWSTR prefix, PVOID range, PVOID buffer, ULONG length,
                                       PULONG read, LPOVERLAPPED overlapped)
{
    if (read)
        *read = 0;
    return ERROR_NOT_SUPPORTED_;
}

ULONG WINAPI HttpFlushResponseCache(HANDLE queue, PCWSTR prefix, ULONG flags, LPOVERLAPPED overlapped)
{
    return ERROR_NOT_SUPPORTED_;
}

ULONG WINAPI HttpDeclarePush(HANDLE queue, HTTP_REQUEST_ID id, ULONG verb, PCWSTR path, PCSTR query, PVOID headers)
{
    return ERROR_NOT_SUPPORTED_;
}

ULONG WINAPI HttpDelegateRequest(HANDLE queue, HANDLE delegate, HTTP_REQUEST_ID id, HTTP_URL_GROUP_ID group)
{
    return ERROR_NOT_SUPPORTED_;
}

ULONG WINAPI HttpDelegateRequestEx(HANDLE queue, HANDLE delegate, HTTP_REQUEST_ID id, HTTP_URL_GROUP_ID group,
                                   ULONG count, PVOID properties)
{
    if (!g_channel || !queue || !delegate || !id || !group || (count == 0) != (properties == nullptr))
        return ERROR_INVALID_PARAMETER_;
    return ERROR_NOT_SUPPORTED_;
}

ULONG WINAPI HttpEvaluateRequest(HANDLE queue, HTTP_REQUEST_ID id, ULONG flags)
{
    return ERROR_NOT_SUPPORTED_;
}

ULONG WINAPI HttpReceiveClientCertificate(HANDLE queue, HTTP_CONNECTION_ID id, ULONG flags, PVOID info, ULONG size,
                                          PULONG returned, LPOVERLAPPED overlapped)
{
    if (returned)
        *returned = 0;
    return ERROR_NOT_SUPPORTED_;
}

ULONG WINAPI HttpQueryRequestProperty(HANDLE queue, HTTP_REQUEST_ID id, ULONG property, PVOID qualifier,
                                      ULONG qualifier_size, PVOID output, ULONG output_size, PULONG returned,
                                      LPOVERLAPPED overlapped)
{
    if (returned)
        *returned = 0;
    if (!id)
        return ERROR_INVALID_PARAMETER_;
    return ERROR_NOT_SUPPORTED_;
}

ULONG WINAPI HttpSetRequestProperty(HANDLE queue, HTTP_REQUEST_ID id, ULONG property, PVOID input, ULONG size,
                                    LPOVERLAPPED overlapped)
{
    if (!id)
        return ERROR_INVALID_PARAMETER_;
    return ERROR_NOT_SUPPORTED_;
}

ULONG WINAPI HttpWaitForDemandStart(HANDLE queue, LPOVERLAPPED overlapped)
{
    return ERROR_NOT_SUPPORTED_;
}

ULONG WINAPI HttpGetCounters(ULONGLONG group, ULONG kind, PULONG size, PVOID buffer, PULONG count)
{
    if (!size)
        return ERROR_INVALID_PARAMETER_;
    *size = 0;
    if (count)
        *count = 0;
    return ERROR_NOT_SUPPORTED_;
}

ULONG WINAPI HttpPrepareUrl(PVOID reserved, ULONG flags, PCWSTR url, PWSTR *prepared)
{
    if (reserved || flags || !url || !prepared)
        return ERROR_INVALID_PARAMETER_;
    *prepared = nullptr;
    return ERROR_NOT_SUPPORTED_;
}

ULONG WINAPI HttpGetExtension(HTTPAPI_VERSION version, ULONG extension, PVOID buffer, ULONG size)
{
    if (!initialized(0))
        return ERROR_DLL_INIT_FAILED_;
    return ERROR_INVALID_PARAMETER_;
}

BOOL WINAPI HttpIsFeatureSupported(ULONG feature)
{
    return FALSE;
}

ULONG WINAPI HttpControlService(PVOID input, ULONG input_length, PVOID output, ULONG output_length, PULONG returned)
{
    if (returned)
        *returned = 0;
    return ERROR_NOT_SUPPORTED_;
}

ULONG WINAPI HttpSetServiceConfiguration(HANDLE reserved, ULONG id, PVOID info, ULONG length, LPOVERLAPPED overlapped)
{
    return ERROR_NOT_SUPPORTED_;
}

ULONG WINAPI HttpUpdateServiceConfiguration(HANDLE reserved, ULONG id, PVOID info, ULONG length, LPOVERLAPPED overlapped)
{
    return ERROR_NOT_SUPPORTED_;
}

ULONG WINAPI HttpDeleteServiceConfiguration(HANDLE reserved, ULONG id, PVOID info, ULONG length, LPOVERLAPPED overlapped)
{
    return ERROR_NOT_SUPPORTED_;
}

ULONG WINAPI HttpQueryServiceConfiguration(HANDLE reserved, ULONG id, PVOID query, ULONG query_length, PVOID output,
                                           ULONG output_length, PULONG returned, LPOVERLAPPED overlapped)
{
    if (returned)
        *returned = 0;
    return ERROR_NOT_SUPPORTED_;
}

/* ------------------------------------------------------------ the dll */

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        InitializeCriticalSection(&g_init_lock);
        InitializeCriticalSection(&g_event_lock);
        g_tls = TlsAlloc();
        break;
    case DLL_THREAD_DETACH: {
        HANDLE e = g_tls != TLS_OUT_OF_INDEXES ? TlsGetValue(g_tls) : nullptr;
        if (e) {
            EnterCriticalSection(&g_event_lock);
            for (EVENT_CACHE **p = &g_events; *p; p = &(*p)->next) {
                if ((*p)->event == e) {
                    EVENT_CACHE *dead = *p;
                    *p = dead->next;
                    HeapFree(GetProcessHeap(), 0, dead);
                    break;
                }
            }
            LeaveCriticalSection(&g_event_lock);
            NtClose(e);
            TlsSetValue(g_tls, nullptr);
        }
        break;
    }
    case DLL_PROCESS_DETACH:
        if (!reserved && g_tls != TLS_OUT_OF_INDEXES)
            TlsFree(g_tls);
        break;
    }
    return TRUE;
}
