/*
 * name resolution: the host name of the machine identity, localhost,
 * numeric addresses, and the service and protocol databases. other names
 * go to the resolver of the host through the socket driver.
 */
#include "ws2_32_private.h"

/* ------------------------------------------------------------------ the local host */

void WsHostName(char *buffer, SIZE_T size)
{
    DWORD n = (DWORD)size;
    if (GetComputerNameExA(ComputerNameDnsHostname, buffer, &n) && n)
        return;
    n = (DWORD)size;
    if (GetComputerNameExA(ComputerNameNetBIOS, buffer, &n) && n)
        return;
    _snprintf(buffer, size - 1, "%s", "localhost");
}

void WsFullHostName(char *buffer, SIZE_T size)
{
    char domain[256];
    DWORD n = sizeof(domain);
    WsHostName(buffer, size);
    if (!GetComputerNameExA(ComputerNameDnsDomain, domain, &n) || !n)
        return;
    SIZE_T used = strlen(buffer);
    if (used + 1 + n < size)
        _snprintf(buffer + used, size - used - 1, ".%s", domain);
}

/* compares ignoring case and one trailing dot on name. */
static BOOL same_name(const char *name, const char *local)
{
    SIZE_T n = strlen(name), l = strlen(local);
    if (n && name[n - 1] == '.')
        n--;
    return l && n == l && !_strnicmp(name, local, n);
}

BOOL WsIsLocalName(const char *name)
{
    char host[256], full[512], netbios[MAX_COMPUTERNAME_LENGTH + 2];
    DWORD n = sizeof(netbios);
    if (!*name || same_name(name, "localhost"))
        return TRUE;
    WsHostName(host, sizeof(host));
    WsFullHostName(full, sizeof(full));
    if (same_name(name, host) || same_name(name, full))
        return TRUE;
    return GetComputerNameExA(ComputerNameNetBIOS, netbios, &n) && same_name(name, netbios);
}

/* the canonical name of a local name. */
static void local_canonical(const char *name, char *buffer, SIZE_T size)
{
    if (same_name(name, "localhost"))
        _snprintf(buffer, size - 1, "%s", "localhost");
    else
        WsFullHostName(buffer, size);
}

BOOL WsIsLoopbackV4(const IN_ADDR *addr)
{
    return addr->S_un.S_un_b.s_b1 == 127;
}

BOOL WsIsLoopbackV6(const IN6_ADDR *addr)
{
    static const BYTE loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    static const BYTE mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
    if (!memcmp(addr->u.Byte, loopback, 16))
        return TRUE;
    return !memcmp(addr->u.Byte, mapped, 12) && addr->u.Byte[12] == 127;
}

int WSAAPI gethostname(char *name, int namelen)
{
    int error = WsCheckStarted();
    if (error)
        return WsFail(error);
    if (!name || namelen <= 0)
        return WsFail(WSAEFAULT);
    char host[256];
    WsHostName(host, sizeof(host));
    SIZE_T n = strlen(host);
    if (n >= (SIZE_T)namelen)
        return WsFail(WSAEFAULT);
    memcpy(name, host, n + 1);
    return 0;
}

int WSAAPI GetHostNameW(PWSTR name, int namelen)
{
    int error = WsCheckStarted();
    if (error)
        return WsFail(error);
    if (!name || namelen <= 0)
        return WsFail(WSAEFAULT);
    char host[256];
    WsHostName(host, sizeof(host));
    if (!WsWiden(host, name, (SIZE_T)namelen))
        return WsFail(WSAEFAULT);
    return 0;
}

/* ------------------------------------------------------------------ services and protocols */

/*
 * the common entries of %SystemRoot%\System32\drivers\etc\services, which
 * mime does not ship; recalled from windows, not read from a file.
 */
typedef struct {
    const char *name;
    USHORT port;
    const char *proto;
    const char *aliases[3];
} WS_SERVICE;

static const WS_SERVICE services[] = {
    {"echo", 7, "tcp", {NULL}},
    {"echo", 7, "udp", {NULL}},
    {"discard", 9, "tcp", {"sink", "null"}},
    {"discard", 9, "udp", {"sink", "null"}},
    {"systat", 11, "tcp", {"users"}},
    {"systat", 11, "udp", {"users"}},
    {"daytime", 13, "tcp", {NULL}},
    {"daytime", 13, "udp", {NULL}},
    {"qotd", 17, "tcp", {"quote"}},
    {"qotd", 17, "udp", {"quote"}},
    {"chargen", 19, "tcp", {"ttytst", "source"}},
    {"chargen", 19, "udp", {"ttytst", "source"}},
    {"ftp-data", 20, "tcp", {NULL}},
    {"ftp", 21, "tcp", {NULL}},
    {"ssh", 22, "tcp", {NULL}},
    {"telnet", 23, "tcp", {NULL}},
    {"smtp", 25, "tcp", {"mail"}},
    {"time", 37, "tcp", {"timserver"}},
    {"time", 37, "udp", {"timserver"}},
    {"rlp", 39, "udp", {"resource"}},
    {"nameserver", 42, "tcp", {"name"}},
    {"nameserver", 42, "udp", {"name"}},
    {"nicname", 43, "tcp", {"whois"}},
    {"domain", 53, "tcp", {NULL}},
    {"domain", 53, "udp", {NULL}},
    {"bootps", 67, "udp", {"dhcps"}},
    {"bootpc", 68, "udp", {"dhcpc"}},
    {"tftp", 69, "udp", {NULL}},
    {"gopher", 70, "tcp", {NULL}},
    {"finger", 79, "tcp", {NULL}},
    {"http", 80, "tcp", {"www", "www-http"}},
    {"kerberos", 88, "tcp", {"krb5", "kerberos-sec"}},
    {"kerberos", 88, "udp", {"krb5", "kerberos-sec"}},
    {"hostname", 101, "tcp", {"hostnames"}},
    {"iso-tsap", 102, "tcp", {NULL}},
    {"rtelnet", 107, "tcp", {NULL}},
    {"pop2", 109, "tcp", {"postoffice"}},
    {"pop3", 110, "tcp", {NULL}},
    {"sunrpc", 111, "tcp", {"rpcbind", "portmap"}},
    {"sunrpc", 111, "udp", {"rpcbind", "portmap"}},
    {"auth", 113, "tcp", {"ident", "tap"}},
    {"uucp-path", 117, "tcp", {NULL}},
    {"sqlserv", 118, "tcp", {NULL}},
    {"nntp", 119, "tcp", {"usenet"}},
    {"ntp", 123, "udp", {NULL}},
    {"epmap", 135, "tcp", {"loc-srv"}},
    {"epmap", 135, "udp", {"loc-srv"}},
    {"netbios-ns", 137, "tcp", {"nbname"}},
    {"netbios-ns", 137, "udp", {"nbname"}},
    {"netbios-dgm", 138, "udp", {"nbdatagram"}},
    {"netbios-ssn", 139, "tcp", {"nbsession"}},
    {"imap", 143, "tcp", {"imap4"}},
    {"sql-net", 150, "tcp", {NULL}},
    {"sqlsrv", 156, "tcp", {NULL}},
    {"pcmail-srv", 158, "tcp", {NULL}},
    {"snmp", 161, "udp", {NULL}},
    {"snmptrap", 162, "udp", {"snmp-trap"}},
    {"print-srv", 170, "tcp", {NULL}},
    {"bgp", 179, "tcp", {NULL}},
    {"irc", 194, "tcp", {NULL}},
    {"ipx", 213, "udp", {NULL}},
    {"rtsps", 322, "tcp", {NULL}},
    {"rtsps", 322, "udp", {NULL}},
    {"mftp", 349, "tcp", {NULL}},
    {"mftp", 349, "udp", {NULL}},
    {"ldap", 389, "tcp", {NULL}},
    {"https", 443, "tcp", {"MCom"}},
    {"https", 443, "udp", {"MCom"}},
    {"microsoft-ds", 445, "tcp", {NULL}},
    {"microsoft-ds", 445, "udp", {NULL}},
    {"kpasswd", 464, "tcp", {NULL}},
    {"kpasswd", 464, "udp", {NULL}},
    {"isakmp", 500, "udp", {"ike"}},
    {"crs", 507, "tcp", {NULL}},
    {"crs", 507, "udp", {NULL}},
    {"exec", 512, "tcp", {NULL}},
    {"biff", 512, "udp", {"comsat"}},
    {"login", 513, "tcp", {NULL}},
    {"who", 513, "udp", {"whod"}},
    {"cmd", 514, "tcp", {"shell"}},
    {"syslog", 514, "udp", {NULL}},
    {"printer", 515, "tcp", {"spooler"}},
    {"talk", 517, "udp", {NULL}},
    {"ntalk", 518, "udp", {NULL}},
    {"efs", 520, "tcp", {NULL}},
    {"router", 520, "udp", {"route", "routed"}},
    {"ulp", 522, "tcp", {NULL}},
    {"ulp", 522, "udp", {NULL}},
    {"timed", 525, "udp", {"timeserver"}},
    {"tempo", 526, "tcp", {"newdate"}},
    {"irc-serv", 529, "tcp", {NULL}},
    {"irc-serv", 529, "udp", {NULL}},
    {"courier", 530, "tcp", {"rpc"}},
    {"conference", 531, "tcp", {"chat"}},
    {"netnews", 532, "tcp", {"readnews"}},
    {"netwall", 533, "udp", {NULL}},
    {"uucp", 540, "tcp", {"uucpd"}},
    {"klogin", 543, "tcp", {NULL}},
    {"kshell", 544, "tcp", {"krcmd"}},
    {"dhcpv6-client", 546, "tcp", {NULL}},
    {"dhcpv6-client", 546, "udp", {NULL}},
    {"dhcpv6-server", 547, "tcp", {NULL}},
    {"dhcpv6-server", 547, "udp", {NULL}},
    {"afpovertcp", 548, "tcp", {NULL}},
    {"afpovertcp", 548, "udp", {NULL}},
    {"new-rwho", 550, "udp", {"new-who"}},
    {"rtsp", 554, "tcp", {NULL}},
    {"rtsp", 554, "udp", {NULL}},
    {"remotefs", 556, "tcp", {"rfs", "rfs_server"}},
    {"rmonitor", 560, "udp", {"rmonitord"}},
    {"monitor", 561, "udp", {NULL}},
    {"nntps", 563, "tcp", {"snntp"}},
    {"nntps", 563, "udp", {"snntp"}},
    {"whoami", 565, "tcp", {NULL}},
    {"whoami", 565, "udp", {NULL}},
    {"ms-shuttle", 568, "tcp", {NULL}},
    {"ms-shuttle", 568, "udp", {NULL}},
    {"ms-rome", 569, "tcp", {NULL}},
    {"ms-rome", 569, "udp", {NULL}},
    {"http-rpc-epmap", 593, "tcp", {NULL}},
    {"http-rpc-epmap", 593, "udp", {NULL}},
    {"hmmp-ind", 612, "tcp", {NULL}},
    {"hmmp-ind", 612, "udp", {NULL}},
    {"hmmp-op", 613, "tcp", {NULL}},
    {"hmmp-op", 613, "udp", {NULL}},
    {"ldaps", 636, "tcp", {"sldap"}},
    {"doom", 666, "tcp", {NULL}},
    {"doom", 666, "udp", {NULL}},
    {"msexch-routing", 691, "tcp", {NULL}},
    {"msexch-routing", 691, "udp", {NULL}},
    {"kerberos-adm", 749, "tcp", {NULL}},
    {"kerberos-adm", 749, "udp", {NULL}},
    {"kerberos-iv", 750, "udp", {NULL}},
    {"mdbs_daemon", 800, "tcp", {NULL}},
    {"mdbs_daemon", 800, "udp", {NULL}},
    {"ftps-data", 989, "tcp", {NULL}},
    {"ftps", 990, "tcp", {NULL}},
    {"telnets", 992, "tcp", {NULL}},
    {"imaps", 993, "tcp", {NULL}},
    {"ircs", 994, "tcp", {NULL}},
    {"pop3s", 995, "tcp", {"spop3"}},
    {"pop3s", 995, "udp", {"spop3"}},
    {"kpop", 1109, "tcp", {NULL}},
    {"nfsd-status", 1110, "tcp", {NULL}},
    {"nfsd-keepalive", 1110, "udp", {NULL}},
    {"nfa", 1155, "tcp", {NULL}},
    {"nfa", 1155, "udp", {NULL}},
    {"activesync", 1034, "tcp", {NULL}},
    {"opsmgr", 1270, "tcp", {NULL}},
    {"opsmgr", 1270, "udp", {NULL}},
    {"ms-sql-s", 1433, "tcp", {NULL}},
    {"ms-sql-s", 1433, "udp", {NULL}},
    {"ms-sql-m", 1434, "tcp", {NULL}},
    {"ms-sql-m", 1434, "udp", {NULL}},
    {"ms-sna-server", 1477, "tcp", {NULL}},
    {"ms-sna-server", 1477, "udp", {NULL}},
    {"ms-sna-base", 1478, "tcp", {NULL}},
    {"ms-sna-base", 1478, "udp", {NULL}},
    {"wins", 1512, "tcp", {NULL}},
    {"wins", 1512, "udp", {NULL}},
    {"ingreslock", 1524, "tcp", {"ingres"}},
    {"stt", 1607, "tcp", {NULL}},
    {"stt", 1607, "udp", {NULL}},
    {"l2tp", 1701, "udp", {NULL}},
    {"pptconference", 1711, "tcp", {NULL}},
    {"pptconference", 1711, "udp", {NULL}},
    {"pptp", 1723, "tcp", {NULL}},
    {"msiccp", 1731, "tcp", {NULL}},
    {"msiccp", 1731, "udp", {NULL}},
    {"remote-winsock", 1745, "tcp", {NULL}},
    {"remote-winsock", 1745, "udp", {NULL}},
    {"ms-streaming", 1755, "tcp", {NULL}},
    {"ms-streaming", 1755, "udp", {NULL}},
    {"msmq", 1801, "tcp", {NULL}},
    {"msmq", 1801, "udp", {NULL}},
    {"radius", 1812, "udp", {NULL}},
    {"radacct", 1813, "udp", {NULL}},
    {"msnp", 1863, "tcp", {NULL}},
    {"msnp", 1863, "udp", {NULL}},
    {"ssdp", 1900, "tcp", {NULL}},
    {"ssdp", 1900, "udp", {NULL}},
    {"close-combat", 1944, "tcp", {NULL}},
    {"close-combat", 1944, "udp", {NULL}},
    {"nfsd", 2049, "udp", {"nfs"}},
    {"knetd", 2053, "tcp", {NULL}},
    {"mzap", 2106, "tcp", {NULL}},
    {"mzap", 2106, "udp", {NULL}},
    {"qwave", 2177, "tcp", {NULL}},
    {"qwave", 2177, "udp", {NULL}},
    {"directplay", 2234, "tcp", {NULL}},
    {"directplay", 2234, "udp", {NULL}},
    {"ms-olap3", 2382, "tcp", {NULL}},
    {"ms-olap3", 2382, "udp", {NULL}},
    {"ms-olap4", 2383, "tcp", {NULL}},
    {"ms-olap4", 2383, "udp", {NULL}},
    {"ms-olap1", 2393, "tcp", {NULL}},
    {"ms-olap1", 2393, "udp", {NULL}},
    {"ms-olap2", 2394, "tcp", {NULL}},
    {"ms-olap2", 2394, "udp", {NULL}},
    {"ms-theater", 2460, "tcp", {NULL}},
    {"ms-theater", 2460, "udp", {NULL}},
    {"wlbs", 2504, "tcp", {NULL}},
    {"wlbs", 2504, "udp", {NULL}},
    {"ms-v-worlds", 2525, "tcp", {NULL}},
    {"ms-v-worlds", 2525, "udp", {NULL}},
    {"sms-rcinfo", 2701, "tcp", {NULL}},
    {"sms-rcinfo", 2701, "udp", {NULL}},
    {"sms-xfer", 2702, "tcp", {NULL}},
    {"sms-xfer", 2702, "udp", {NULL}},
    {"sms-chat", 2703, "tcp", {NULL}},
    {"sms-chat", 2703, "udp", {NULL}},
    {"sms-remctrl", 2704, "tcp", {NULL}},
    {"sms-remctrl", 2704, "udp", {NULL}},
    {"msolap-ptp2", 2725, "tcp", {NULL}},
    {"msolap-ptp2", 2725, "udp", {NULL}},
    {"icslap", 2869, "tcp", {NULL}},
    {"icslap", 2869, "udp", {NULL}},
    {"cifs", 3020, "tcp", {NULL}},
    {"cifs", 3020, "udp", {NULL}},
    {"xbox", 3074, "tcp", {NULL}},
    {"xbox", 3074, "udp", {NULL}},
    {"ms-dotnetster", 3126, "tcp", {NULL}},
    {"ms-dotnetster", 3126, "udp", {NULL}},
    {"ms-rule-engine", 3132, "tcp", {NULL}},
    {"ms-rule-engine", 3132, "udp", {NULL}},
    {"msft-gc", 3268, "tcp", {NULL}},
    {"msft-gc", 3268, "udp", {NULL}},
    {"msft-gc-ssl", 3269, "tcp", {NULL}},
    {"msft-gc-ssl", 3269, "udp", {NULL}},
    {"ms-cluster-net", 3343, "tcp", {NULL}},
    {"ms-cluster-net", 3343, "udp", {NULL}},
    {"ms-wbt-server", 3389, "tcp", {NULL}},
    {"ms-wbt-server", 3389, "udp", {NULL}},
    {"ms-la", 3535, "tcp", {NULL}},
    {"ms-la", 3535, "udp", {NULL}},
    {"pnrp-port", 3540, "tcp", {NULL}},
    {"pnrp-port", 3540, "udp", {NULL}},
    {"teredo", 3544, "tcp", {NULL}},
    {"teredo", 3544, "udp", {NULL}},
    {"p2pgroup", 3587, "tcp", {NULL}},
    {"p2pgroup", 3587, "udp", {NULL}},
    {"ws-discovery", 3702, "udp", {NULL}},
    {"dvcprov-port", 3776, "tcp", {NULL}},
    {"dvcprov-port", 3776, "udp", {NULL}},
    {"msfw-control", 3847, "tcp", {NULL}},
    {"msdts1", 3882, "tcp", {NULL}},
    {"sdp-portmapper", 3935, "tcp", {NULL}},
    {"sdp-portmapper", 3935, "udp", {NULL}},
    {"net-device", 4350, "tcp", {NULL}},
    {"net-device", 4350, "udp", {NULL}},
    {"ipsec-msft", 4500, "tcp", {NULL}},
    {"ipsec-msft", 4500, "udp", {NULL}},
    {"llmnr", 5355, "tcp", {NULL}},
    {"llmnr", 5355, "udp", {NULL}},
    {"rrac", 5678, "tcp", {NULL}},
    {"rrac", 5678, "udp", {NULL}},
    {"dccm", 5679, "tcp", {NULL}},
    {"dccm", 5679, "udp", {NULL}},
    {"ms-licensing", 5720, "tcp", {NULL}},
    {"ms-licensing", 5720, "udp", {NULL}},
    {"directplay8", 6073, "tcp", {NULL}},
    {"directplay8", 6073, "udp", {NULL}},
    {"man", 9535, "tcp", {NULL}},
    {"rasadv", 9753, "tcp", {NULL}},
    {"rasadv", 9753, "udp", {NULL}},
    {"imip-channels", 11320, "tcp", {NULL}},
    {"imip-channels", 11320, "udp", {NULL}},
    {"directplaysrvr", 47624, "tcp", {NULL}},
    {"directplaysrvr", 47624, "udp", {NULL}},
};

static BOOL proto_matches(const char *want, const char *proto)
{
    return !want || !*want || !_stricmp(want, proto);
}

static BOOL service_named(const WS_SERVICE *s, const char *name)
{
    if (!_stricmp(s->name, name))
        return TRUE;
    for (int i = 0; i < 3 && s->aliases[i]; i++) {
        if (!_stricmp(s->aliases[i], name))
            return TRUE;
    }
    return FALSE;
}

static const WS_SERVICE *find_service_name(const char *name, const char *proto)
{
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(services); i++) {
        if (service_named(&services[i], name) && proto_matches(proto, services[i].proto))
            return &services[i];
    }
    return NULL;
}

static const WS_SERVICE *find_service_port(USHORT port, const char *proto)
{
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(services); i++) {
        if (services[i].port == port && proto_matches(proto, services[i].proto))
            return &services[i];
    }
    return NULL;
}

BOOL WsServiceByName(const char *name, const char *proto, USHORT *port, const char **found_proto)
{
    const WS_SERVICE *s = find_service_name(name, proto);
    if (!s)
        return FALSE;
    *port = s->port;
    if (found_proto)
        *found_proto = s->proto;
    return TRUE;
}

const char *WsServiceByPort(USHORT port, const char *proto)
{
    const WS_SERVICE *s = find_service_port(port, proto);
    return s ? s->name : NULL;
}

static void copy_name(char *dst, SIZE_T size, const char *src)
{
    _snprintf(dst, size - 1, "%s", src);
    dst[size - 1] = 0;
}

static struct servent *fill_servent(const WS_SERVICE *s)
{
    WS_THREAD *t = WsThread();
    if (!t) {
        SetLastError(WSA_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    int n = 0;
    copy_name(t->serv_names[0], sizeof(t->serv_names[0]), s->name);
    for (int i = 0; i < 3 && s->aliases[i]; i++) {
        copy_name(t->serv_names[i + 1], sizeof(t->serv_names[0]), s->aliases[i]);
        t->serv_aliases[n++] = t->serv_names[i + 1];
    }
    t->serv_aliases[n] = NULL;
    copy_name(t->serv_proto, sizeof(t->serv_proto), s->proto);
    t->serv.s_name = t->serv_names[0];
    t->serv.s_aliases = t->serv_aliases;
    t->serv.s_proto = t->serv_proto;
    t->serv.s_port = (short)htons(s->port);
    return &t->serv;
}

struct servent *WSAAPI getservbyname(const char *name, const char *proto)
{
    int error = WsCheckStarted();
    if (error) {
        SetLastError(error);
        return NULL;
    }
    if (!name) {
        SetLastError(WSAEINVAL);
        return NULL;
    }
    const WS_SERVICE *s = find_service_name(name, proto);
    if (!s) {
        SetLastError(WSANO_DATA);
        return NULL;
    }
    return fill_servent(s);
}

struct servent *WSAAPI getservbyport(int port, const char *proto)
{
    int error = WsCheckStarted();
    if (error) {
        SetLastError(error);
        return NULL;
    }
    const WS_SERVICE *s = find_service_port(ntohs((u_short)port), proto);
    if (!s) {
        SetLastError(WSANO_DATA);
        return NULL;
    }
    return fill_servent(s);
}

/* %SystemRoot%\System32\drivers\etc\protocol, recalled from windows. */
typedef struct {
    const char *name;
    short number;
    const char *alias;
} WS_PROTOCOL;

static const WS_PROTOCOL protocols[] = {
    {"ip", 0, "IP"},
    {"icmp", 1, "ICMP"},
    {"ggp", 3, "GGP"},
    {"tcp", 6, "TCP"},
    {"egp", 8, "EGP"},
    {"pup", 12, "PUP"},
    {"udp", 17, "UDP"},
    {"hmp", 20, "HMP"},
    {"xns-idp", 22, "XNS-IDP"},
    {"rdp", 27, "RDP"},
    {"ipv6", 41, "IPv6"},
    {"ipv6-route", 43, "IPv6-Route"},
    {"ipv6-frag", 44, "IPv6-Frag"},
    {"esp", 50, "ESP"},
    {"ah", 51, "AH"},
    {"ipv6-icmp", 58, "IPv6-ICMP"},
    {"ipv6-nonxt", 59, "IPv6-NoNxt"},
    {"ipv6-opts", 60, "IPv6-Opts"},
    {"rvd", 66, "RVD"},
};

static struct protoent *fill_protoent(const WS_PROTOCOL *p)
{
    WS_THREAD *t = WsThread();
    if (!t) {
        SetLastError(WSA_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    copy_name(t->proto_names[0], sizeof(t->proto_names[0]), p->name);
    copy_name(t->proto_names[1], sizeof(t->proto_names[1]), p->alias);
    t->proto_aliases[0] = t->proto_names[1];
    t->proto_aliases[1] = NULL;
    t->proto.p_name = t->proto_names[0];
    t->proto.p_aliases = t->proto_aliases;
    t->proto.p_proto = p->number;
    return &t->proto;
}

struct protoent *WSAAPI getprotobyname(const char *name)
{
    int error = WsCheckStarted();
    if (error) {
        SetLastError(error);
        return NULL;
    }
    if (!name) {
        SetLastError(WSAEINVAL);
        return NULL;
    }
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(protocols); i++) {
        if (!_stricmp(protocols[i].name, name) || !_stricmp(protocols[i].alias, name))
            return fill_protoent(&protocols[i]);
    }
    SetLastError(WSANO_DATA);
    return NULL;
}

struct protoent *WSAAPI getprotobynumber(int number)
{
    int error = WsCheckStarted();
    if (error) {
        SetLastError(error);
        return NULL;
    }
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(protocols); i++) {
        if (protocols[i].number == number)
            return fill_protoent(&protocols[i]);
    }
    SetLastError(WSANO_DATA);
    return NULL;
}

/* ------------------------------------------------------------------ gethostbyname and gethostbyaddr */

static struct hostent *fill_hostent(const char *name, short family, const void *addresses, int count)
{
    WS_THREAD *t = WsThread();
    if (!t) {
        SetLastError(WSA_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    int length = family == AF_INET6 ? 16 : 4;
    copy_name(t->host_name, sizeof(t->host_name), name);
    t->host_aliases[0] = NULL;
    for (int i = 0; i < count && i < WS_HOST_ADDRESSES; i++) {
        memcpy(t->host_addresses[i], (const BYTE *)addresses + i * length, length);
        t->host_list[i] = (char *)t->host_addresses[i];
    }
    t->host_list[count] = NULL;
    t->host.h_name = t->host_name;
    t->host.h_aliases = t->host_aliases;
    t->host.h_addrtype = family;
    t->host.h_length = (short)length;
    t->host.h_addr_list = t->host_list;
    return &t->host;
}

struct hostent *WSAAPI gethostbyname(const char *name)
{
    int error = WsCheckStarted();
    if (error) {
        SetLastError(error);
        return NULL;
    }
    char host[512];
    if (!name) {
        WsFullHostName(host, sizeof(host));
        name = host;
    }
    IN_ADDR addr;
    const char *end;
    if (WsParseIpv4(name, FALSE, &addr, &end) && !*end)
        return fill_hostent(name, AF_INET, &addr, 1);
    if (!WsIsLocalName(name)) {
        MIME_AFD_ADDRESS found[WS_HOST_ADDRESSES];
        int n = WsResolve(name, AF_INET, found, WS_HOST_ADDRESSES);
        if (n <= 0) {
            SetLastError(n < 0 ? -n : WSANO_DATA);
            return NULL;
        }
        IN_ADDR list[WS_HOST_ADDRESSES];
        for (int i = 0; i < n; i++)
            list[i] = ((SOCKADDR_IN *)&found[i])->sin_addr;
        return fill_hostent(name, AF_INET, list, n);
    }
    char canonical[512];
    local_canonical(name, canonical, sizeof(canonical));
    addr.S_un.S_addr = htonl(INADDR_LOOPBACK);
    return fill_hostent(canonical, AF_INET, &addr, 1);
}

struct hostent *WSAAPI gethostbyaddr(const char *addr, int len, int type)
{
    int error = WsCheckStarted();
    if (error) {
        SetLastError(error);
        return NULL;
    }
    if (!addr) {
        SetLastError(WSAEFAULT);
        return NULL;
    }
    BOOL loopback;
    if (type == AF_INET) {
        if (len < 4) {
            SetLastError(WSAEFAULT);
            return NULL;
        }
        IN_ADDR a;
        memcpy(&a, addr, 4);
        loopback = WsIsLoopbackV4(&a);
    } else if (type == AF_INET6) {
        if (len < 16) {
            SetLastError(WSAEFAULT);
            return NULL;
        }
        IN6_ADDR a;
        memcpy(&a, addr, 16);
        loopback = WsIsLoopbackV6(&a);
    } else {
        SetLastError(WSAEAFNOSUPPORT);
        return NULL;
    }
    if (!loopback) {
        SetLastError(WSAHOST_NOT_FOUND);
        return NULL;
    }
    char full[512];
    WsFullHostName(full, sizeof(full));
    return fill_hostent(full, (short)type, addr, 1);
}

/* ------------------------------------------------------------------ getaddrinfo */

static const IN6_ADDR in6_any = {{{0}}};
static const IN6_ADDR in6_loopback = {{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}}};

typedef struct {
    int socktype;
    int protocol;
    USHORT port;
    PADDRINFOW *tail;
} WS_GAI;

static PADDRINFOW append_v4(WS_GAI *g, IN_ADDR addr, int flags)
{
    PADDRINFOW ai = WsAllocZero(sizeof(*ai));
    SOCKADDR_IN *in = WsAllocZero(sizeof(*in));
    if (!ai || !in) {
        WsFree(ai);
        WsFree(in);
        return NULL;
    }
    in->sin_family = AF_INET;
    in->sin_port = g->port;
    in->sin_addr = addr;
    ai->ai_flags = flags;
    ai->ai_family = AF_INET;
    ai->ai_socktype = g->socktype;
    ai->ai_protocol = g->protocol;
    ai->ai_addrlen = sizeof(*in);
    ai->ai_addr = (SOCKADDR *)in;
    *g->tail = ai;
    g->tail = &ai->ai_next;
    return ai;
}

static PADDRINFOW append_v6(WS_GAI *g, const IN6_ADDR *addr, ULONG scope, int flags)
{
    PADDRINFOW ai = WsAllocZero(sizeof(*ai));
    SOCKADDR_IN6 *in6 = WsAllocZero(sizeof(*in6));
    if (!ai || !in6) {
        WsFree(ai);
        WsFree(in6);
        return NULL;
    }
    in6->sin6_family = AF_INET6;
    in6->sin6_port = g->port;
    in6->sin6_addr = *addr;
    in6->sin6_scope_id = scope;
    ai->ai_flags = flags;
    ai->ai_family = AF_INET6;
    ai->ai_socktype = g->socktype;
    ai->ai_protocol = g->protocol;
    ai->ai_addrlen = sizeof(*in6);
    ai->ai_addr = (SOCKADDR *)in6;
    *g->tail = ai;
    g->tail = &ai->ai_next;
    return ai;
}

static IN6_ADDR mapped(IN_ADDR v4)
{
    IN6_ADDR a = {{{0}}};
    a.u.Byte[10] = 0xFF;
    a.u.Byte[11] = 0xFF;
    memcpy(&a.u.Byte[12], &v4, 4);
    return a;
}

static PWSTR wide_copy(const char *s)
{
    SIZE_T n = strlen(s) + 1;
    PWSTR w = WsAlloc(n * sizeof(WCHAR));
    if (w)
        WsWiden(s, w, n);
    return w;
}

void WSAAPI FreeAddrInfoW(PADDRINFOW info)
{
    while (info) {
        PADDRINFOW next = info->ai_next;
        WsFree(info->ai_canonname);
        WsFree(info->ai_addr);
        WsFree(info);
        info = next;
    }
}

void WSAAPI freeaddrinfo(PADDRINFOA info)
{
    FreeAddrInfoW((PADDRINFOW)info);
}

/* the port of a service string, in network order. */
static int service_port(const char *service, int flags, int socktype, USHORT *port)
{
    if (!service || !*service) {
        *port = 0;
        return 0;
    }
    BOOL numeric = TRUE;
    ULONG value = 0;
    for (const char *p = service; *p; p++) {
        if (*p < '0' || *p > '9') {
            numeric = FALSE;
            break;
        }
        value = value * 10 + (ULONG)(*p - '0');
        if (value > 0xFFFF)
            return WSATYPE_NOT_FOUND;
    }
    if (numeric) {
        *port = htons((USHORT)value);
        return 0;
    }
    if (flags & AI_NUMERICSERV)
        return WSAHOST_NOT_FOUND;
    const char *proto = socktype == SOCK_STREAM ? "tcp" : socktype == SOCK_DGRAM ? "udp" : NULL;
    USHORT p;
    if (!WsServiceByName(service, proto, &p, NULL))
        return WSATYPE_NOT_FOUND;
    *port = htons(p);
    return 0;
}

static int set_canonname(PADDRINFOW first, const char *name)
{
    if (!first)
        return 0;
    first->ai_canonname = wide_copy(name);
    return first->ai_canonname ? 0 : WSA_NOT_ENOUGH_MEMORY;
}

/* the resolver behind getaddrinfo and GetAddrInfoW; node and service are ascii here. */
static int lookup(const char *node, const char *service, const ADDRINFOW *hints, PADDRINFOW *result)
{
    int flags = 0, family = AF_UNSPEC;
    WS_GAI g = {0, 0, 0, result};
    *result = NULL;
    if (!node && !service)
        return WSAHOST_NOT_FOUND;
    if (hints) {
        if (hints->ai_addrlen || hints->ai_canonname || hints->ai_addr || hints->ai_next)
            return WSANO_RECOVERY;
        flags = hints->ai_flags;
        if ((flags & (AI_CANONNAME | AI_FQDN)) && !node)
            return WSAEINVAL;
        if ((flags & (AI_CANONNAME | AI_FQDN)) == (AI_CANONNAME | AI_FQDN))
            return WSAEINVAL;
        family = (USHORT)hints->ai_family;
        if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6)
            return WSAEAFNOSUPPORT;
        if ((unsigned)hints->ai_socktype > SOCK_RAW)
            return WSAESOCKTNOSUPPORT;
        g.socktype = hints->ai_socktype;
        g.protocol = hints->ai_protocol;
    }
    int error = service_port(service, flags, g.socktype, &g.port);
    if (error)
        return error;

    if (!node) {
        BOOL passive = flags & AI_PASSIVE;
        if (family != AF_INET && !append_v6(&g, passive ? &in6_any : &in6_loopback, 0, 0))
            return WSA_NOT_ENOUGH_MEMORY;
        IN_ADDR v4 = {.S_un.S_addr = passive ? INADDR_ANY : htonl(INADDR_LOOPBACK)};
        if (family != AF_INET6 && !append_v4(&g, v4, 0))
            return WSA_NOT_ENOUGH_MEMORY;
        return 0;
    }

    /* numeric hosts: the canonical name is the address text, the fqdn the node itself. */
    PADDRINFOW numeric = NULL;
    IN6_ADDR a6;
    ULONG scope;
    USHORT ignored_port;
    IN_ADDR a4;
    const char *end;
    if (family != AF_INET && WsParseIpv6Ex(node, &a6, &scope, &ignored_port)) {
        if (!(numeric = append_v6(&g, &a6, scope, AI_NUMERICHOST)))
            return WSA_NOT_ENOUGH_MEMORY;
    } else if (family != AF_INET6 && WsParseIpv4(node, !(flags & AI_NUMERICHOST), &a4, &end) && !*end) {
        if (!(numeric = append_v4(&g, a4, AI_NUMERICHOST)))
            return WSA_NOT_ENOUGH_MEMORY;
    } else if (family == AF_INET6 && (flags & AI_V4MAPPED) && WsParseIpv4(node, TRUE, &a4, &end) && !*end) {
        IN6_ADDR m = mapped(a4);
        if (!(numeric = append_v6(&g, &m, 0, AI_NUMERICHOST)))
            return WSA_NOT_ENOUGH_MEMORY;
    }
    if (numeric) {
        if (flags & AI_CANONNAME) {
            char text[96];
            WsFormatSockaddr(numeric->ai_addr, (DWORD)numeric->ai_addrlen, text, sizeof(text));
            return set_canonname(numeric, text);
        }
        if (flags & AI_FQDN)
            return set_canonname(numeric, node);
        return 0;
    }
    if (flags & AI_NUMERICHOST)
        return WSAHOST_NOT_FOUND;

    /* other names go to the host's resolver; the local ones resolve to the loopback. */
    if (!WsIsLocalName(node)) {
        MIME_AFD_ADDRESS found[32];
        int n = WsResolve(node, family, found, 32);
        if (n < 0)
            return -n;
        PADDRINFOW first = NULL;
        for (int i = 0; i < n; i++) {
            PADDRINFOW added = NULL;
            if (found[i].Family == AF_INET && family != AF_INET6) {
                added = append_v4(&g, ((SOCKADDR_IN *)&found[i])->sin_addr, 0);
            } else if (found[i].Family == AF_INET6 && family != AF_INET) {
                const SOCKADDR_IN6 *in6 = (const SOCKADDR_IN6 *)&found[i];
                added = append_v6(&g, &in6->sin6_addr, in6->sin6_scope_id, 0);
            } else {
                continue;
            }
            if (!added)
                return WSA_NOT_ENOUGH_MEMORY;
            if (!first)
                first = added;
        }
        if (first && (flags & (AI_CANONNAME | AI_FQDN)))
            return set_canonname(first, node);
        return 0;
    }
    PADDRINFOW first = NULL;
    if (family != AF_INET && !(first = append_v6(&g, &in6_loopback, 0, 0)))
        return WSA_NOT_ENOUGH_MEMORY;
    IN_ADDR v4 = {.S_un.S_addr = htonl(INADDR_LOOPBACK)};
    PADDRINFOW added = NULL;
    if (family != AF_INET6 && !(added = append_v4(&g, v4, 0)))
        return WSA_NOT_ENOUGH_MEMORY;
    if (!first)
        first = added;
    if (flags & (AI_CANONNAME | AI_FQDN)) {
        char canonical[512];
        local_canonical(node, canonical, sizeof(canonical));
        return set_canonname(first, canonical);
    }
    return 0;
}

static int finish_lookup(int error, PADDRINFOW *result)
{
    if (!error && !*result)
        error = WSANO_DATA;
    if (error) {
        FreeAddrInfoW(*result);
        *result = NULL;
    }
    SetLastError(error);
    return error;
}

INT WSAAPI GetAddrInfoW(PCWSTR node, PCWSTR service, const ADDRINFOW *hints, PADDRINFOW *result)
{
    if (!result)
        return finish_lookup(WSAEINVAL, &(PADDRINFOW){NULL});
    *result = NULL;
    int error = WsCheckStarted();
    if (error)
        return finish_lookup(error, result);
    /* names outside ascii would need idn encoding, and no resolver could find them anyway. */
    char narrow_node[1024], narrow_service[256];
    if (node && !WsNarrow(node, narrow_node, sizeof(narrow_node)))
        return finish_lookup(WSAHOST_NOT_FOUND, result);
    if (service && !WsNarrow(service, narrow_service, sizeof(narrow_service)))
        return finish_lookup(WSATYPE_NOT_FOUND, result);
    error = lookup(node ? narrow_node : NULL, service ? narrow_service : NULL, hints, result);
    return finish_lookup(error, result);
}

INT WSAAPI getaddrinfo(PCSTR node, PCSTR service, const ADDRINFOA *hints, PADDRINFOA *result)
{
    if (!result)
        return finish_lookup(WSAEINVAL, &(PADDRINFOW){NULL});
    PADDRINFOW *wide = (PADDRINFOW *)result;
    *wide = NULL;
    int error = WsCheckStarted();
    if (error)
        return finish_lookup(error, wide);
    error = lookup(node, service, (const ADDRINFOW *)hints, wide);
    /* the canonical names come back in the ansi code page. */
    for (PADDRINFOW ai = *wide; !error && ai; ai = ai->ai_next) {
        if (!ai->ai_canonname)
            continue;
        int n = WideCharToMultiByte(CP_ACP, 0, ai->ai_canonname, -1, NULL, 0, NULL, NULL);
        char *a = n > 0 ? WsAlloc((SIZE_T)n) : NULL;
        if (!a) {
            error = WSA_NOT_ENOUGH_MEMORY;
            break;
        }
        WideCharToMultiByte(CP_ACP, 0, ai->ai_canonname, -1, a, n, NULL, NULL);
        WsFree(ai->ai_canonname);
        ai->ai_canonname = (PWSTR)a;
    }
    return finish_lookup(error, wide);
}

/* ------------------------------------------------------------------ getnameinfo */

static int name_info(const SOCKADDR *addr, socklen_t addrlen, char *host, DWORD hostlen, char *serv,
                     DWORD servlen, INT flags)
{
    int error = WsCheckStarted();
    if (error)
        return error;
    if (!addr)
        return WSAEFAULT;
    BOOL loopback;
    USHORT port;
    if (addr->sa_family == AF_INET) {
        if (addrlen < (socklen_t)sizeof(SOCKADDR_IN))
            return WSAEFAULT;
        loopback = WsIsLoopbackV4(&((const SOCKADDR_IN *)addr)->sin_addr);
        port = ((const SOCKADDR_IN *)addr)->sin_port;
    } else if (addr->sa_family == AF_INET6) {
        if (addrlen < (socklen_t)sizeof(SOCKADDR_IN6))
            return WSAEFAULT;
        loopback = WsIsLoopbackV6(&((const SOCKADDR_IN6 *)addr)->sin6_addr);
        port = ((const SOCKADDR_IN6 *)addr)->sin6_port;
    } else {
        return WSAEAFNOSUPPORT;
    }
    if ((!host || !hostlen) && (!serv || !servlen))
        return WSAHOST_NOT_FOUND;
    if (host && hostlen) {
        char text[512];
        if (!(flags & NI_NUMERICHOST) && loopback) {
            if (flags & NI_NOFQDN)
                WsHostName(text, sizeof(text));
            else
                WsFullHostName(text, sizeof(text));
        } else if (!(flags & NI_NUMERICHOST) && (flags & NI_NAMEREQD)) {
            return WSAHOST_NOT_FOUND;
        } else if (addr->sa_family == AF_INET) {
            WsFormatIpv4(&((const SOCKADDR_IN *)addr)->sin_addr, text);
        } else {
            const SOCKADDR_IN6 *in6 = (const SOCKADDR_IN6 *)addr;
            int n = WsFormatIpv6(&in6->sin6_addr, text);
            if (in6->sin6_scope_id)
                _snprintf(text + n, sizeof(text) - n - 1, "%%%u", in6->sin6_scope_id);
        }
        if (strlen(text) >= hostlen)
            return WSAEFAULT;
        memcpy(host, text, strlen(text) + 1);
    }
    if (serv && servlen) {
        char text[32];
        const char *name = NULL;
        if (!(flags & NI_NUMERICSERV))
            name = WsServiceByPort(ntohs(port), (flags & NI_DGRAM) ? "udp" : "tcp");
        if (name)
            copy_name(text, sizeof(text), name);
        else
            _snprintf(text, sizeof(text) - 1, "%u", ntohs(port));
        if (strlen(text) >= servlen)
            return WSAEFAULT;
        memcpy(serv, text, strlen(text) + 1);
    }
    return 0;
}

INT WSAAPI getnameinfo(const SOCKADDR *addr, socklen_t addrlen, PCHAR host, DWORD hostlen, PCHAR serv,
                       DWORD servlen, INT flags)
{
    int error = name_info(addr, addrlen, host, hostlen, serv, servlen, flags);
    SetLastError(error);
    return error;
}

INT WSAAPI GetNameInfoW(const SOCKADDR *addr, socklen_t addrlen, PWCHAR host, DWORD hostlen, PWCHAR serv,
                        DWORD servlen, INT flags)
{
    char narrow_host[NI_MAXHOST], narrow_serv[NI_MAXSERV];
    DWORD hl = host ? min(hostlen, (DWORD)sizeof(narrow_host)) : 0;
    DWORD sl = serv ? min(servlen, (DWORD)sizeof(narrow_serv)) : 0;
    int error = name_info(addr, addrlen, host ? narrow_host : NULL, hl, serv ? narrow_serv : NULL, sl, flags);
    if (!error && host && hostlen)
        WsWiden(narrow_host, host, hostlen);
    if (!error && serv && servlen)
        WsWiden(narrow_serv, serv, servlen);
    SetLastError(error);
    return error;
}
