/*
 * the http server driver, \Device\Http. httpapi.dll talks to it through device controls: the communication channel
 * makes server sessions and url groups and puts url prefixes in them, a request queue handle receives requests and
 * sends responses. the connections, the parser and the responses are in httpnet.c, the prefixes in httpurl.c.
 *
 * the layouts of what the controls carry are Windows' own, learned from httpapi.dll and http.sys, in both widths.
 * a 32-bit process is told by the machine of its process.
 */
#include "http_p.h"

PDEVICE_OBJECT HttpDeviceChannel, HttpDeviceQueue;
LIST_ENTRY HttpUrls, HttpQueues, HttpSessions, HttpConns, HttpEndpoints, HttpWaits;

static KMUTEX g_lock;
static LIST_ENTRY g_opens;
static ULONG64 g_sequence;
static PDRIVER_OBJECT g_driver;
static SD_BLOB *g_urlacl;

void HttpLock(void)
{
    KeWaitForSingleObject(&g_lock, Executive, KernelMode, FALSE, nullptr);
}

void HttpUnlock(void)
{
    KeReleaseMutex(&g_lock, FALSE);
}

BOOLEAN HttpIsWow(void)
{
    return PsCurrentProcess()->Machine == IMAGE_FILE_MACHINE_I386;
}

ULONG64 HttpNewId(ULONG kind)
{
    return 0xFB00000000000000ull | ((ULONG64)(kind & 0xFF) << 48) | ++g_sequence;
}

/* ------------------------------------------------------------ pending requests */

HTTP_WAIT *HttpNewWait(IO_USER_REQUEST *r, ULONG kind)
{
    HTTP_WAIT *w = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*w), KI_TAG('H', 't', 'W', 't'));
    if (!w)
        return nullptr;
    memset(w, 0, sizeof(*w));
    w->R = r;
    w->Kind = kind;
    w->Wow = HttpIsWow();
    InsertTailList(&HttpWaits, &w->Links);
    return w;
}

void HttpFinishWait(HTTP_WAIT *w, NTSTATUS status, ULONG_PTR information)
{
    RemoveEntryList(&w->Links);
    IopCompleteUserRequest(w->R, status, information);
    ExFreePool(w);
}

/* ends the pending requests of one file, or of one thread or request of it, as a cancel does. */
static BOOLEAN cancel_waits(PFILE_OBJECT file, PETHREAD thread, PVOID iosb)
{
    BOOLEAN found = FALSE;
    PLIST_ENTRY e = HttpWaits.Flink;
    while (e != &HttpWaits) {
        HTTP_WAIT *w = CONTAINING_RECORD(e, HTTP_WAIT, Links);
        e = e->Flink;
        IO_USER_REQUEST *r = w->R;
        if (r->File != file || r->Process != PsCurrentProcess() || (thread && r->Thread != thread) ||
            (iosb && ((ULONG_PTR)r->Iosb & ~(ULONG_PTR)1) != (ULONG_PTR)iosb))
            continue;
        if (w->Kind == HTTP_WAIT_SEND) {
            /* the response is already on its way to the client. */
            continue;
        }
        HttpFinishWait(w, STATUS_CANCELLED, 0);
        found = TRUE;
    }
    return found;
}

NTSTATUS HttpCancel(PFILE_OBJECT file, PETHREAD thread, PVOID iosb)
{
    HttpLock();
    BOOLEAN found = cancel_waits(file, thread, iosb);
    HttpUnlock();
    return found ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

/* ------------------------------------------------------------ the objects */

static HTTP_SESSION *find_session(ULONG64 id)
{
    for (PLIST_ENTRY e = HttpSessions.Flink; e != &HttpSessions; e = e->Flink) {
        HTTP_SESSION *s = CONTAINING_RECORD(e, HTTP_SESSION, Links);
        if (s->Id == id)
            return s;
    }
    return nullptr;
}

static HTTP_GROUP *find_group(ULONG64 id, HTTP_SESSION **session)
{
    for (PLIST_ENTRY e = HttpSessions.Flink; e != &HttpSessions; e = e->Flink) {
        HTTP_SESSION *s = CONTAINING_RECORD(e, HTTP_SESSION, Links);
        for (PLIST_ENTRY g = s->Groups.Flink; g != &s->Groups; g = g->Flink) {
            HTTP_GROUP *group = CONTAINING_RECORD(g, HTTP_GROUP, Links);
            if (group->Id == id) {
                if (session)
                    *session = s;
                return group;
            }
        }
    }
    return nullptr;
}

/* the sessions of a version 1 queue belong to nobody, any channel may put prefixes in them. */
static BOOLEAN owns(const HTTP_SESSION *s, const HTTP_OPEN *o)
{
    return !s->Owner || s->Owner == o;
}

static void default_timeouts(HTTP_TIMEOUTS *t)
{
    memset(t, 0, sizeof(*t));
    t->EntityBody = 120;
    t->DrainEntityBody = 120;
    t->RequestQueue = 65535;
    t->IdleConnection = 120;
    t->HeaderWait = 120;
    t->MinSendRate = 150;
}

static void remove_url(HTTP_URLREG *url)
{
    RemoveEntryList(&url->GroupLinks);
    RemoveEntryList(&url->Links);
    HttpEndpointRelease(url->Port);
    HttpFreeUrl(url);
}

static void close_group(HTTP_GROUP *g)
{
    while (!IsListEmpty(&g->Urls))
        remove_url(CONTAINING_RECORD(g->Urls.Flink, HTTP_URLREG, GroupLinks));
    if (g->Queue) {
        g->Queue->Bound--;
        g->Queue = nullptr;
    }
    RemoveEntryList(&g->Links);
    ExFreePool(g);
}

static void close_session(HTTP_SESSION *s)
{
    while (!IsListEmpty(&s->Groups))
        close_group(CONTAINING_RECORD(s->Groups.Flink, HTTP_GROUP, Links));
    RemoveEntryList(&s->Links);
    if (s->Owner)
        RemoveEntryList(&s->OwnerLinks);
    ExFreePool(s);
}

static HTTP_SESSION *new_session(HTTP_OPEN *owner, USHORT major, USHORT minor)
{
    HTTP_SESSION *s = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*s), KI_TAG('H', 't', 'S', 'e'));
    if (!s)
        return nullptr;
    memset(s, 0, sizeof(*s));
    InitializeListHead(&s->Groups);
    s->Id = HttpNewId(2);
    s->Owner = owner;
    s->VersionMajor = major;
    s->VersionMinor = minor;
    default_timeouts(&s->Timeouts);
    InsertTailList(&HttpSessions, &s->Links);
    if (owner)
        InsertTailList(&owner->Sessions, &s->OwnerLinks);
    else
        InitializeListHead(&s->OwnerLinks);
    return s;
}

static HTTP_GROUP *new_group(HTTP_SESSION *s)
{
    HTTP_GROUP *g = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*g), KI_TAG('H', 't', 'G', 'r'));
    if (!g)
        return nullptr;
    memset(g, 0, sizeof(*g));
    InitializeListHead(&g->Urls);
    g->Id = HttpNewId(3);
    g->Session = s;
    g->Timeouts = s->Timeouts;
    InsertTailList(&s->Groups, &g->Links);
    return g;
}

static void bind_group(HTTP_GROUP *g, HTTP_QUEUE *q)
{
    if (g->Queue)
        g->Queue->Bound--;
    g->Queue = q;
    if (q)
        q->Bound++;
}

/* a queue's handles are all gone: what it held ends. */
static void free_queue(HTTP_QUEUE *q)
{
    HttpQueueDetach(q, STATUS_CANCELLED);
    for (PLIST_ENTRY e = HttpSessions.Flink; e != &HttpSessions; e = e->Flink) {
        HTTP_SESSION *s = CONTAINING_RECORD(e, HTTP_SESSION, Links);
        for (PLIST_ENTRY g = s->Groups.Flink; g != &s->Groups; g = g->Flink) {
            HTTP_GROUP *group = CONTAINING_RECORD(g, HTTP_GROUP, Links);
            if (group->Queue == q)
                bind_group(group, nullptr);
        }
    }
    if (q->AutoSession)
        close_session(q->AutoSession);
    RemoveEntryList(&q->Links);
    if (q->Name)
        ExFreePool(q->Name);
    ExFreePool(q);
}

/* ------------------------------------------------------------ the checks of a url */

static const ULONG g_url_mapping[4] = {1, 1, 1, 1};

/*
 * without a reservation for it, only the system and the administrators may take a prefix. the reservations that
 * http.sys keeps for netsh http add urlacl are not read yet, so a user's only way in is the prefix every user may
 * listen on.
 */
static NTSTATUS url_access(const HTTP_URLREG *url)
{
    if (!SeAccessCheckRequired())
        return STATUS_SUCCESS;
    if (g_urlacl) {
        ACCESS_MASK granted;
        if (NT_SUCCESS(SeCheckAccess(g_urlacl, g_url_mapping, 1, 0, 0, &granted)))
            return STATUS_SUCCESS;
    }
    static const WCHAR temporary[] = L"/temporary_listen_addresses/";
    if (url->Kind == HTTP_URL_STRONG && url->Port == 80 && url->SegmentCount >= 1 && url->SegmentLengths[0] == 26 &&
        !_wcsnicmp(url->Text + url->Segments[0], temporary + 1, 26))
        return STATUS_SUCCESS;
    return STATUS_ACCESS_DENIED;
}

/* ------------------------------------------------------------ properties */

static NTSTATUS copy_out(PVOID output, ULONG out_len, const void *data, ULONG size, ULONG_PTR *information)
{
    *information = size;
    if (out_len < size)
        return STATUS_BUFFER_OVERFLOW;
    if (!output)
        return STATUS_INVALID_USER_BUFFER;
    return KiCopyToUser(output, data, size);
}

static ULONG property_size(ULONG property, BOOLEAN wow)
{
    switch (property) {
    case HTTP_PROP_AUTHENTICATION:
        return wow ? 36 : 64;
    case HTTP_PROP_LOGGING:
        return wow ? 48 : 72;
    case HTTP_PROP_QOS:
    case HTTP_PROP_BINDING:
        return wow ? 8 : 16;
    case HTTP_PROP_TIMEOUTS:
        return 20;
    case HTTP_PROP_STATE:
    case HTTP_PROP_EXTENDED_AUTHENTICATION:
    case HTTP_PROP_LISTEN_ENDPOINT:
    case HTTP_PROP_PROTECTION_LEVEL:
    case HTTP_PROP_DELEGATION:
        return 8;
    case HTTP_PROP_CHANNEL_BIND:
        return wow ? 16 : 24;
    default:
        return 0;
    }
}

/* the file a handle of the caller names, if it is a request queue of this driver. */
static HTTP_OPEN *queue_of_handle(HANDLE handle, PFILE_OBJECT *keep)
{
    PFILE_OBJECT file;
    if (!NT_SUCCESS(ObReferenceHandle(handle, IoFileObjectTypeObject, UserMode, (PVOID *)&file, nullptr)))
        return nullptr;
    HTTP_OPEN *o = file->DeviceObject == HttpDeviceQueue ? file->FsContext : nullptr;
    if (!o || o->Kind != HTTP_OPEN_QUEUE) {
        ObDereferenceObject(file);
        return nullptr;
    }
    *keep = file;
    return o;
}

typedef struct HTTP_PROPERTY_SET {
    UCHAR *Auth;
    ULONG *AuthLength;
    HTTP_TIMEOUTS *Timeouts;
    ULONG *State;
    UCHAR *Qos;
    ULONG *QosLength;
    HTTP_GROUP *Group; /* only for a url group */
} HTTP_PROPERTY_SET;

static NTSTATUS set_property(HTTP_PROPERTY_SET *t, ULONG property, PVOID data, ULONG length, BOOLEAN wow)
{
    ULONG need = property_size(property, wow);
    if (!need)
        return STATUS_INVALID_PARAMETER;
    if (length < need)
        return STATUS_BUFFER_TOO_SMALL;
    UCHAR buffer[80] = {};
    NTSTATUS status = KiCopyFromUser(buffer, data, need < sizeof(buffer) ? need : sizeof(buffer));
    if (!NT_SUCCESS(status))
        return status;
    switch (property) {
    case HTTP_PROP_AUTHENTICATION:
        memcpy(t->Auth, buffer, need < 72 ? need : 72);
        *t->AuthLength = need;
        return STATUS_SUCCESS;
    case HTTP_PROP_TIMEOUTS: {
        HTTP_TIMEOUTS v;
        memcpy(&v, buffer, 20);
        if (v.Flags & 1)
            *t->Timeouts = v;
        return STATUS_SUCCESS;
    }
    case HTTP_PROP_STATE: {
        ULONG flags = *(ULONG *)buffer, state = *(ULONG *)(buffer + 4);
        if ((flags & 1) && state > 1)
            return STATUS_INVALID_PARAMETER;
        if (flags & 1)
            *t->State = state;
        return STATUS_SUCCESS;
    }
    case HTTP_PROP_QOS:
        memcpy(t->Qos, buffer, need);
        *t->QosLength = need;
        return STATUS_SUCCESS;
    default:
        /* accepted and kept nowhere: nothing in the server reads them. */
        return STATUS_SUCCESS;
    }
}

static NTSTATUS query_property(HTTP_PROPERTY_SET *t, ULONG property, PVOID output, ULONG out_len, BOOLEAN wow,
                               ULONG_PTR *information)
{
    ULONG size = property_size(property, wow);
    if (!size)
        return STATUS_INVALID_PARAMETER;
    UCHAR buffer[80] = {};
    switch (property) {
    case HTTP_PROP_AUTHENTICATION:
        if (*t->AuthLength)
            memcpy(buffer, t->Auth, size < 72 ? size : 72);
        break;
    case HTTP_PROP_TIMEOUTS:
        memcpy(buffer, t->Timeouts, 20);
        break;
    case HTTP_PROP_STATE:
        *(ULONG *)buffer = 1;
        *(ULONG *)(buffer + 4) = *t->State;
        break;
    case HTTP_PROP_QOS:
        if (*t->QosLength)
            memcpy(buffer, t->Qos, size < 24 ? size : 24);
        break;
    case HTTP_PROP_BINDING:
        if (t->Group && t->Group->Queue) {
            *(ULONG *)buffer = 1;
        }
        break;
    default:
        break;
    }
    return copy_out(output, out_len, buffer, size, information);
}

/* ------------------------------------------------------------ the controls */

static NTSTATUS capture_input(void *dst, PVOID input, ULONG in_len, ULONG need)
{
    if (in_len < need)
        return STATUS_BUFFER_TOO_SMALL;
    if (!input)
        return STATUS_INVALID_PARAMETER;
    return KiCopyFromUser(dst, input, need);
}

static ULONG64 q(const UCHAR *p)
{
    ULONG64 v;
    memcpy(&v, p, 8);
    return v;
}

static NTSTATUS valid_version(ULONG v)
{
    USHORT major = (USHORT)v, minor = (USHORT)(v >> 16);
    return (major == 1 || major == 2) && minor == 0 ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
}

static NTSTATUS add_url(HTTP_OPEN *o, const UCHAR *in, BOOLEAN wow)
{
    /* the same record in both widths: flags, the group, the context, two words, the prefix and one more. */
    ULONG64 group_id = q(in + 8);
    ULONG length, name_at;
    ULONG64 buffer;
    if (wow) {
        length = *(USHORT *)(in + 0x20);
        buffer = *(ULONG *)(in + 0x24);
        name_at = 0;
    } else {
        length = *(USHORT *)(in + 0x28);
        buffer = q(in + 0x30);
        name_at = 0;
    }
    (void)name_at;
    HTTP_SESSION *session;
    HTTP_GROUP *group = find_group(group_id, &session);
    if (!group || !owns(session, o))
        return STATUS_INVALID_PARAMETER;
    if (!length || (length & 1) || length > HTTP_MAX_URL_CHARS * 2 || !buffer)
        return STATUS_INVALID_PARAMETER;
    PWSTR text = ExAllocatePool2(POOL_FLAG_NON_PAGED, length + sizeof(WCHAR), KI_TAG('H', 't', 'U', 's'));
    if (!text)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = KiCopyFromUser(text, (PVOID)(ULONG_PTR)buffer, length);
    if (!NT_SUCCESS(status)) {
        ExFreePool(text);
        return status;
    }
    text[length / 2] = 0;
    HTTP_URLREG *url;
    status = HttpParseRegistration(text, length / 2, &url);
    ExFreePool(text);
    if (!NT_SUCCESS(status))
        return status;
    url->Context = q(in + 0x10);
    status = url_access(url);
    if (!NT_SUCCESS(status)) {
        HttpFreeUrl(url);
        return status;
    }
    for (PLIST_ENTRY e = HttpUrls.Flink; e != &HttpUrls; e = e->Flink) {
        if (HttpSameUrl(CONTAINING_RECORD(e, HTTP_URLREG, Links), url)) {
            HttpFreeUrl(url);
            return STATUS_OBJECT_NAME_COLLISION;
        }
    }
    status = HttpEndpointAdd(url->Port);
    if (!NT_SUCCESS(status)) {
        HttpFreeUrl(url);
        return status;
    }
    url->Group = group;
    InsertTailList(&group->Urls, &url->GroupLinks);
    InsertTailList(&HttpUrls, &url->Links);
    return STATUS_SUCCESS;
}

static NTSTATUS remove_url_control(HTTP_OPEN *o, const UCHAR *in, BOOLEAN wow)
{
    ULONG64 group_id = q(in + 8);
    ULONG flags = *(ULONG *)(in + (wow ? 0x28 : 0x38));
    ULONG length = *(USHORT *)(in + (wow ? 0x20 : 0x28));
    ULONG64 buffer = wow ? *(ULONG *)(in + 0x24) : q(in + 0x30);
    HTTP_SESSION *session;
    HTTP_GROUP *group = find_group(group_id, &session);
    if (!group || !owns(session, o))
        return STATUS_INVALID_PARAMETER;
    if (flags & 1) {
        while (!IsListEmpty(&group->Urls))
            remove_url(CONTAINING_RECORD(group->Urls.Flink, HTTP_URLREG, GroupLinks));
        return STATUS_SUCCESS;
    }
    if (!length || (length & 1) || length > HTTP_MAX_URL_CHARS * 2 || !buffer)
        return STATUS_INVALID_PARAMETER;
    PWSTR text = ExAllocatePool2(POOL_FLAG_NON_PAGED, length + sizeof(WCHAR), KI_TAG('H', 't', 'U', 's'));
    if (!text)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = KiCopyFromUser(text, (PVOID)(ULONG_PTR)buffer, length);
    if (NT_SUCCESS(status)) {
        text[length / 2] = 0;
        HTTP_URLREG *probe;
        status = HttpParseRegistration(text, length / 2, &probe);
        if (NT_SUCCESS(status)) {
            status = STATUS_OBJECT_NAME_NOT_FOUND;
            for (PLIST_ENTRY e = group->Urls.Flink; e != &group->Urls; e = e->Flink) {
                HTTP_URLREG *url = CONTAINING_RECORD(e, HTTP_URLREG, GroupLinks);
                if (HttpSameUrl(url, probe)) {
                    remove_url(url);
                    status = STATUS_SUCCESS;
                    break;
                }
            }
            HttpFreeUrl(probe);
        }
    }
    ExFreePool(text);
    return status;
}

static NTSTATUS set_group_property(HTTP_OPEN *o, const UCHAR *in, PVOID data, ULONG length, BOOLEAN wow)
{
    HTTP_SESSION *session;
    HTTP_GROUP *group = find_group(q(in + 0x10), &session);
    ULONG property = *(ULONG *)(in + 0x18);
    if (!group || !owns(session, o))
        return STATUS_INVALID_PARAMETER;
    if (property == HTTP_PROP_BINDING) {
        UCHAR b[16] = {};
        ULONG need = wow ? 8 : 16;
        if (length < need)
            return STATUS_BUFFER_TOO_SMALL;
        NTSTATUS status = KiCopyFromUser(b, data, need);
        if (!NT_SUCCESS(status))
            return status;
        if (!(*(ULONG *)b & 1)) {
            bind_group(group, nullptr);
            return STATUS_SUCCESS;
        }
        ULONG64 handle = wow ? *(ULONG *)(b + 4) : q(b + 8);
        PFILE_OBJECT file;
        HTTP_OPEN *queue = queue_of_handle((HANDLE)(ULONG_PTR)handle, &file);
        if (!queue)
            return STATUS_INVALID_HANDLE;
        if (queue->QueueType != 1 && queue->QueueType != 2) {
            ObDereferenceObject(file);
            return STATUS_INVALID_PARAMETER;
        }
        bind_group(group, queue->Queue);
        ObDereferenceObject(file);
        return STATUS_SUCCESS;
    }
    HTTP_PROPERTY_SET t = {group->Auth, &group->AuthLength, &group->Timeouts, &group->State, group->Qos, &group->QosLength,
                           group};
    return set_property(&t, property, data, length, wow);
}

static NTSTATUS handle_channel(HTTP_OPEN *o, IO_USER_REQUEST *r, ULONG code, PVOID input, ULONG in_len, PVOID output,
                               ULONG out_len, ULONG_PTR *information)
{
    BOOLEAN wow = HttpIsWow();
    UCHAR in[0x40] = {};
    NTSTATUS status;
    switch (code) {
    case IOCTL_HTTP_CREATE_SERVER_SESSION: {
        status = capture_input(in, input, in_len, 24);
        if (!NT_SUCCESS(status))
            return status;
        status = valid_version(*(ULONG *)in);
        if (!NT_SUCCESS(status))
            return status;
        if (out_len < 24)
            return STATUS_BUFFER_TOO_SMALL;
        HTTP_SESSION *s = new_session(o, (USHORT)*(ULONG *)in, (USHORT)(*(ULONG *)in >> 16));
        if (!s)
            return STATUS_INSUFFICIENT_RESOURCES;
        UCHAR out[24] = {};
        memcpy(out + 8, &s->Id, 8);
        status = KiCopyToUser(output, out, 24);
        if (!NT_SUCCESS(status)) {
            close_session(s);
            return status;
        }
        *information = 24;
        return STATUS_SUCCESS;
    }
    case IOCTL_HTTP_CLOSE_SERVER_SESSION: {
        status = capture_input(in, input, in_len, 24);
        if (!NT_SUCCESS(status))
            return status;
        HTTP_SESSION *s = find_session(q(in + 8));
        if (!s || !owns(s, o) || s->Auto)
            return STATUS_INVALID_PARAMETER;
        close_session(s);
        return STATUS_SUCCESS;
    }
    case IOCTL_HTTP_QUERY_SERVER_SESSION:
    case IOCTL_HTTP_SET_SERVER_SESSION: {
        status = capture_input(in, input, in_len, 24);
        if (!NT_SUCCESS(status))
            return status;
        HTTP_SESSION *s = find_session(q(in + 8));
        ULONG property = *(ULONG *)(in + 16);
        if (!s || !owns(s, o))
            return STATUS_INVALID_PARAMETER;
        HTTP_PROPERTY_SET t = {s->Auth, &s->AuthLength, &s->Timeouts, &s->State, nullptr, nullptr, nullptr};
        UCHAR qos[24];
        ULONG qos_length = 0;
        t.Qos = qos;
        t.QosLength = &qos_length;
        if (code == IOCTL_HTTP_SET_SERVER_SESSION) {
            status = set_property(&t, property, output, out_len, wow);
            if (NT_SUCCESS(status) && property == HTTP_PROP_TIMEOUTS) {
                for (PLIST_ENTRY e = s->Groups.Flink; e != &s->Groups; e = e->Flink)
                    CONTAINING_RECORD(e, HTTP_GROUP, Links)->Timeouts = s->Timeouts;
            }
            return status;
        }
        return query_property(&t, property, output, out_len, wow, information);
    }
    case IOCTL_HTTP_CREATE_URL_GROUP: {
        status = capture_input(in, input, in_len, 32);
        if (!NT_SUCCESS(status))
            return status;
        HTTP_SESSION *s = find_session(q(in + 8));
        if (!s || !owns(s, o))
            return STATUS_INVALID_PARAMETER;
        if (out_len < 32)
            return STATUS_BUFFER_TOO_SMALL;
        HTTP_GROUP *g = new_group(s);
        if (!g)
            return STATUS_INSUFFICIENT_RESOURCES;
        UCHAR out[32] = {};
        memcpy(out + 16, &g->Id, 8);
        status = KiCopyToUser(output, out, 32);
        if (!NT_SUCCESS(status)) {
            close_group(g);
            return status;
        }
        *information = 32;
        return STATUS_SUCCESS;
    }
    case IOCTL_HTTP_CLOSE_URL_GROUP: {
        status = capture_input(in, input, in_len, 32);
        if (!NT_SUCCESS(status))
            return status;
        HTTP_SESSION *s;
        HTTP_GROUP *g = find_group(q(in + 16), &s);
        if (!g || !owns(s, o))
            return STATUS_INVALID_PARAMETER;
        close_group(g);
        return STATUS_SUCCESS;
    }
    case IOCTL_HTTP_SET_URL_GROUP:
        status = capture_input(in, input, in_len, 32);
        if (!NT_SUCCESS(status))
            return status;
        return set_group_property(o, in, output, out_len, wow);
    case IOCTL_HTTP_QUERY_URL_GROUP: {
        status = capture_input(in, input, in_len, 32);
        if (!NT_SUCCESS(status))
            return status;
        HTTP_SESSION *s;
        HTTP_GROUP *g = find_group(q(in + 16), &s);
        ULONG property = *(ULONG *)(in + 24);
        if (!g || !owns(s, o))
            return STATUS_INVALID_PARAMETER;
        HTTP_PROPERTY_SET t = {g->Auth, &g->AuthLength, &g->Timeouts, &g->State, g->Qos, &g->QosLength, g};
        return query_property(&t, property, output, out_len, wow, information);
    }
    case IOCTL_HTTP_ADD_URL:
        status = capture_input(in, input, in_len, wow ? 48 : 64);
        if (!NT_SUCCESS(status))
            return status;
        return add_url(o, in, wow);
    case IOCTL_HTTP_REMOVE_URL:
        status = capture_input(in, input, in_len, wow ? 48 : 64);
        if (!NT_SUCCESS(status))
            return status;
        return remove_url_control(o, in, wow);
    case IOCTL_HTTP_FIND_URL_GROUP_ID: {
        UCHAR find[32] = {};
        ULONG need = wow ? 24 : 32;
        status = capture_input(find, input, in_len, need);
        if (!NT_SUCCESS(status))
            return status;
        ULONG length = *(USHORT *)(find + 8);
        ULONG64 buffer = wow ? *(ULONG *)(find + 12) : q(find + 16);
        PFILE_OBJECT queue_file;
        HTTP_OPEN *queue_open = queue_of_handle((HANDLE)(ULONG_PTR)(wow ? (ULONG)q(find) : q(find)), &queue_file);
        if (!queue_open)
            return STATUS_INVALID_HANDLE;
        ObDereferenceObject(queue_file);
        if (!length || (length & 1) || length > HTTP_MAX_URL_CHARS * 2 || !buffer)
            return STATUS_INVALID_PARAMETER;
        PWSTR text = ExAllocatePool2(POOL_FLAG_NON_PAGED, length + sizeof(WCHAR), KI_TAG('H', 't', 'U', 's'));
        if (!text)
            return STATUS_INSUFFICIENT_RESOURCES;
        status = KiCopyFromUser(text, (PVOID)(ULONG_PTR)buffer, length);
        HTTP_URLREG *probe = nullptr;
        if (NT_SUCCESS(status)) {
            text[length / 2] = 0;
            status = HttpParseRegistration(text, length / 2, &probe);
        }
        ExFreePool(text);
        if (!NT_SUCCESS(status))
            return status;
        status = STATUS_OBJECT_NAME_NOT_FOUND;
        for (PLIST_ENTRY e = HttpUrls.Flink; e != &HttpUrls; e = e->Flink) {
            HTTP_URLREG *url = CONTAINING_RECORD(e, HTTP_URLREG, Links);
            if (url->Group->Queue != queue_open->Queue || !HttpSameUrl(url, probe))
                continue;
            UCHAR out[32] = {};
            ULONG size = wow ? 24 : 32;
            memcpy(out + (wow ? 16 : 24), &url->Group->Id, 8);
            if (out_len < size) {
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                status = KiCopyToUser(output, out, size);
                *information = size;
            }
            break;
        }
        HttpFreeUrl(probe);
        return status;
    }
    case IOCTL_HTTP_PREPARE_URL:
    case IOCTL_HTTP_GET_COUNTERS:
        return STATUS_NOT_SUPPORTED;
    default:
        return STATUS_INVALID_DEVICE_REQUEST;
    }
}

static NTSTATUS handle_queue(HTTP_OPEN *o, PFILE_OBJECT file, IO_USER_REQUEST *r, ULONG code, PVOID input, ULONG in_len,
                             PVOID output, ULONG out_len, ULONG_PTR *information)
{
    BOOLEAN wow = HttpIsWow();
    UCHAR in[0x40] = {};
    HTTP_QUEUE *queue = o->Queue;
    NTSTATUS status;
    switch (code) {
    case IOCTL_HTTP_RECEIVE_REQUEST:
    case IOCTL_HTTP_RECEIVE_ENTITY_BODY: {
        status = capture_input(in, input, in_len, 16);
        if (!NT_SUCCESS(status))
            return status;
        ULONG flags = *(ULONG *)(in + 8);
        if (flags & ~3u || (flags & 3) == 3)
            return STATUS_INVALID_PARAMETER;
        if (!output)
            return STATUS_INVALID_PARAMETER;
        if (code == IOCTL_HTTP_RECEIVE_REQUEST)
            return HttpReceiveRequest(r, o, q(in), flags, output, out_len, information);
        return HttpReceiveBody(r, o, q(in), flags, output, out_len, information);
    }
    case IOCTL_HTTP_SEND_RESPONSE:
    case IOCTL_HTTP_SEND_ENTITY_BODY: {
        ULONG need = wow ? 0x28 : 0x38;
        status = capture_input(in, input, in_len, need);
        if (!NT_SUCCESS(status))
            return status;
        return HttpSendResponse(r, o, in, in_len, code == IOCTL_HTTP_SEND_RESPONSE, information);
    }
    case IOCTL_HTTP_WAIT_FOR_DISCONNECT:
        status = capture_input(in, input, in_len, 16);
        if (!NT_SUCCESS(status))
            return status;
        return HttpWaitDisconnect(r, o, q(in));
    case IOCTL_HTTP_CANCEL_REQUEST:
        status = capture_input(in, input, in_len, 8);
        if (!NT_SUCCESS(status))
            return status;
        return HttpCancelRequest(o, q(in));
    case IOCTL_HTTP_SHUTDOWN_REQUEST_QUEUE:
        if (!queue->Shutdown) {
            queue->Shutdown = TRUE;
            HttpFailQueueWaits(queue, STATUS_CANCELLED);
        }
        return STATUS_SUCCESS;
    case IOCTL_HTTP_QUERY_REQUEST_QUEUE: {
        status = capture_input(in, input, in_len, 4);
        if (!NT_SUCCESS(status))
            return status;
        ULONG property = *(ULONG *)in;
        UCHAR out[32] = {};
        ULONG size = 4;
        switch (property) {
        case HTTP_PROP_QUEUE_LENGTH:
            *(ULONG *)out = queue->MaxLength;
            break;
        case HTTP_PROP_STATE:
            *(ULONG *)out = queue->Shutdown ? 1 : queue->State;
            break;
        case HTTP_PROP_503_VERBOSITY:
            *(ULONG *)out = queue->Verbosity;
            break;
        case HTTP_PROP_BINDING:
            size = 32;
            if (queue->AutoGroup)
                memcpy(out + 16, &queue->AutoGroup->Id, 8);
            break;
        default:
            return STATUS_INVALID_PARAMETER;
        }
        return copy_out(output, out_len, out, size, information);
    }
    case IOCTL_HTTP_SET_REQUEST_QUEUE: {
        status = capture_input(in, input, in_len, 4);
        if (!NT_SUCCESS(status))
            return status;
        ULONG property = *(ULONG *)in;
        if (property < HTTP_PROP_QUEUE_LENGTH || property > HTTP_PROP_503_VERBOSITY || out_len < 4 || !output)
            return STATUS_INVALID_PARAMETER;
        ULONG value;
        status = KiCopyFromUser(&value, output, 4);
        if (!NT_SUCCESS(status))
            return status;
        if (property == HTTP_PROP_QUEUE_LENGTH) {
            if (!value || value > 0x7FFFFFFF)
                return STATUS_INVALID_PARAMETER;
            queue->MaxLength = value;
        } else if (property == HTTP_PROP_STATE) {
            if (value > 1)
                return STATUS_INVALID_PARAMETER;
            queue->State = value;
        } else {
            if (value > 2)
                return STATUS_INVALID_PARAMETER;
            queue->Verbosity = value;
        }
        return STATUS_SUCCESS;
    }
    case IOCTL_HTTP_QUERY_REQUEST:
    case IOCTL_HTTP_SET_REQUEST:
    case IOCTL_HTTP_EVALUATE_REQUEST:
    case IOCTL_HTTP_RECEIVE_CLIENT_CERT:
    case IOCTL_HTTP_FLUSH_RESPONSE_CACHE:
    case IOCTL_HTTP_ADD_FRAGMENT_TO_CACHE:
    case IOCTL_HTTP_READ_FRAGMENT_FROM_CACHE:
    case IOCTL_HTTP_DECLARE_PUSH:
    case IOCTL_HTTP_DELEGATE_REQUEST_EX:
    case IOCTL_HTTP_WAIT_FOR_DEMAND_START:
        return STATUS_NOT_SUPPORTED;
    default:
        return STATUS_INVALID_DEVICE_REQUEST;
    }
}

BOOLEAN IopIsHttp(PFILE_OBJECT file)
{
    return g_driver && file->DeviceObject && file->DeviceObject->DriverObject == g_driver;
}

NTSTATUS HttpDeviceControl(PFILE_OBJECT file, HANDLE event, PVOID apc, PVOID context, PIO_STATUS_BLOCK iosb, ULONG code,
                           PVOID input, ULONG in_len, PVOID output, ULONG out_len)
{
    HTTP_OPEN *o = file->FsContext;
    if (!o)
        return STATUS_INVALID_HANDLE;
    NTSTATUS status;
    IO_USER_REQUEST *r = IopNewUserRequest(file, event, apc, context, iosb, output, out_len, &status);
    if (!r)
        return status;
    ULONG_PTR information = 0;
    BOOLEAN waiter = r->Waiter;
    HttpLock();
    if (o->Closed) {
        status = STATUS_INVALID_HANDLE;
    } else if (o->Kind == HTTP_OPEN_CHANNEL) {
        status = handle_channel(o, r, code, input, in_len, output, out_len, &information);
    } else if (o->Queue->Closed) {
        status = STATUS_INVALID_HANDLE;
    } else {
        status = handle_queue(o, file, r, code, input, in_len, output, out_len, &information);
    }
    HttpUnlock();
    if (status == STATUS_PENDING) {
        /* the request may have been answered already, and a request nobody waits on is gone with its answer. */
        return waiter ? IopPendUserRequest(r) : STATUS_PENDING;
    }
    return IopImmediateUserRequest(r, status, NT_SUCCESS(status) || status == STATUS_BUFFER_OVERFLOW ? information : 0);
}

/* ------------------------------------------------------------ the device */

static NTSTATUS NTAPI complete_irp(PIRP irp, NTSTATUS status, ULONG_PTR information)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static BOOLEAN valid_queue_name(PCWSTR name, ULONG chars)
{
    if (!chars || chars > 260)
        return FALSE;
    for (ULONG i = 0; i < chars; i++) {
        if (name[i] == '\\' || name[i] == '/' || name[i] < 32)
            return FALSE;
    }
    return TRUE;
}

static HTTP_QUEUE *find_queue(PCWSTR name, ULONG chars)
{
    for (PLIST_ENTRY e = HttpQueues.Flink; e != &HttpQueues; e = e->Flink) {
        HTTP_QUEUE *queue = CONTAINING_RECORD(e, HTTP_QUEUE, Links);
        if (queue->Name && queue->NameLength == chars * sizeof(WCHAR) &&
            !_wcsnicmp(queue->Name, name, chars))
            return queue;
    }
    return nullptr;
}

static NTSTATUS NTAPI http_create(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    PFILE_OBJECT file = stack->FileObject;
    BOOLEAN wow = HttpIsWow();
    /* every open carries the same extended attribute: the version the library speaks, flags and the channel. */
    const UCHAR *ea = irp->AssociatedIrp.SystemBuffer;
    ULONG ea_length = stack->Parameters.Create.EaLength;
    if (!ea || ea_length < 8 + 16)
        return complete_irp(irp, STATUS_INVALID_PARAMETER, 0);
    ULONG name_length = ea[5];
    ULONG value_length = *(const USHORT *)(ea + 6);
    if (name_length != 15 || memcmp(ea + 8, HTTP_OPEN_PACKET_NAME, 15) || 8 + name_length + 1 + value_length > ea_length)
        return complete_irp(irp, STATUS_INVALID_PARAMETER, 0);
    const UCHAR *value = ea + 8 + name_length + 1;
    ULONG version, flags, extra;
    if (wow) {
        if (value_length != 20)
            return complete_irp(irp, STATUS_INVALID_PARAMETER, 0);
        version = *(const ULONG *)value;
        flags = *(const ULONG *)(value + 4);
        extra = *(const ULONG *)(value + 12);
        if (extra)
            return complete_irp(irp, STATUS_INVALID_PARAMETER, 0);
    } else {
        if (value_length < 24)
            return complete_irp(irp, STATUS_INVALID_PARAMETER, 0);
        version = *(const ULONG *)value;
        flags = *(const ULONG *)(value + 4);
        extra = *(const ULONG *)(value + 16);
        if (value_length - 24 != extra)
            return complete_irp(irp, STATUS_INVALID_PARAMETER, 0);
    }
    HTTP_OPEN *o = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*o), KI_TAG('H', 't', 'O', 'p'));
    if (!o)
        return complete_irp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    memset(o, 0, sizeof(*o));
    InitializeListHead(&o->Sessions);
    o->Wow = wow;
    o->VersionMajor = (USHORT)version;
    o->VersionMinor = (USHORT)(version >> 16);
    o->ProcessId = (ULONG)(ULONG_PTR)PsGetCurrentProcessId();
    NTSTATUS status = STATUS_SUCCESS;
    HttpLock();
    if (device == HttpDeviceChannel) {
        if (o->VersionMajor != 2 || o->VersionMinor != 0 || (stack->Parameters.Create.Options >> 24) != FILE_CREATE ||
            file->FileName.Length) {
            status = o->VersionMajor == 2 && o->VersionMinor == 0 ? STATUS_INVALID_PARAMETER : STATUS_NOT_SUPPORTED;
        } else {
            o->Kind = HTTP_OPEN_CHANNEL;
        }
    } else {
        ULONG disposition = stack->Parameters.Create.Options >> 24;
        ULONG type_bits = flags & 0xE;
        ULONG type = !type_bits ? 1 : type_bits;
        PCWSTR name = nullptr;
        ULONG chars = 0;
        if (file->FileName.Length > sizeof(WCHAR)) {
            name = file->FileName.Buffer + 1;
            chars = file->FileName.Length / sizeof(WCHAR) - 1;
        } else if (file->FileName.Length) {
            status = STATUS_INVALID_PARAMETER;
        }
        if (type_bits & (type_bits - 1))
            status = STATUS_INVALID_PARAMETER;
        if (NT_SUCCESS(status))
            status = valid_version(version);
        if (NT_SUCCESS(status) && name && !valid_queue_name(name, chars))
            status = STATUS_INVALID_PARAMETER;
        o->Kind = HTTP_OPEN_QUEUE;
        o->QueueType = type;
        if (NT_SUCCESS(status) && disposition == FILE_CREATE) {
            HTTP_QUEUE *queue = nullptr;
            if ((type != 1 && type != 2) || (!name && type == 2))
                status = STATUS_INVALID_PARAMETER;
            else if (name && find_queue(name, chars))
                status = STATUS_OBJECT_NAME_COLLISION;
            else if (!(queue = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*queue), KI_TAG('H', 't', 'Q', 'u'))))
                status = STATUS_INSUFFICIENT_RESOURCES;
            if (queue) {
                memset(queue, 0, sizeof(*queue));
                InitializeListHead(&queue->Pending);
                queue->VersionMajor = o->VersionMajor;
                queue->VersionMinor = o->VersionMinor;
                queue->MaxLength = 1000;
                if (name) {
                    queue->Name = ExAllocatePool2(POOL_FLAG_NON_PAGED, (chars + 1) * sizeof(WCHAR), KI_TAG('H', 't', 'Q', 'n'));
                    if (!queue->Name) {
                        ExFreePool(queue);
                        queue = nullptr;
                        status = STATUS_INSUFFICIENT_RESOURCES;
                    } else {
                        memcpy(queue->Name, name, chars * sizeof(WCHAR));
                        queue->Name[chars] = 0;
                        queue->NameLength = (USHORT)(chars * sizeof(WCHAR));
                    }
                }
            }
            if (queue) {
                /* a queue of the first version has a session and a group of its own. */
                if (queue->VersionMajor == 1) {
                    HTTP_SESSION *s = new_session(nullptr, 1, 0);
                    HTTP_GROUP *g = s ? new_group(s) : nullptr;
                    if (!g) {
                        if (s)
                            close_session(s);
                        if (queue->Name)
                            ExFreePool(queue->Name);
                        ExFreePool(queue);
                        queue = nullptr;
                        status = STATUS_INSUFFICIENT_RESOURCES;
                    } else {
                        s->Auto = TRUE;
                        queue->AutoSession = s;
                        queue->AutoGroup = g;
                        bind_group(g, queue);
                    }
                }
            }
            if (queue) {
                InsertTailList(&HttpQueues, &queue->Links);
                o->Queue = queue;
            }
        } else if (NT_SUCCESS(status) && disposition == FILE_OPEN) {
            HTTP_QUEUE *queue = name ? find_queue(name, chars) : nullptr;
            if (!name)
                status = STATUS_INVALID_PARAMETER;
            else if (!queue)
                status = STATUS_OBJECT_NAME_NOT_FOUND;
            else if (type != 1 && type != 4 && type != 8)
                status = STATUS_INVALID_PARAMETER;
            else if (queue->VersionMajor != o->VersionMajor || queue->VersionMinor != o->VersionMinor)
                status = STATUS_NOT_SUPPORTED;
            else
                o->Queue = queue;
        } else if (NT_SUCCESS(status)) {
            status = STATUS_INVALID_PARAMETER;
        }
        if (NT_SUCCESS(status))
            o->Queue->Handles++;
    }
    if (NT_SUCCESS(status)) {
        InsertTailList(&g_opens, &o->Links);
        file->FsContext = o;
    }
    HttpUnlock();
    if (!NT_SUCCESS(status))
        ExFreePool(o);
    return complete_irp(irp, status, 0);
}

/* the last handle is gone. */
static NTSTATUS NTAPI http_cleanup(PDEVICE_OBJECT device, PIRP irp)
{
    PFILE_OBJECT file = IoGetCurrentIrpStackLocation(irp)->FileObject;
    HTTP_OPEN *o = file->FsContext;
    if (o) {
        HttpLock();
        o->Closed = TRUE;
        PLIST_ENTRY e = HttpWaits.Flink;
        while (e != &HttpWaits) {
            HTTP_WAIT *w = CONTAINING_RECORD(e, HTTP_WAIT, Links);
            e = e->Flink;
            if (w->R->File == file && w->Kind != HTTP_WAIT_SEND)
                HttpFinishWait(w, STATUS_CANCELLED, 0);
        }
        if (o->Kind == HTTP_OPEN_CHANNEL) {
            while (!IsListEmpty(&o->Sessions))
                close_session(CONTAINING_RECORD(o->Sessions.Flink, HTTP_SESSION, OwnerLinks));
        } else if (o->Queue && --o->Queue->Handles == 0) {
            o->Queue->Closed = TRUE;
            free_queue(o->Queue);
        }
        RemoveEntryList(&o->Links);
        HttpUnlock();
    }
    return complete_irp(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS NTAPI http_close(PDEVICE_OBJECT device, PIRP irp)
{
    PFILE_OBJECT file = IoGetCurrentIrpStackLocation(irp)->FileObject;
    if (file->FsContext) {
        ExFreePool(file->FsContext);
        file->FsContext = nullptr;
    }
    return complete_irp(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS NTAPI http_unsupported_open(PDEVICE_OBJECT device, PIRP irp)
{
    return complete_irp(irp, STATUS_NOT_SUPPORTED, 0);
}

static NTSTATUS NTAPI http_create_dispatch(PDEVICE_OBJECT device, PIRP irp)
{
    if (device == HttpDeviceChannel || device == HttpDeviceQueue)
        return http_create(device, irp);
    return http_unsupported_open(device, irp);
}

static NTSTATUS NTAPI HttpDriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    driver->MajorFunction[IRP_MJ_CREATE] = http_create_dispatch;
    driver->MajorFunction[IRP_MJ_CLEANUP] = http_cleanup;
    driver->MajorFunction[IRP_MJ_CLOSE] = http_close;
    ObCreateDirectory(L"\\Device\\Http", nullptr);
    UNICODE_STRING name = RTL_CONSTANT_STRING(HTTP_DEVICE_COMMUNICATION);
    NTSTATUS status = IoCreateDevice(driver, 0, &name, FILE_DEVICE_NETWORK, 0, FALSE, &HttpDeviceChannel);
    if (!NT_SUCCESS(status))
        return status;
    UNICODE_STRING queue_name = RTL_CONSTANT_STRING(HTTP_DEVICE_REQUEST_QUEUE);
    status = IoCreateDevice(driver, 0, &queue_name, FILE_DEVICE_NETWORK, 0, FALSE, &HttpDeviceQueue);
    if (!NT_SUCCESS(status))
        return status;
    UNICODE_STRING session_name = RTL_CONSTANT_STRING(HTTP_DEVICE_CLIENT_SESSION);
    PDEVICE_OBJECT session;
    return IoCreateDevice(driver, 0, &session_name, FILE_DEVICE_NETWORK, 0, FALSE, &session);
}

void IopInitializeHttp(void)
{
    KeInitializeMutex(&g_lock, 0);
    InitializeListHead(&HttpUrls);
    InitializeListHead(&HttpQueues);
    InitializeListHead(&HttpSessions);
    InitializeListHead(&HttpConns);
    InitializeListHead(&HttpEndpoints);
    InitializeListHead(&HttpWaits);
    InitializeListHead(&g_opens);
    /* the reservation nobody gave a prefix: the system and the administrators. */
    static const SD_ACE aces[] = {{0, 1, SeSidSystem}, {0, 1, SeSidAdministrators}};
    if (!NT_SUCCESS(SeSdBuild(SeSidSystem, SeSidSystem, 0, aces, RTL_NUMBER_OF(aces), &g_urlacl)))
        g_urlacl = nullptr;
    PDRIVER_OBJECT driver;
    NTSTATUS status = IoCreateDriver(L"\\Driver\\HTTP", HttpDriverEntry, &driver);
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("http failed to start: %08x", status);
        return;
    }
    g_driver = driver;
    HttpNetInitialize();
}
