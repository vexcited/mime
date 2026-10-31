/*
 * the lookups of the dns client: the servers of the adapters, the messages sent to them over udp and tcp, the cache of
 * the answers, the hosts file and the names the machine answers for itself.
 */
#include "dns_private.h"
#include <mime/win32/winsock2.h>
#include <mime/win32/ws2tcpip.h>
#include <mime/win32/iphlpapi.h>

#define MAX_SUFFIXES 8
#define CACHE_ENTRIES 4096
#define MAX_CACHE_TTL 86400
#define MAX_NEGATIVE_TTL 5
#define HOSTS_TTL 604800
#define LOCAL_TTL 1200
#define LITERAL_TTL 604800
#define MESSAGE_SIZE 65535

/* ------------------------------------------------------------------ winsock */

static SRWLOCK startup_lock = SRWLOCK_INIT;
static BOOL started;

static BOOL start_winsock(void)
{
    AcquireSRWLockExclusive(&startup_lock);
    if (!started) {
        WSADATA data;
        started = !WSAStartup(MAKEWORD(2, 2), &data);
    }
    BOOL ok = started;
    ReleaseSRWLockExclusive(&startup_lock);
    return ok;
}

/* ------------------------------------------------------------------ the configuration */

typedef struct {
    DNS_SERVER servers[DNS_MAX_SERVERS];
    int server_count;
    char suffixes[MAX_SUFFIXES][256];
    int suffix_count;
} DNS_CONFIG;

static void add_suffix(DNS_CONFIG *c, const char *suffix)
{
    while (*suffix == '.')
        suffix++;
    if (!*suffix || strlen(suffix) >= sizeof(c->suffixes[0]) || c->suffix_count >= MAX_SUFFIXES)
        return;
    for (int i = 0; i < c->suffix_count; i++)
        if (DnsNameEqual(c->suffixes[i], suffix))
            return;
    strcpy(c->suffixes[c->suffix_count++], suffix);
}

static void add_suffix_wide(DNS_CONFIG *c, const WCHAR *suffix)
{
    char text[256];
    if (suffix && *suffix && WideCharToMultiByte(CP_UTF8, 0, suffix, -1, text, sizeof(text), nullptr, nullptr) > 0)
        add_suffix(c, text);
}

static BOOL usable_server(const SOCKADDR *a)
{
    if (a->sa_family == AF_INET) {
        const BYTE *b = (const BYTE *)&((const SOCKADDR_IN *)a)->sin_addr;
        return b[0] | b[1] | b[2] | b[3];
    }
    if (a->sa_family == AF_INET6) {
        const BYTE *b = ((const SOCKADDR_IN6 *)a)->sin6_addr.u.Byte;
        /* the site local addresses windows lists when it has no ipv6 server are not servers. */
        if (b[0] == 0xFE && b[1] == 0xC0 && !b[2] && !b[3] && b[4] == 0 && b[5] == 0 && b[6] == 0xFF && b[7] == 0xFF)
            return FALSE;
        for (int i = 0; i < 16; i++)
            if (b[i])
                return TRUE;
    }
    return FALSE;
}

static void add_server(DNS_CONFIG *c, const SOCKADDR *a, int length)
{
    if (c->server_count >= DNS_MAX_SERVERS || length <= 0 || length > (int)sizeof(((DNS_SERVER *)0)->address) || !usable_server(a))
        return;
    for (int i = 0; i < c->server_count; i++) {
        const SOCKADDR *e = (const SOCKADDR *)c->servers[i].address;
        if (e->sa_family != a->sa_family)
            continue;
        if (a->sa_family == AF_INET &&
            ((const SOCKADDR_IN *)e)->sin_addr.S_un.S_addr == ((const SOCKADDR_IN *)a)->sin_addr.S_un.S_addr)
            return;
        if (a->sa_family == AF_INET6 &&
            !memcmp(&((const SOCKADDR_IN6 *)e)->sin6_addr, &((const SOCKADDR_IN6 *)a)->sin6_addr, 16))
            return;
    }
    DNS_SERVER *s = &c->servers[c->server_count++];
    memset(s->address, 0, sizeof(s->address));
    memcpy(s->address, a, (SIZE_T)length);
    s->length = length;
    if (a->sa_family == AF_INET)
        ((SOCKADDR_IN *)s->address)->sin_port = htons(53);
    else
        ((SOCKADDR_IN6 *)s->address)->sin6_port = htons(53);
}

typedef ULONG(WINAPI *GET_ADAPTERS_ADDRESSES)(ULONG, ULONG, PVOID, PIP_ADAPTER_ADDRESSES, PULONG);
typedef DWORD(WINAPI *GET_NETWORK_PARAMS)(PFIXED_INFO, PULONG);

/* the servers and suffixes the way the adapters present them. */
static void load_config(DNS_CONFIG *c)
{
    memset(c, 0, sizeof(*c));
    HMODULE iphlpapi = LoadLibraryW(L"iphlpapi.dll");
    if (!iphlpapi)
        return;
    GET_ADAPTERS_ADDRESSES adapters = (GET_ADAPTERS_ADDRESSES)GetProcAddress(iphlpapi, "GetAdaptersAddresses");
    ULONG size = 0;
    ULONG flags = GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST;
    PIP_ADAPTER_ADDRESSES list = nullptr;
    if (adapters && adapters(AF_UNSPEC, flags, nullptr, nullptr, &size) == ERROR_BUFFER_OVERFLOW) {
        list = DnsAllocate(size);
        if (list && adapters(AF_UNSPEC, flags, nullptr, list, &size)) {
            DnsRelease(list);
            list = nullptr;
        }
    }
    for (PIP_ADAPTER_ADDRESSES a = list; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp)
            continue;
        for (PIP_ADAPTER_DNS_SERVER_ADDRESS s = a->FirstDnsServerAddress; s; s = s->Next)
            if (s->Address.lpSockaddr)
                add_server(c, s->Address.lpSockaddr, s->Address.iSockaddrLength);
    }
    if (!c->server_count) {
        GET_NETWORK_PARAMS params = (GET_NETWORK_PARAMS)GetProcAddress(iphlpapi, "GetNetworkParams");
        ULONG n = 0;
        PFIXED_INFO info = nullptr;
        if (params && params(nullptr, &n) == ERROR_BUFFER_OVERFLOW && (info = DnsAllocate(n)) && !params(info, &n)) {
            for (IP_ADDR_STRING *e = &info->DnsServerList; e; e = e->Next) {
                SOCKADDR_IN in = {.sin_family = AF_INET};
                if (inet_pton(AF_INET, e->IpAddress.String, &in.sin_addr) == 1)
                    add_server(c, (SOCKADDR *)&in, sizeof(in));
            }
        }
        DnsRelease(info);
    }
    char domain[256];
    DWORD n = sizeof(domain);
    if (GetComputerNameExA(ComputerNameDnsDomain, domain, &n) && n)
        add_suffix(c, domain);
    for (PIP_ADAPTER_ADDRESSES a = list; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp)
            continue;
        add_suffix_wide(c, a->DnsSuffix);
        for (PIP_ADAPTER_DNS_SUFFIX s = a->FirstDnsSuffix; s; s = s->Next)
            add_suffix_wide(c, s->String);
    }
    DnsRelease(list);
    FreeLibrary(iphlpapi);
}

/* ------------------------------------------------------------------ the wire */

typedef enum { EXCHANGE_ANSWER, EXCHANGE_TIMEOUT, EXCHANGE_FAILED, EXCHANGE_CANCELLED } EXCHANGE;

static const DWORD retry_timeouts[] = {1000, 1000, 2000, 4000, 4000};

static WORD random_id(void)
{
    static ULONG seed;
    static SRWLOCK lock = SRWLOCK_INIT;
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    AcquireSRWLockExclusive(&lock);
    if (!seed)
        seed = (ULONG)counter.QuadPart ^ GetCurrentProcessId() * 2654435761u;
    seed ^= (ULONG)counter.QuadPart;
    WORD id = (WORD)(RtlRandomEx(&seed) >> 8);
    ReleaseSRWLockExclusive(&lock);
    return id;
}

/* waits for a socket in slices so that a cancelled query leaves soon: 1 ready, 0 time is up, -1 cancelled. */
static int wait_socket(SOCKET s, BOOL for_write, ULONGLONG until, volatile LONG *cancelled)
{
    for (;;) {
        if (cancelled && *cancelled)
            return -1;
        ULONGLONG now = GetTickCount64();
        if (now >= until)
            return 0;
        ULONGLONG left = until - now;
        if (left > 50)
            left = 50;
        fd_set set;
        FD_ZERO(&set);
        FD_SET(s, &set);
        TIMEVAL tv = {0, (long)left * 1000};
        int r = for_write ? select(0, nullptr, &set, nullptr, &tv) : select(0, &set, nullptr, nullptr, &tv);
        if (r > 0)
            return 1;
        if (r < 0)
            return 0;
    }
}

static EXCHANGE udp_exchange(const DNS_SERVER *server, const BYTE *query, int query_length, WORD id, const char *name,
                             WORD type, DWORD timeout, volatile LONG *cancelled, BYTE *buffer, DNS_ANSWER *answer,
                             BOOL *truncated, int *message_length)
{
    const SOCKADDR *to = (const SOCKADDR *)server->address;
    SOCKET s = socket(to->sa_family, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return EXCHANGE_FAILED;
    u_long nonblocking = 1;
    ioctlsocket(s, FIONBIO, &nonblocking);
    EXCHANGE result = EXCHANGE_FAILED;
    if (sendto(s, (const char *)query, query_length, 0, to, server->length) == query_length) {
        ULONGLONG until = GetTickCount64() + timeout;
        result = EXCHANGE_TIMEOUT;
        for (;;) {
            int ready = wait_socket(s, FALSE, until, cancelled);
            if (ready < 0) {
                result = EXCHANGE_CANCELLED;
                break;
            }
            if (!ready)
                break;
            int n = recv(s, (char *)buffer, 4096, 0);
            if (n < 0) {
                /* a refused port is reported to the sender of the datagram on some hosts. */
                if (WSAGetLastError() == WSAECONNRESET)
                    continue;
                result = EXCHANGE_FAILED;
                break;
            }
            DNS_ANSWER parsed = {};
            if (DnsWireParse(buffer, n, id, name, type, truncated, &parsed)) {
                *answer = parsed;
                *message_length = n;
                result = EXCHANGE_ANSWER;
                break;
            }
        }
    }
    closesocket(s);
    return result;
}

static BOOL send_all(SOCKET s, const BYTE *data, int length, ULONGLONG until, volatile LONG *cancelled)
{
    while (length > 0) {
        int ready = wait_socket(s, TRUE, until, cancelled);
        if (ready <= 0)
            return FALSE;
        int n = send(s, (const char *)data, length, 0);
        if (n < 0) {
            if (WSAGetLastError() == WSAEWOULDBLOCK)
                continue;
            return FALSE;
        }
        data += n;
        length -= n;
    }
    return TRUE;
}

static BOOL receive_all(SOCKET s, BYTE *data, int length, ULONGLONG until, volatile LONG *cancelled)
{
    while (length > 0) {
        int ready = wait_socket(s, FALSE, until, cancelled);
        if (ready <= 0)
            return FALSE;
        int n = recv(s, (char *)data, length, 0);
        if (n == 0)
            return FALSE;
        if (n < 0) {
            if (WSAGetLastError() == WSAEWOULDBLOCK)
                continue;
            return FALSE;
        }
        data += n;
        length -= n;
    }
    return TRUE;
}

static EXCHANGE tcp_exchange(const DNS_SERVER *server, const BYTE *query, int query_length, WORD id, const char *name,
                             WORD type, DWORD timeout, volatile LONG *cancelled, BYTE *buffer, DNS_ANSWER *answer,
                             BOOL *truncated, int *message_length)
{
    const SOCKADDR *to = (const SOCKADDR *)server->address;
    SOCKET s = socket(to->sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return EXCHANGE_FAILED;
    u_long nonblocking = 1;
    ioctlsocket(s, FIONBIO, &nonblocking);
    EXCHANGE result = EXCHANGE_FAILED;
    ULONGLONG until = GetTickCount64() + timeout;
    if (connect(s, to, server->length) && WSAGetLastError() != WSAEWOULDBLOCK && WSAGetLastError() != WSAEINPROGRESS)
        goto done;
    int ready = wait_socket(s, TRUE, until, cancelled);
    if (ready < 0) {
        result = EXCHANGE_CANCELLED;
        goto done;
    }
    if (ready == 0) {
        result = EXCHANGE_TIMEOUT;
        goto done;
    }
    int failure = 0, size = sizeof(failure);
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&failure, &size) || failure)
        goto done;
    BYTE framed[2 + 512];
    if (query_length > 512)
        goto done;
    framed[0] = (BYTE)(query_length >> 8);
    framed[1] = (BYTE)query_length;
    memcpy(framed + 2, query, (SIZE_T)query_length);
    if (!send_all(s, framed, query_length + 2, until, cancelled))
        goto done;
    for (;;) {
        BYTE prefix[2];
        if (!receive_all(s, prefix, 2, until, cancelled)) {
            result = cancelled && *cancelled ? EXCHANGE_CANCELLED : EXCHANGE_TIMEOUT;
            goto done;
        }
        int n = (prefix[0] << 8) | prefix[1];
        if (n < 12 || n > MESSAGE_SIZE)
            goto done;
        if (!receive_all(s, buffer, n, until, cancelled)) {
            result = cancelled && *cancelled ? EXCHANGE_CANCELLED : EXCHANGE_TIMEOUT;
            goto done;
        }
        DNS_ANSWER parsed = {};
        if (DnsWireParse(buffer, n, id, name, type, truncated, &parsed)) {
            *answer = parsed;
            *message_length = n;
            result = EXCHANGE_ANSWER;
            goto done;
        }
    }
done:
    closesocket(s);
    return result;
}

static void set_message(DNS_ANSWER *answer, const BYTE *message, int length)
{
    DnsRelease(answer->message);
    answer->message = DnsAllocate((SIZE_T)length);
    answer->message_length = answer->message ? (DWORD)length : 0;
    if (answer->message)
        memcpy(answer->message, message, (SIZE_T)length);
}

/* asks the servers for a name, in rounds with the timeouts windows gives them. */
static DNS_STATUS wire_query(const DNS_CONFIG *config, const char *name, WORD type, ULONGLONG options,
                             volatile LONG *cancelled, DNS_ANSWER *answer)
{
    if (!config->server_count || !start_winsock())
        return DNS_ERROR_NO_DNS_SERVERS;
    BYTE *buffer = DnsAllocate(MESSAGE_SIZE);
    if (!buffer)
        return ERROR_NOT_ENOUGH_MEMORY;
    WORD id = random_id();
    BYTE query[512];
    int query_length = DnsWireQuery(query, sizeof(query), id, name, type, !(options & DNS_QUERY_NO_RECURSION));
    if (query_length < 0) {
        DnsRelease(buffer);
        return ERROR_INVALID_NAME;
    }
    BOOL dead[DNS_MAX_SERVERS] = {};
    DNS_STATUS status = ERROR_TIMEOUT;
    BOOL got_failure = FALSE;
    for (int round = 0; round < (int)RTL_NUMBER_OF(retry_timeouts); round++) {
        int tried = 0, silent = 0;
        for (int i = 0; i < config->server_count; i++) {
            /* the first round asks one server, the next rounds all of them. */
            if (dead[i] || (!round && tried))
                continue;
            tried++;
            BOOL truncated = FALSE;
            int length = 0;
            DNS_ANSWER got = {};
            EXCHANGE e;
            if (options & DNS_QUERY_USE_TCP_ONLY)
                e = tcp_exchange(&config->servers[i], query, query_length, id, name, type, 5000, cancelled, buffer,
                                 &got, &truncated, &length);
            else
                e = udp_exchange(&config->servers[i], query, query_length, id, name, type, retry_timeouts[round],
                                 cancelled, buffer, &got, &truncated, &length);
            if (e == EXCHANGE_ANSWER && truncated && !(options & (DNS_QUERY_ACCEPT_TRUNCATED_RESPONSE |
                                                                   DNS_QUERY_USE_TCP_ONLY))) {
                DNS_ANSWER whole = {};
                BOOL again = FALSE;
                int whole_length = 0;
                if (tcp_exchange(&config->servers[i], query, query_length, id, name, type, 5000, cancelled, buffer,
                                 &whole, &again, &whole_length) == EXCHANGE_ANSWER) {
                    DnsRrFree(got.records);
                    got = whole;
                    length = whole_length;
                }
            }
            if (e == EXCHANGE_CANCELLED) {
                DnsRelease(buffer);
                return ERROR_CANCELLED;
            }
            if (e == EXCHANGE_FAILED) {
                dead[i] = TRUE;
                continue;
            }
            if (e == EXCHANGE_TIMEOUT) {
                silent++;
                continue;
            }
            /* a server that fails the query leaves it to the next. */
            if (got.status == DNS_ERROR_RCODE_SERVER_FAILURE || got.status == DNS_ERROR_RCODE_REFUSED ||
                got.status == DNS_ERROR_RCODE_NOT_IMPLEMENTED || got.status == DNS_ERROR_RCODE_FORMAT_ERROR) {
                status = got.status;
                got_failure = TRUE;
                DnsRrFree(got.records);
                continue;
            }
            *answer = got;
            if (options & DNS_QUERY_RETURN_MESSAGE)
                set_message(answer, buffer, length);
            DnsRelease(buffer);
            return got.status;
        }
        if (!tried) {
            DnsRelease(buffer);
            return DNS_ERROR_NO_DNS_SERVERS;
        }
        if (!silent && got_failure)
            break;
    }
    DnsRelease(buffer);
    return status;
}

/* ------------------------------------------------------------------ the cache */

/*
 * the cache of the dns client service. it holds a set of records for a name and a type, and the whole set runs out
 * together when the shortest ttl of the set has: every record of it is then given back with the time that is left.
 * only the types the service passes on are kept, and an answer without records is kept for a moment as well.
 */
typedef struct {
    char *name;
    WORD type;
    DNS_STATUS status;
    ULONGLONG expires;
    DNS_RR *records;
} CACHE_ENTRY;

static SRWLOCK cache_lock = SRWLOCK_INIT;
static CACHE_ENTRY cache[CACHE_ENTRIES];
static int cache_next;

static ULONGLONG seconds(void)
{
    return GetTickCount64() / 1000;
}

static void entry_clear(CACHE_ENTRY *e)
{
    DnsRrFree(e->records);
    DnsRelease(e->name);
    memset(e, 0, sizeof(*e));
}

/* a name that is not there is not there for any type. */
static BOOL entry_matches(const CACHE_ENTRY *e, const char *name, WORD type)
{
    if (!e->name || !DnsNameEqual(e->name, name))
        return FALSE;
    return e->type == type || (e->type == DNS_TYPE_ALL && e->status == DNS_ERROR_RCODE_NAME_ERROR);
}

/* a copy of what the cache holds for a name, every ttl the time that is left. */
static BOOL cache_get(const char *name, WORD type, DNS_ANSWER *answer)
{
    ULONGLONG now = seconds();
    BOOL found = FALSE;
    AcquireSRWLockExclusive(&cache_lock);
    for (int i = 0; i < CACHE_ENTRIES && !found; i++) {
        CACHE_ENTRY *e = &cache[i];
        if (!entry_matches(e, name, type))
            continue;
        if (e->expires <= now) {
            entry_clear(e);
            continue;
        }
        DNS_RR *copy = DnsRrClone(e->records);
        if (e->records && !copy)
            break;
        for (DNS_RR *rr = copy; rr; rr = rr->next)
            rr->ttl = (DWORD)(e->expires - now);
        answer->records = copy;
        answer->status = e->status;
        found = TRUE;
    }
    ReleaseSRWLockExclusive(&cache_lock);
    return found;
}

/* keeps what a lookup found. the answers that say there is nothing are kept for MaxNegativeCacheTtl. */
static void cache_put(const char *name, WORD type, const DNS_ANSWER *answer)
{
    DNS_STATUS status = answer->status;
    DWORD ttl = 0;
    DNS_RR *copy = nullptr;
    if (status == ERROR_SUCCESS) {
        ttl = MAX_CACHE_TTL;
        BOOL asked = FALSE;
        for (DNS_RR *rr = answer->records; rr; rr = rr->next) {
            ttl = rr->ttl < ttl ? rr->ttl : ttl;
            asked |= rr->type == type || type == DNS_TYPE_ALL;
        }
        if (!asked) {
            /* aliases alone are kept only when the zone says there is no more. */
            if (!answer->has_soa)
                return;
            status = DNS_INFO_NO_RECORDS;
            ttl = ttl < MAX_NEGATIVE_TTL ? ttl : MAX_NEGATIVE_TTL;
        } else {
            copy = DnsRrClone(answer->records);
            if (!copy)
                return;
            for (DNS_RR *rr = copy; rr; rr = rr->next)
                rr->ttl = ttl;
        }
    } else if (status == DNS_ERROR_RCODE_SERVER_FAILURE || status == DNS_ERROR_RCODE_NAME_ERROR ||
               status == DNS_INFO_NO_RECORDS || status == DNS_ERROR_RECORD_DOES_NOT_EXIST) {
        ttl = MAX_NEGATIVE_TTL;
    } else {
        return;
    }
    if (status == DNS_ERROR_RCODE_NAME_ERROR)
        type = DNS_TYPE_ALL;
    char *key = ttl ? DnsDuplicate(name) : nullptr;
    if (!key) {
        DnsRrFree(copy);
        return;
    }
    ULONGLONG now = seconds();
    AcquireSRWLockExclusive(&cache_lock);
    CACHE_ENTRY *slot = nullptr;
    for (int i = 0; i < CACHE_ENTRIES; i++) {
        CACHE_ENTRY *e = &cache[i];
        if (e->name && e->type == type && DnsNameEqual(e->name, name)) {
            slot = e;
            break;
        }
        if (!slot && (!e->name || e->expires <= now))
            slot = e;
    }
    if (!slot) {
        slot = &cache[cache_next];
        cache_next = (cache_next + 1) % CACHE_ENTRIES;
    }
    entry_clear(slot);
    slot->name = key;
    slot->type = type;
    slot->status = status;
    slot->expires = now + ttl;
    slot->records = copy;
    ReleaseSRWLockExclusive(&cache_lock);
}

/* forgets one name, or every name. */
void DnsCacheFlush(const char *name)
{
    AcquireSRWLockExclusive(&cache_lock);
    for (int i = 0; i < CACHE_ENTRIES; i++)
        if (cache[i].name && (!name || DnsNameEqual(cache[i].name, name)))
            entry_clear(&cache[i]);
    ReleaseSRWLockExclusive(&cache_lock);
}

/* ------------------------------------------------------------------ the hosts file */

static char *read_hosts(void)
{
    WCHAR path[MAX_PATH + 32];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return nullptr;
    wcscpy(path + n, L"\\drivers\\etc\\hosts");
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return nullptr;
    DWORD size = GetFileSize(file, nullptr), got = 0;
    char *text = size && size < (1u << 20) ? DnsAllocate((SIZE_T)size + 1) : nullptr;
    if (text && (!ReadFile(file, text, size, &got, nullptr) || got != size)) {
        DnsRelease(text);
        text = nullptr;
    }
    CloseHandle(file);
    return text;
}

static BOOL space(char c)
{
    return c == ' ' || c == '\t';
}

/* the reverse name of an address, as the hosts file lists it. */
static void reverse_name(const BYTE *ip, BOOL v6, char *out)
{
    if (!v6) {
        _snprintf(out, 80, "%u.%u.%u.%u.in-addr.arpa", ip[3], ip[2], ip[1], ip[0]);
        return;
    }
    char *p = out;
    for (int i = 15; i >= 0; i--)
        p += _snprintf(p, 8, "%x.%x.", ip[i] & 15, ip[i] >> 4);
    strcpy(p, "ip6.arpa");
}

static void rr_append(DNS_RR ***tail, DNS_RR *rr)
{
    **tail = rr;
    *tail = &rr->next;
}

/* names of microsoft that a hosts file cannot take over: the service ignores the entries for them. */
static BOOL screened_name(const char *name)
{
    static const char *const domains[] = {"windowsupdate.microsoft.com", "windowsupdate.com", "microsoftupdate.com",
                                          "download.microsoft.com", "update.microsoft.com"};
    static const char *const hosts[] = {"microsoft.com", "www.microsoft.com", "support.microsoft.com",
                                        "wustats.microsoft.com", "download.microsoft.com",
                                        "microsoftupdate.microsoft.com", "office.microsoft.com",
                                        "msdn.microsoft.com", "go.microsoft.com", "msn.com", "www.msn.com",
                                        "msdn.com", "www.msdn.com", "technet.microsoft.com", "technet.com",
                                        "www.technet.com", "activation-v2.sls.microsoft.com",
                                        "validation-v2.sls.microsoft.com"};
    SIZE_T n = strlen(name);
    if (n && name[n - 1] == '.')
        n--;
    for (int i = 0; i < (int)RTL_NUMBER_OF(domains); i++) {
        SIZE_T d = strlen(domains[i]);
        if (n == d && !_strnicmp(name, domains[i], d))
            return TRUE;
        if (n > d && name[n - d - 1] == '.' && !_strnicmp(name + n - d, domains[i], d))
            return TRUE;
    }
    for (int i = 0; i < (int)RTL_NUMBER_OF(hosts); i++)
        if (n == strlen(hosts[i]) && !_strnicmp(name, hosts[i], n))
            return TRUE;
    return FALSE;
}

static DNS_RR *address_record(const char *name, WORD type, DWORD ttl, const BYTE *ip)
{
    DNS_RR *rr = DnsRrNew(name, type, ttl, DnsSectionAnswer);
    if (rr)
        memcpy(rr->ip, ip, type == DNS_TYPE_A ? 4 : 16);
    return rr;
}

/*
 * what the hosts file says about a name, the way the service loads it: the first name of a line is the host, the
 * ones after it its aliases, and the host has a record for the address of the line, which never runs out. an alias
 * is a cname to the host. the address has a ptr record. a host that has an address of one family only is known not
 * to have one of the other. the result is the status of the answer, or -1 when the file does not know the name.
 */
static LONG hosts_lookup(const char *name, WORD type, DNS_RR **records)
{
    *records = nullptr;
    char *text = read_hosts();
    if (!text)
        return -1;
    DNS_RR **tail = records;
    BOOL known = FALSE;
    BOOL address_type = type == DNS_TYPE_A || type == DNS_TYPE_AAAA || type == DNS_TYPE_ALL;
    for (char *line = text; *line;) {
        char *end = line;
        while (*end && *end != '\r' && *end != '\n')
            end++;
        char *next = *end ? end + 1 : end;
        *end = 0;
        char *hash = strchr(line, '#');
        if (hash)
            *hash = 0;
        char *p = line;
        char *tokens[10];
        int count = 0;
        while (count < 10) {
            while (space(*p))
                p++;
            if (!*p)
                break;
            tokens[count++] = p;
            while (*p && !space(*p))
                p++;
            if (*p)
                *p++ = 0;
        }
        line = next;
        BYTE ip[16];
        BOOL v6 = count >= 2 && strchr(tokens[0], ':') != nullptr;
        if (count < 2 || inet_pton(v6 ? AF_INET6 : AF_INET, tokens[0], ip) != 1 || screened_name(tokens[1]))
            continue;
        const char *host = tokens[1];
        WORD kind = v6 ? DNS_TYPE_AAAA : DNS_TYPE_A;
        if (type == DNS_TYPE_PTR) {
            char reverse[96];
            reverse_name(ip, v6, reverse);
            if (DnsNameEqual(reverse, name)) {
                DNS_RR *rr = DnsRrNew(name, DNS_TYPE_PTR, HOSTS_TTL, DnsSectionAnswer);
                if (rr && (rr->host = DnsDuplicate(host)))
                    rr_append(&tail, rr);
                else
                    DnsRrFree(rr);
                known = TRUE;
            }
            continue;
        }
        if (DnsNameEqual(host, name) && address_type) {
            known = TRUE;
            if (type == kind || type == DNS_TYPE_ALL) {
                DNS_RR *rr = address_record(name, kind, HOSTS_TTL, ip);
                if (rr)
                    rr_append(&tail, rr);
            }
        }
        for (int a = 2; a < count && (address_type || type == DNS_TYPE_CNAME); a++) {
            if (screened_name(tokens[a]) || !DnsNameEqual(tokens[a], name))
                continue;
            known = TRUE;
            DNS_RR *alias = DnsRrNew(name, DNS_TYPE_CNAME, HOSTS_TTL, DnsSectionAnswer);
            if (alias && (alias->host = DnsDuplicate(host)))
                rr_append(&tail, alias);
            else
                DnsRrFree(alias);
            if (type != DNS_TYPE_CNAME && (type == kind || type == DNS_TYPE_ALL)) {
                DNS_RR *rr = address_record(host, kind, HOSTS_TTL, ip);
                if (rr)
                    rr_append(&tail, rr);
            }
        }
    }
    DnsRelease(text);
    if (*records)
        return ERROR_SUCCESS;
    return known && (type == DNS_TYPE_A || type == DNS_TYPE_AAAA) ? DNS_INFO_NO_RECORDS : -1;
}

/* ------------------------------------------------------------------ the names of the machine */

static BOOL same_name(const char *name, const char *local)
{
    return DnsNameEqual(name, local);
}

void DnsHostName(char *buffer, DWORD size)
{
    DWORD n = size;
    if (!GetComputerNameExA(ComputerNameDnsHostname, buffer, &n) || !n) {
        n = size;
        if (!GetComputerNameExA(ComputerNameNetBIOS, buffer, &n))
            buffer[0] = 0;
    }
}

void DnsFullName(char *buffer, DWORD size)
{
    char domain[256];
    DWORD n = sizeof(domain);
    DnsHostName(buffer, size);
    if (GetComputerNameExA(ComputerNameDnsDomain, domain, &n) && n && strlen(buffer) + 1 + n < size) {
        strcat(buffer, ".");
        strcat(buffer, domain);
    }
}

static BOOL is_machine_name(const char *name)
{
    char host[256], full[512], netbios[MAX_COMPUTERNAME_LENGTH + 2];
    DWORD n = sizeof(netbios);
    DnsHostName(host, sizeof(host));
    DnsFullName(full, sizeof(full));
    if (same_name(name, host) || same_name(name, full))
        return TRUE;
    return GetComputerNameExA(ComputerNameNetBIOS, netbios, &n) && same_name(name, netbios);
}

/* 1.0.0.127.in-addr.arpa and the ip6.arpa form of ::1. */
static BOOL is_loopback_reverse(const char *name)
{
    static const char v6[] = "1.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.ip6.arpa";
    if (same_name(name, v6))
        return TRUE;
    const char *p = name;
    for (int label = 0; label < 3; label++) {
        if (*p < '0' || *p > '9')
            return FALSE;
        while (*p >= '0' && *p <= '9')
            p++;
        if (*p++ != '.')
            return FALSE;
    }
    return same_name(p, "127.in-addr.arpa");
}

/* what the machine answers for itself: the loopback names, its own name and the loopback addresses reversed. */
static BOOL local_answer(const char *name, WORD type, ULONGLONG options, DNS_ANSWER *answer)
{
    static const BYTE v4[4] = {127, 0, 0, 1};
    static const BYTE v6[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    static const BYTE mapped[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 127, 0, 0, 1};
    if (type == DNS_TYPE_A || type == DNS_TYPE_AAAA) {
        if (!same_name(name, "localhost") && !same_name(name, "loopback") && !is_machine_name(name))
            return FALSE;
        if (type == DNS_TYPE_A)
            answer->records = address_record(name, DNS_TYPE_A, LOCAL_TTL, v4);
        else
            answer->records = address_record(name, DNS_TYPE_AAAA, LOCAL_TTL, (options & DNS_QUERY_DUAL_ADDR) ? mapped : v6);
        answer->status = answer->records ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
        return TRUE;
    }
    if (type == DNS_TYPE_PTR && is_loopback_reverse(name)) {
        char target[512];
        DnsFullName(target, sizeof(target));
        DNS_RR *rr = DnsRrNew(name, DNS_TYPE_PTR, LOCAL_TTL, DnsSectionAnswer);
        if (rr && !(rr->host = DnsDuplicate(target))) {
            DnsRrFree(rr);
            rr = nullptr;
        }
        answer->records = rr;
        answer->status = rr ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ names */

DNS_STATUS DnsCheckName(const char *name, BOOL *numeric)
{
    SIZE_T n = strlen(name);
    *numeric = FALSE;
    if (n && name[n - 1] == '.' && n > 1)
        n--;
    if (!n || n > 253)
        return ERROR_INVALID_NAME;
    if (n == 1 && name[0] == '.')
        return ERROR_SUCCESS;
    SIZE_T label = 0;
    BOOL digits = TRUE;
    for (SIZE_T i = 0; i < n; i++) {
        BYTE c = (BYTE)name[i];
        if (c == '.') {
            if (!label)
                return ERROR_INVALID_NAME;
            label = 0;
            continue;
        }
        if (++label > 63)
            return ERROR_INVALID_NAME;
        if (c <= 0x20 || c == 0x7F)
            return DNS_ERROR_INVALID_NAME_CHAR;
        digits &= c >= '0' && c <= '9';
    }
    if (!label)
        return ERROR_INVALID_NAME;
    *numeric = digits;
    return ERROR_SUCCESS;
}

/* an address written out as the name: an ip4 with four numbers, or an ip6. */
static DNS_RR *literal_record(const char *name)
{
    BYTE ip[16];
    int dots = 0;
    BOOL numbers = TRUE;
    for (const char *p = name; *p; p++) {
        dots += *p == '.';
        numbers &= (*p >= '0' && *p <= '9') || *p == '.';
    }
    if (numbers && dots == 3 && inet_pton(AF_INET, name, ip) == 1)
        return address_record(name, DNS_TYPE_A, LITERAL_TTL, ip);
    if (strchr(name, ':') && inet_pton(AF_INET6, name, ip) == 1)
        return address_record(name, DNS_TYPE_AAAA, LITERAL_TTL, ip);
    return nullptr;
}

void DnsAnswerFree(DNS_ANSWER *answer)
{
    DnsRrFree(answer->records);
    DnsRelease(answer->message);
    memset(answer, 0, sizeof(*answer));
}

/*
 * one name of the search. the hosts file and the cache answer first, then the machine's own names, and a lookup that
 * must not use the wire stops there with no record.
 */
static DNS_STATUS lookup_candidate(const DNS_CONFIG *config, const char *name, WORD type, ULONGLONG options,
                                   BOOL own_servers, volatile LONG *cancelled, DNS_ANSWER *answer)
{
    if (!(options & DNS_QUERY_NO_HOSTS_FILE)) {
        DNS_RR *records;
        LONG status = hosts_lookup(name, type, &records);
        if (status >= 0) {
            answer->records = records;
            return answer->status = status;
        }
    }
    if (!(options & DNS_QUERY_BYPASS_CACHE) && cache_get(name, type, answer))
        return answer->status;
    if (options & DNS_QUERY_NO_WIRE_QUERY)
        return DNS_ERROR_RECORD_DOES_NOT_EXIST;
    DNS_STATUS status = wire_query(config, name, type, options, cancelled, answer);
    answer->status = status;
    /* servers a program names are not the machine's, so what they say is not kept. */
    if (!own_servers)
        cache_put(name, type, answer);
    return status;
}

DNS_STATUS DnsLookup(const char *given, WORD type, ULONGLONG options, const DNS_SERVER *servers, int server_count,
                     volatile LONG *cancelled, DNS_ANSWER *answer)
{
    memset(answer, 0, sizeof(*answer));
    if (options & DNS_QUERY_RETURN_MESSAGE)
        options |= DNS_QUERY_BYPASS_CACHE;
    if (options & (DNS_QUERY_WIRE_ONLY | DNS_QUERY_MULTICAST_ONLY))
        options |= DNS_QUERY_BYPASS_CACHE | DNS_QUERY_NO_LOCAL_NAME | DNS_QUERY_NO_HOSTS_FILE;
    if ((options & (DNS_QUERY_BYPASS_CACHE | DNS_QUERY_NO_WIRE_QUERY)) ==
        (DNS_QUERY_BYPASS_CACHE | DNS_QUERY_NO_WIRE_QUERY))
        return ERROR_INVALID_PARAMETER;
    if ((options & DNS_QUERY_DUAL_ADDR) && type != DNS_TYPE_AAAA)
        return ERROR_INVALID_PARAMETER;

    char own[512];
    const char *name = given;
    if (!*name) {
        DnsFullName(own, sizeof(own));
        name = own;
        if (!*name)
            return 9701;
    }
    DNS_RR *literal = literal_record(name);
    if (literal) {
        answer->records = literal;
        return answer->status = ERROR_SUCCESS;
    }

    char text[300];
    SIZE_T n = strlen(name);
    BOOL absolute = n > 1 && name[n - 1] == '.';
    if (n >= sizeof(text))
        return ERROR_INVALID_NAME;
    memcpy(text, name, n + 1);
    if (absolute)
        text[--n] = 0;

    if (!(options & DNS_QUERY_NO_LOCAL_NAME) && local_answer(text, type, options, answer))
        return answer->status;

    BOOL numeric;
    DNS_STATUS status = DnsCheckName(text, &numeric);
    if (status)
        return status;
    if (numeric)
        return DNS_ERROR_NUMERIC_NAME;

    if (!(options & DNS_QUERY_NO_HOSTS_FILE)) {
        DNS_RR *found;
        LONG hosts = hosts_lookup(text, type, &found);
        if (hosts >= 0) {
            answer->records = found;
            return answer->status = hosts;
        }
    }

    DNS_CONFIG *config = DnsAllocate(sizeof(*config));
    if (!config)
        return ERROR_NOT_ENOUGH_MEMORY;
    load_config(config);
    if (server_count > 0) {
        config->server_count = 0;
        for (int i = 0; i < server_count && i < DNS_MAX_SERVERS; i++)
            config->servers[config->server_count++] = servers[i];
    }

    /* a name with dots goes out as it is first, one without them meets the suffixes first. */
    char candidates[MAX_SUFFIXES + 2][300];
    int count = 0;
    BOOL dotted = strchr(text, '.') != nullptr;
    BOOL search = !absolute && !(options & DNS_QUERY_TREAT_AS_FQDN) && type != DNS_TYPE_PTR && strcmp(text, ".");
    if (dotted || !search)
        strcpy(candidates[count++], text);
    for (int i = 0; search && i < config->suffix_count; i++)
        if (n + 1 + strlen(config->suffixes[i]) < sizeof(candidates[0]))
            _snprintf(candidates[count++], sizeof(candidates[0]) - 1, "%s.%s", text, config->suffixes[i]);
    if (!dotted && search)
        strcpy(candidates[count++], text);

    DNS_STATUS result = DNS_ERROR_RCODE_NAME_ERROR;
    BOOL empty = FALSE, failed = FALSE;
    for (int i = 0; i < count && !failed; i++) {
        DNS_ANSWER got = {};
        status = lookup_candidate(config, candidates[i], type, options, server_count > 0, cancelled, &got);
        if (status == ERROR_SUCCESS) {
            DnsAnswerFree(answer);
            *answer = got;
            DnsRelease(config);
            return answer->status = ERROR_SUCCESS;
        }
        if (got.message) {
            DnsRelease(answer->message);
            answer->message = got.message;
            answer->message_length = got.message_length;
            got.message = nullptr;
        }
        DnsAnswerFree(&got);
        if (status == DNS_INFO_NO_RECORDS) {
            empty = TRUE;
        } else if (status != DNS_ERROR_RCODE_NAME_ERROR) {
            result = status;
            failed = TRUE;
        }
    }
    if (!failed && empty)
        result = DNS_INFO_NO_RECORDS;
    DnsRelease(config);
    answer->status = result;
    return result;
}
