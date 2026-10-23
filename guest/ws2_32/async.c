/*
 * Winsock 1.1 asynchronous database lookups share one worker thread per process. completion posts the task handle in
 * wParam, result size in lParam's low word and error in its high word. WSAENOBUFS reports the required size.
 */
#include "ws2_32_private.h"

enum { ASYNC_HOST_ADDR = 1, ASYNC_HOST_NAME, ASYNC_PROTO_NAME, ASYNC_PROTO_NUMBER, ASYNC_SERV_NAME, ASYNC_SERV_PORT };

typedef struct ASYNC_TASK {
    struct ASYNC_TASK *next;
    HANDLE handle;
    int kind;
    HWND hwnd;
    UINT message;
    char *buffer;
    int length;
    int number; /* the type, protocol number or port. */
    int count;  /* bytes in key. */
    char *proto;
    BYTE key[1]; /* the name or the address, then the protocol name. */
} ASYNC_TASK;

typedef BOOL(WINAPI *POST_ROUTINE)(HWND, UINT, WPARAM, LPARAM);

static CRITICAL_SECTION g_lock;
static LONG g_lock_ready;
static ASYNC_TASK *g_head, *g_tail, *g_current;
static HANDLE g_cancelled;
static HANDLE g_event;
static HANDLE g_thread;
static ULONG_PTR g_next_handle = 0x101;
static POST_ROUTINE g_post;

static void lock(void)
{
    /* initializes the lock once, including concurrent first calls. */
    if (!g_lock_ready) {
        static LONG volatile making;
        if (InterlockedCompareExchange(&making, 1, 0) == 0) {
            InitializeCriticalSection(&g_lock);
            g_lock_ready = 1;
        } else {
            while (!g_lock_ready)
                Sleep(0);
        }
    }
    EnterCriticalSection(&g_lock);
}

static void unlock(void)
{
    LeaveCriticalSection(&g_lock);
}

/* ------------------------------------------------------------------ copying the answers */

static SIZE_T text_size(const char *s)
{
    return strlen(s) + 1;
}

static SIZE_T align8(SIZE_T n)
{
    return (n + 7) & ~(SIZE_T)7;
}

/*
 * packs the structure, pointer arrays, strings and addresses into one buffer with internal pointers. returns the
 * required size and writes only if the buffer fits.
 */
static SIZE_T flatten_host(const struct hostent *h, char *buffer, int length)
{
    int aliases = 0, addresses = 0;
    while (h->h_aliases && h->h_aliases[aliases])
        aliases++;
    while (h->h_addr_list && h->h_addr_list[addresses])
        addresses++;
    SIZE_T size = sizeof(*h) + (aliases + 1) * sizeof(char *) + (addresses + 1) * sizeof(char *) +
                  text_size(h->h_name) + (SIZE_T)addresses * h->h_length;
    for (int i = 0; i < aliases; i++)
        size += text_size(h->h_aliases[i]);
    size = align8(size);
    if (!buffer || (SIZE_T)length < size)
        return size;
    memset(buffer, 0, size);
    struct hostent *out = (struct hostent *)buffer;
    char **alias_array = (char **)(buffer + sizeof(*out));
    char **address_array = alias_array + aliases + 1;
    char *at = (char *)(address_array + addresses + 1);
    out->h_aliases = alias_array;
    out->h_addr_list = address_array;
    out->h_addrtype = h->h_addrtype;
    out->h_length = h->h_length;
    for (int i = 0; i < addresses; i++) {
        address_array[i] = at;
        memcpy(at, h->h_addr_list[i], h->h_length);
        at += h->h_length;
    }
    out->h_name = at;
    memcpy(at, h->h_name, text_size(h->h_name));
    at += text_size(h->h_name);
    for (int i = 0; i < aliases; i++) {
        alias_array[i] = at;
        memcpy(at, h->h_aliases[i], text_size(h->h_aliases[i]));
        at += text_size(h->h_aliases[i]);
    }
    return size;
}

static SIZE_T flatten_serv(const struct servent *s, char *buffer, int length)
{
    int aliases = 0;
    while (s->s_aliases && s->s_aliases[aliases])
        aliases++;
    SIZE_T size = sizeof(*s) + (aliases + 1) * sizeof(char *) + text_size(s->s_name) + text_size(s->s_proto);
    for (int i = 0; i < aliases; i++)
        size += text_size(s->s_aliases[i]);
    size = align8(size);
    if (!buffer || (SIZE_T)length < size)
        return size;
    memset(buffer, 0, size);
    struct servent *out = (struct servent *)buffer;
    char **alias_array = (char **)(buffer + sizeof(*out));
    char *at = (char *)(alias_array + aliases + 1);
    out->s_aliases = alias_array;
    out->s_port = s->s_port;
    out->s_name = at;
    memcpy(at, s->s_name, text_size(s->s_name));
    at += text_size(s->s_name);
    out->s_proto = at;
    memcpy(at, s->s_proto, text_size(s->s_proto));
    at += text_size(s->s_proto);
    for (int i = 0; i < aliases; i++) {
        alias_array[i] = at;
        memcpy(at, s->s_aliases[i], text_size(s->s_aliases[i]));
        at += text_size(s->s_aliases[i]);
    }
    return size;
}

static SIZE_T flatten_proto(const struct protoent *p, char *buffer, int length)
{
    int aliases = 0;
    while (p->p_aliases && p->p_aliases[aliases])
        aliases++;
    SIZE_T size = sizeof(*p) + (aliases + 1) * sizeof(char *) + text_size(p->p_name);
    for (int i = 0; i < aliases; i++)
        size += text_size(p->p_aliases[i]);
    size = align8(size);
    if (!buffer || (SIZE_T)length < size)
        return size;
    memset(buffer, 0, size);
    struct protoent *out = (struct protoent *)buffer;
    char **alias_array = (char **)(buffer + sizeof(*out));
    char *at = (char *)(alias_array + aliases + 1);
    out->p_aliases = alias_array;
    out->p_proto = p->p_proto;
    out->p_name = at;
    memcpy(at, p->p_name, text_size(p->p_name));
    at += text_size(p->p_name);
    for (int i = 0; i < aliases; i++) {
        alias_array[i] = at;
        memcpy(at, p->p_aliases[i], text_size(p->p_aliases[i]));
        at += text_size(p->p_aliases[i]);
    }
    return size;
}

/* ------------------------------------------------------------------ the thread */

static void post_answer(ASYNC_TASK *task, SIZE_T size, int error)
{
    if (size > 0xFFFF)
        error = WSAEINVAL;
    LPARAM reply = WSAMAKEASYNCREPLY((WORD)size, (WORD)error);
    POST_ROUTINE post = g_post;
    if (post)
        post(task->hwnd, task->message, (WPARAM)task->handle, reply);
    else
        PostMessageW(task->hwnd, task->message, (WPARAM)task->handle, reply);
}

static void run_task(ASYNC_TASK *task)
{
    SIZE_T size = 0;
    int error = 0;
    char *copy = task->buffer;
    int room = task->length;
    /* the answer is built before the request is known to be cancelled, and dropped then. */
    switch (task->kind) {
    case ASYNC_HOST_ADDR:
    case ASYNC_HOST_NAME: {
        struct hostent *h = task->kind == ASYNC_HOST_ADDR ? gethostbyaddr((char *)task->key, task->count, task->number)
                                                          : gethostbyname((char *)task->key);
        if (!h)
            error = WSAGetLastError();
        else
            size = flatten_host(h, copy, room);
        break;
    }
    case ASYNC_PROTO_NAME:
    case ASYNC_PROTO_NUMBER: {
        struct protoent *p =
            task->kind == ASYNC_PROTO_NAME ? getprotobyname((char *)task->key) : getprotobynumber(task->number);
        if (!p)
            error = WSAGetLastError();
        else
            size = flatten_proto(p, copy, room);
        break;
    }
    default: {
        struct servent *s = task->kind == ASYNC_SERV_NAME ? getservbyname((char *)task->key, task->proto)
                                                          : getservbyport(task->number, task->proto);
        if (!s)
            error = WSAGetLastError();
        else
            size = flatten_serv(s, copy, room);
        break;
    }
    }
    if (!error && size > (SIZE_T)room)
        error = WSAENOBUFS;
    lock();
    BOOL cancelled = task->handle == g_cancelled;
    g_current = nullptr;
    unlock();
    if (!cancelled)
        post_answer(task, size, error);
}

static DWORD WINAPI async_thread(LPVOID parameter)
{
    WSADATA data;
    WSAStartup(0x0202, &data);
    for (;;) {
        WaitForSingleObject(g_event, INFINITE);
        for (;;) {
            lock();
            ASYNC_TASK *task = g_head;
            if (task) {
                g_head = task->next;
                if (!g_head)
                    g_tail = nullptr;
                g_current = task;
            }
            unlock();
            if (!task)
                break;
            run_task(task);
            WsFree(task);
        }
    }
}

/* the thread is started with the first request. */
static BOOL ensure_thread(void)
{
    lock();
    if (!g_thread) {
        g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (g_event)
            g_thread = CreateThread(nullptr, 0, async_thread, nullptr, 0, nullptr);
        if (!g_thread && g_event) {
            CloseHandle(g_event);
            g_event = nullptr;
        }
    }
    BOOL ok = g_thread != nullptr;
    unlock();
    return ok;
}

static HANDLE queue_task(ASYNC_TASK *task)
{
    lock();
    task->handle = (HANDLE)g_next_handle++;
    if (g_tail)
        g_tail->next = task;
    else
        g_head = task;
    g_tail = task;
    HANDLE handle = task->handle;
    unlock();
    SetEvent(g_event);
    return handle;
}

/* copies the request key and protocol name after the task record. */
static ASYNC_TASK *make_task(int kind, HWND hwnd, UINT message, const void *key, int key_size, const char *proto,
                             char *buffer, int length, int number)
{
    SIZE_T proto_size = proto ? strlen(proto) + 1 : 0;
    ASYNC_TASK *task = WsAllocZero(sizeof(*task) + key_size + 1 + proto_size);
    if (!task)
        return nullptr;
    task->kind = kind;
    task->hwnd = hwnd;
    task->message = message;
    task->buffer = buffer;
    task->length = length;
    task->number = number;
    task->count = key_size;
    if (key_size)
        memcpy(task->key, key, key_size);
    if (proto) {
        task->proto = (char *)task->key + key_size + 1;
        memcpy(task->proto, proto, proto_size);
    }
    return task;
}

static HANDLE submit(int kind, HWND hwnd, UINT message, const void *key, int key_size, const char *proto, char *buffer,
                     int length, int number)
{
    int error = WsCheckStarted();
    if (error) {
        SetLastError(error);
        return nullptr;
    }
    if (!ensure_thread()) {
        SetLastError(WSAENOBUFS);
        return nullptr;
    }
    ASYNC_TASK *task = make_task(kind, hwnd, message, key, key_size, proto, buffer, length, number);
    if (!task) {
        SetLastError(WSAEFAULT);
        return nullptr;
    }
    return queue_task(task);
}

HANDLE WSAAPI WSAAsyncGetHostByName(HWND hwnd, u_int message, const char *name, char *buffer, int length)
{
    return submit(ASYNC_HOST_NAME, hwnd, message, name, (int)strlen(name) + 1, nullptr, buffer, length, 0);
}

HANDLE WSAAPI WSAAsyncGetHostByAddr(HWND hwnd, u_int message, const char *addr, int len, int type, char *buffer,
                                    int length)
{
    return submit(ASYNC_HOST_ADDR, hwnd, message, addr, len, nullptr, buffer, length, type);
}

HANDLE WSAAPI WSAAsyncGetProtoByName(HWND hwnd, u_int message, const char *name, char *buffer, int length)
{
    return submit(ASYNC_PROTO_NAME, hwnd, message, name, (int)strlen(name) + 1, nullptr, buffer, length, 0);
}

HANDLE WSAAPI WSAAsyncGetProtoByNumber(HWND hwnd, u_int message, int number, char *buffer, int length)
{
    return submit(ASYNC_PROTO_NUMBER, hwnd, message, nullptr, 0, nullptr, buffer, length, number);
}

HANDLE WSAAPI WSAAsyncGetServByName(HWND hwnd, u_int message, const char *name, const char *proto, char *buffer,
                                    int length)
{
    return submit(ASYNC_SERV_NAME, hwnd, message, name, (int)strlen(name) + 1, proto, buffer, length, 0);
}

HANDLE WSAAPI WSAAsyncGetServByPort(HWND hwnd, u_int message, int port, const char *proto, char *buffer, int length)
{
    return submit(ASYNC_SERV_PORT, hwnd, message, nullptr, 0, proto, buffer, length, port);
}

/* drops queued requests or suppresses active requests' completion messages. other handles fail with WSAEINVAL. */
int WSAAPI WSACancelAsyncRequest(HANDLE task)
{
    int error = WsCheckStarted();
    if (error)
        return WsFail(error);
    if (!g_thread)
        return WsFail(WSAEINVAL);
    lock();
    if (g_current && g_current->handle == task) {
        g_cancelled = task;
        unlock();
        return 0;
    }
    ASYNC_TASK **at = &g_head, *previous = nullptr;
    while (*at && (*at)->handle != task) {
        previous = *at;
        at = &(*at)->next;
    }
    ASYNC_TASK *found = *at;
    if (found) {
        *at = found->next;
        if (g_tail == found)
            g_tail = previous;
    }
    unlock();
    if (!found)
        return WsFail(WSAEINVAL);
    WsFree(found);
    return 0;
}

/* allows a replacement for PostMessage with the same signature. */
int WSAAPI WSApSetPostRoutine(PVOID routine)
{
    lock();
    g_post = (POST_ROUTINE)routine;
    unlock();
    return 0;
}

/* legacy 16-bit Winsock unload entry point. */
int WINAPI WEP(void)
{
    return 1;
}
