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
    {"echo", 7, "tcp", {nullptr}},
    {"echo", 7, "udp", {nullptr}},
    {"discard", 9, "tcp", {"sink", "null"}},
    {"discard", 9, "udp", {"sink", "null"}},
    {"systat", 11, "tcp", {"users"}},
    {"systat", 11, "udp", {"users"}},
    {"daytime", 13, "tcp", {nullptr}},
    {"daytime", 13, "udp", {nullptr}},
    {"qotd", 17, "tcp", {"quote"}},
    {"qotd", 17, "udp", {"quote"}},
    {"chargen", 19, "tcp", {"ttytst", "source"}},
    {"chargen", 19, "udp", {"ttytst", "source"}},
    {"ftp-data", 20, "tcp", {nullptr}},
    {"ftp", 21, "tcp", {nullptr}},
    {"ssh", 22, "tcp", {nullptr}},
    {"telnet", 23, "tcp", {nullptr}},
    {"smtp", 25, "tcp", {"mail"}},
    {"time", 37, "tcp", {"timserver"}},
    {"time", 37, "udp", {"timserver"}},
    {"rlp", 39, "udp", {"resource"}},
    {"nameserver", 42, "tcp", {"name"}},
    {"nameserver", 42, "udp", {"name"}},
    {"nicname", 43, "tcp", {"whois"}},
    {"domain", 53, "tcp", {nullptr}},
    {"domain", 53, "udp", {nullptr}},
    {"bootps", 67, "udp", {"dhcps"}},
    {"bootpc", 68, "udp", {"dhcpc"}},
    {"tftp", 69, "udp", {nullptr}},
    {"gopher", 70, "tcp", {nullptr}},
    {"finger", 79, "tcp", {nullptr}},
    {"http", 80, "tcp", {"www", "www-http"}},
    {"kerberos", 88, "tcp", {"krb5", "kerberos-sec"}},
    {"kerberos", 88, "udp", {"krb5", "kerberos-sec"}},
    {"hostname", 101, "tcp", {"hostnames"}},
    {"iso-tsap", 102, "tcp", {nullptr}},
    {"rtelnet", 107, "tcp", {nullptr}},
    {"pop2", 109, "tcp", {"postoffice"}},
    {"pop3", 110, "tcp", {nullptr}},
    {"sunrpc", 111, "tcp", {"rpcbind", "portmap"}},
    {"sunrpc", 111, "udp", {"rpcbind", "portmap"}},
    {"auth", 113, "tcp", {"ident", "tap"}},
    {"uucp-path", 117, "tcp", {nullptr}},
    {"sqlserv", 118, "tcp", {nullptr}},
    {"nntp", 119, "tcp", {"usenet"}},
    {"ntp", 123, "udp", {nullptr}},
    {"epmap", 135, "tcp", {"loc-srv"}},
    {"epmap", 135, "udp", {"loc-srv"}},
    {"netbios-ns", 137, "tcp", {"nbname"}},
    {"netbios-ns", 137, "udp", {"nbname"}},
    {"netbios-dgm", 138, "udp", {"nbdatagram"}},
    {"netbios-ssn", 139, "tcp", {"nbsession"}},
    {"imap", 143, "tcp", {"imap4"}},
    {"sql-net", 150, "tcp", {nullptr}},
    {"sqlsrv", 156, "tcp", {nullptr}},
    {"pcmail-srv", 158, "tcp", {nullptr}},
    {"snmp", 161, "udp", {nullptr}},
    {"snmptrap", 162, "udp", {"snmp-trap"}},
    {"print-srv", 170, "tcp", {nullptr}},
    {"bgp", 179, "tcp", {nullptr}},
    {"irc", 194, "tcp", {nullptr}},
    {"ipx", 213, "udp", {nullptr}},
    {"rtsps", 322, "tcp", {nullptr}},
    {"rtsps", 322, "udp", {nullptr}},
    {"mftp", 349, "tcp", {nullptr}},
    {"mftp", 349, "udp", {nullptr}},
    {"ldap", 389, "tcp", {nullptr}},
    {"https", 443, "tcp", {"MCom"}},
    {"https", 443, "udp", {"MCom"}},
    {"microsoft-ds", 445, "tcp", {nullptr}},
    {"microsoft-ds", 445, "udp", {nullptr}},
    {"kpasswd", 464, "tcp", {nullptr}},
    {"kpasswd", 464, "udp", {nullptr}},
    {"isakmp", 500, "udp", {"ike"}},
    {"crs", 507, "tcp", {nullptr}},
    {"crs", 507, "udp", {nullptr}},
    {"exec", 512, "tcp", {nullptr}},
    {"biff", 512, "udp", {"comsat"}},
    {"login", 513, "tcp", {nullptr}},
    {"who", 513, "udp", {"whod"}},
    {"cmd", 514, "tcp", {"shell"}},
    {"syslog", 514, "udp", {nullptr}},
    {"printer", 515, "tcp", {"spooler"}},
    {"talk", 517, "udp", {nullptr}},
    {"ntalk", 518, "udp", {nullptr}},
    {"efs", 520, "tcp", {nullptr}},
    {"router", 520, "udp", {"route", "routed"}},
    {"ulp", 522, "tcp", {nullptr}},
    {"ulp", 522, "udp", {nullptr}},
    {"timed", 525, "udp", {"timeserver"}},
    {"tempo", 526, "tcp", {"newdate"}},
    {"irc-serv", 529, "tcp", {nullptr}},
    {"irc-serv", 529, "udp", {nullptr}},
    {"courier", 530, "tcp", {"rpc"}},
    {"conference", 531, "tcp", {"chat"}},
    {"netnews", 532, "tcp", {"readnews"}},
    {"netwall", 533, "udp", {nullptr}},
    {"uucp", 540, "tcp", {"uucpd"}},
    {"klogin", 543, "tcp", {nullptr}},
    {"kshell", 544, "tcp", {"krcmd"}},
    {"dhcpv6-client", 546, "tcp", {nullptr}},
    {"dhcpv6-client", 546, "udp", {nullptr}},
    {"dhcpv6-server", 547, "tcp", {nullptr}},
    {"dhcpv6-server", 547, "udp", {nullptr}},
    {"afpovertcp", 548, "tcp", {nullptr}},
    {"afpovertcp", 548, "udp", {nullptr}},
    {"new-rwho", 550, "udp", {"new-who"}},
    {"rtsp", 554, "tcp", {nullptr}},
    {"rtsp", 554, "udp", {nullptr}},
    {"remotefs", 556, "tcp", {"rfs", "rfs_server"}},
    {"rmonitor", 560, "udp", {"rmonitord"}},
    {"monitor", 561, "udp", {nullptr}},
    {"nntps", 563, "tcp", {"snntp"}},
    {"nntps", 563, "udp", {"snntp"}},
    {"whoami", 565, "tcp", {nullptr}},
    {"whoami", 565, "udp", {nullptr}},
    {"ms-shuttle", 568, "tcp", {nullptr}},
    {"ms-shuttle", 568, "udp", {nullptr}},
    {"ms-rome", 569, "tcp", {nullptr}},
    {"ms-rome", 569, "udp", {nullptr}},
    {"http-rpc-epmap", 593, "tcp", {nullptr}},
    {"http-rpc-epmap", 593, "udp", {nullptr}},
    {"hmmp-ind", 612, "tcp", {nullptr}},
    {"hmmp-ind", 612, "udp", {nullptr}},
    {"hmmp-op", 613, "tcp", {nullptr}},
    {"hmmp-op", 613, "udp", {nullptr}},
    {"ldaps", 636, "tcp", {"sldap"}},
    {"doom", 666, "tcp", {nullptr}},
    {"doom", 666, "udp", {nullptr}},
    {"msexch-routing", 691, "tcp", {nullptr}},
    {"msexch-routing", 691, "udp", {nullptr}},
    {"kerberos-adm", 749, "tcp", {nullptr}},
    {"kerberos-adm", 749, "udp", {nullptr}},
    {"kerberos-iv", 750, "udp", {nullptr}},
    {"mdbs_daemon", 800, "tcp", {nullptr}},
    {"mdbs_daemon", 800, "udp", {nullptr}},
    {"ftps-data", 989, "tcp", {nullptr}},
    {"ftps", 990, "tcp", {nullptr}},
    {"telnets", 992, "tcp", {nullptr}},
    {"imaps", 993, "tcp", {nullptr}},
    {"ircs", 994, "tcp", {nullptr}},
    {"pop3s", 995, "tcp", {"spop3"}},
    {"pop3s", 995, "udp", {"spop3"}},
    {"kpop", 1109, "tcp", {nullptr}},
    {"nfsd-status", 1110, "tcp", {nullptr}},
    {"nfsd-keepalive", 1110, "udp", {nullptr}},
    {"nfa", 1155, "tcp", {nullptr}},
    {"nfa", 1155, "udp", {nullptr}},
    {"activesync", 1034, "tcp", {nullptr}},
    {"opsmgr", 1270, "tcp", {nullptr}},
    {"opsmgr", 1270, "udp", {nullptr}},
    {"ms-sql-s", 1433, "tcp", {nullptr}},
    {"ms-sql-s", 1433, "udp", {nullptr}},
    {"ms-sql-m", 1434, "tcp", {nullptr}},
    {"ms-sql-m", 1434, "udp", {nullptr}},
    {"ms-sna-server", 1477, "tcp", {nullptr}},
    {"ms-sna-server", 1477, "udp", {nullptr}},
    {"ms-sna-base", 1478, "tcp", {nullptr}},
    {"ms-sna-base", 1478, "udp", {nullptr}},
    {"wins", 1512, "tcp", {nullptr}},
    {"wins", 1512, "udp", {nullptr}},
    {"ingreslock", 1524, "tcp", {"ingres"}},
    {"stt", 1607, "tcp", {nullptr}},
    {"stt", 1607, "udp", {nullptr}},
    {"l2tp", 1701, "udp", {nullptr}},
    {"pptconference", 1711, "tcp", {nullptr}},
    {"pptconference", 1711, "udp", {nullptr}},
    {"pptp", 1723, "tcp", {nullptr}},
    {"msiccp", 1731, "tcp", {nullptr}},
    {"msiccp", 1731, "udp", {nullptr}},
    {"remote-winsock", 1745, "tcp", {nullptr}},
    {"remote-winsock", 1745, "udp", {nullptr}},
    {"ms-streaming", 1755, "tcp", {nullptr}},
    {"ms-streaming", 1755, "udp", {nullptr}},
    {"msmq", 1801, "tcp", {nullptr}},
    {"msmq", 1801, "udp", {nullptr}},
    {"radius", 1812, "udp", {nullptr}},
    {"radacct", 1813, "udp", {nullptr}},
    {"msnp", 1863, "tcp", {nullptr}},
    {"msnp", 1863, "udp", {nullptr}},
    {"ssdp", 1900, "tcp", {nullptr}},
    {"ssdp", 1900, "udp", {nullptr}},
    {"close-combat", 1944, "tcp", {nullptr}},
    {"close-combat", 1944, "udp", {nullptr}},
    {"nfsd", 2049, "udp", {"nfs"}},
    {"knetd", 2053, "tcp", {nullptr}},
    {"mzap", 2106, "tcp", {nullptr}},
    {"mzap", 2106, "udp", {nullptr}},
    {"qwave", 2177, "tcp", {nullptr}},
    {"qwave", 2177, "udp", {nullptr}},
    {"directplay", 2234, "tcp", {nullptr}},
    {"directplay", 2234, "udp", {nullptr}},
    {"ms-olap3", 2382, "tcp", {nullptr}},
    {"ms-olap3", 2382, "udp", {nullptr}},
    {"ms-olap4", 2383, "tcp", {nullptr}},
    {"ms-olap4", 2383, "udp", {nullptr}},
    {"ms-olap1", 2393, "tcp", {nullptr}},
    {"ms-olap1", 2393, "udp", {nullptr}},
    {"ms-olap2", 2394, "tcp", {nullptr}},
    {"ms-olap2", 2394, "udp", {nullptr}},
    {"ms-theater", 2460, "tcp", {nullptr}},
    {"ms-theater", 2460, "udp", {nullptr}},
    {"wlbs", 2504, "tcp", {nullptr}},
    {"wlbs", 2504, "udp", {nullptr}},
    {"ms-v-worlds", 2525, "tcp", {nullptr}},
    {"ms-v-worlds", 2525, "udp", {nullptr}},
    {"sms-rcinfo", 2701, "tcp", {nullptr}},
    {"sms-rcinfo", 2701, "udp", {nullptr}},
    {"sms-xfer", 2702, "tcp", {nullptr}},
    {"sms-xfer", 2702, "udp", {nullptr}},
    {"sms-chat", 2703, "tcp", {nullptr}},
    {"sms-chat", 2703, "udp", {nullptr}},
    {"sms-remctrl", 2704, "tcp", {nullptr}},
    {"sms-remctrl", 2704, "udp", {nullptr}},
    {"msolap-ptp2", 2725, "tcp", {nullptr}},
    {"msolap-ptp2", 2725, "udp", {nullptr}},
    {"icslap", 2869, "tcp", {nullptr}},
    {"icslap", 2869, "udp", {nullptr}},
    {"cifs", 3020, "tcp", {nullptr}},
    {"cifs", 3020, "udp", {nullptr}},
    {"xbox", 3074, "tcp", {nullptr}},
    {"xbox", 3074, "udp", {nullptr}},
    {"ms-dotnetster", 3126, "tcp", {nullptr}},
    {"ms-dotnetster", 3126, "udp", {nullptr}},
    {"ms-rule-engine", 3132, "tcp", {nullptr}},
    {"ms-rule-engine", 3132, "udp", {nullptr}},
    {"msft-gc", 3268, "tcp", {nullptr}},
    {"msft-gc", 3268, "udp", {nullptr}},
    {"msft-gc-ssl", 3269, "tcp", {nullptr}},
    {"msft-gc-ssl", 3269, "udp", {nullptr}},
    {"ms-cluster-net", 3343, "tcp", {nullptr}},
    {"ms-cluster-net", 3343, "udp", {nullptr}},
    {"ms-wbt-server", 3389, "tcp", {nullptr}},
    {"ms-wbt-server", 3389, "udp", {nullptr}},
    {"ms-la", 3535, "tcp", {nullptr}},
    {"ms-la", 3535, "udp", {nullptr}},
    {"pnrp-port", 3540, "tcp", {nullptr}},
    {"pnrp-port", 3540, "udp", {nullptr}},
    {"teredo", 3544, "tcp", {nullptr}},
    {"teredo", 3544, "udp", {nullptr}},
    {"p2pgroup", 3587, "tcp", {nullptr}},
    {"p2pgroup", 3587, "udp", {nullptr}},
    {"ws-discovery", 3702, "udp", {nullptr}},
    {"dvcprov-port", 3776, "tcp", {nullptr}},
    {"dvcprov-port", 3776, "udp", {nullptr}},
    {"msfw-control", 3847, "tcp", {nullptr}},
    {"msdts1", 3882, "tcp", {nullptr}},
    {"sdp-portmapper", 3935, "tcp", {nullptr}},
    {"sdp-portmapper", 3935, "udp", {nullptr}},
    {"net-device", 4350, "tcp", {nullptr}},
    {"net-device", 4350, "udp", {nullptr}},
    {"ipsec-msft", 4500, "tcp", {nullptr}},
    {"ipsec-msft", 4500, "udp", {nullptr}},
    {"llmnr", 5355, "tcp", {nullptr}},
    {"llmnr", 5355, "udp", {nullptr}},
    {"rrac", 5678, "tcp", {nullptr}},
    {"rrac", 5678, "udp", {nullptr}},
    {"dccm", 5679, "tcp", {nullptr}},
    {"dccm", 5679, "udp", {nullptr}},
    {"ms-licensing", 5720, "tcp", {nullptr}},
    {"ms-licensing", 5720, "udp", {nullptr}},
    {"directplay8", 6073, "tcp", {nullptr}},
    {"directplay8", 6073, "udp", {nullptr}},
    {"man", 9535, "tcp", {nullptr}},
    {"rasadv", 9753, "tcp", {nullptr}},
    {"rasadv", 9753, "udp", {nullptr}},
    {"imip-channels", 11320, "tcp", {nullptr}},
    {"imip-channels", 11320, "udp", {nullptr}},
    {"directplaysrvr", 47624, "tcp", {nullptr}},
    {"directplaysrvr", 47624, "udp", {nullptr}},
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
    return nullptr;
}

static const WS_SERVICE *find_service_port(USHORT port, const char *proto)
{
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(services); i++) {
        if (services[i].port == port && proto_matches(proto, services[i].proto))
            return &services[i];
    }
    return nullptr;
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
    return s ? s->name : nullptr;
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
        return nullptr;
    }
    int n = 0;
    copy_name(t->serv_names[0], sizeof(t->serv_names[0]), s->name);
    for (int i = 0; i < 3 && s->aliases[i]; i++) {
        copy_name(t->serv_names[i + 1], sizeof(t->serv_names[0]), s->aliases[i]);
        t->serv_aliases[n++] = t->serv_names[i + 1];
    }
    t->serv_aliases[n] = nullptr;
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
        return nullptr;
    }
    if (!name) {
        SetLastError(WSAEINVAL);
        return nullptr;
    }
    const WS_SERVICE *s = find_service_name(name, proto);
    if (!s) {
        SetLastError(WSANO_DATA);
        return nullptr;
    }
    return fill_servent(s);
}

struct servent *WSAAPI getservbyport(int port, const char *proto)
{
    int error = WsCheckStarted();
    if (error) {
        SetLastError(error);
        return nullptr;
    }
    const WS_SERVICE *s = find_service_port(ntohs((u_short)port), proto);
    if (!s) {
        SetLastError(WSANO_DATA);
        return nullptr;
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
        return nullptr;
    }
    copy_name(t->proto_names[0], sizeof(t->proto_names[0]), p->name);
    copy_name(t->proto_names[1], sizeof(t->proto_names[1]), p->alias);
    t->proto_aliases[0] = t->proto_names[1];
    t->proto_aliases[1] = nullptr;
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
        return nullptr;
    }
    if (!name) {
        SetLastError(WSAEINVAL);
        return nullptr;
    }
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(protocols); i++) {
        if (!_stricmp(protocols[i].name, name) || !_stricmp(protocols[i].alias, name))
            return fill_protoent(&protocols[i]);
    }
    SetLastError(WSANO_DATA);
    return nullptr;
}

struct protoent *WSAAPI getprotobynumber(int number)
{
    int error = WsCheckStarted();
    if (error) {
        SetLastError(error);
        return nullptr;
    }
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(protocols); i++) {
        if (protocols[i].number == number)
            return fill_protoent(&protocols[i]);
    }
    SetLastError(WSANO_DATA);
    return nullptr;
}

/* ------------------------------------------------------------------ gethostbyname and gethostbyaddr */

static struct hostent *fill_hostent(const char *name, short family, const void *addresses, int count)
{
    WS_THREAD *t = WsThread();
    if (!t) {
        SetLastError(WSA_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    int length = family == AF_INET6 ? 16 : 4;
    copy_name(t->host_name, sizeof(t->host_name), name);
    t->host_aliases[0] = nullptr;
    for (int i = 0; i < count && i < WS_HOST_ADDRESSES; i++) {
        memcpy(t->host_addresses[i], (const BYTE *)addresses + i * length, length);
        t->host_list[i] = (char *)t->host_addresses[i];
    }
    t->host_list[count] = nullptr;
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
        return nullptr;
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
            return nullptr;
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
        return nullptr;
    }
    if (!addr) {
        SetLastError(WSAEFAULT);
        return nullptr;
    }
    BOOL loopback;
    if (type == AF_INET) {
        if (len < 4) {
            SetLastError(WSAEFAULT);
            return nullptr;
        }
        IN_ADDR a;
        memcpy(&a, addr, 4);
        loopback = WsIsLoopbackV4(&a);
    } else if (type == AF_INET6) {
        if (len < 16) {
            SetLastError(WSAEFAULT);
            return nullptr;
        }
        IN6_ADDR a;
        memcpy(&a, addr, 16);
        loopback = WsIsLoopbackV6(&a);
    } else {
        SetLastError(WSAEAFNOSUPPORT);
        return nullptr;
    }
    if (!loopback) {
        SetLastError(WSAHOST_NOT_FOUND);
        return nullptr;
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
        return nullptr;
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
        return nullptr;
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
    const char *proto = socktype == SOCK_STREAM ? "tcp" : socktype == SOCK_DGRAM ? "udp" : nullptr;
    USHORT p;
    if (!WsServiceByName(service, proto, &p, nullptr))
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
    *result = nullptr;
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
    PADDRINFOW numeric = nullptr;
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
        PADDRINFOW first = nullptr;
        for (int i = 0; i < n; i++) {
            PADDRINFOW added = nullptr;
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
    PADDRINFOW first = nullptr;
    if (family != AF_INET && !(first = append_v6(&g, &in6_loopback, 0, 0)))
        return WSA_NOT_ENOUGH_MEMORY;
    IN_ADDR v4 = {.S_un.S_addr = htonl(INADDR_LOOPBACK)};
    PADDRINFOW added = nullptr;
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
        *result = nullptr;
    }
    SetLastError(error);
    return error;
}

INT WSAAPI GetAddrInfoW(PCWSTR node, PCWSTR service, const ADDRINFOW *hints, PADDRINFOW *result)
{
    if (!result)
        return finish_lookup(WSAEINVAL, &(PADDRINFOW){nullptr});
    *result = nullptr;
    int error = WsCheckStarted();
    if (error)
        return finish_lookup(error, result);
    /* names outside ascii would need idn encoding, and no resolver could find them anyway. */
    char narrow_node[1024], narrow_service[256];
    if (node && !WsNarrow(node, narrow_node, sizeof(narrow_node)))
        return finish_lookup(WSAHOST_NOT_FOUND, result);
    if (service && !WsNarrow(service, narrow_service, sizeof(narrow_service)))
        return finish_lookup(WSATYPE_NOT_FOUND, result);
    error = lookup(node ? narrow_node : nullptr, service ? narrow_service : nullptr, hints, result);
    return finish_lookup(error, result);
}

INT WSAAPI getaddrinfo(PCSTR node, PCSTR service, const ADDRINFOA *hints, PADDRINFOA *result)
{
    if (!result)
        return finish_lookup(WSAEINVAL, &(PADDRINFOW){nullptr});
    PADDRINFOW *wide = (PADDRINFOW *)result;
    *wide = nullptr;
    int error = WsCheckStarted();
    if (error)
        return finish_lookup(error, wide);
    error = lookup(node, service, (const ADDRINFOW *)hints, wide);
    /* the canonical names come back in the ansi code page. */
    for (PADDRINFOW ai = *wide; !error && ai; ai = ai->ai_next) {
        if (!ai->ai_canonname)
            continue;
        int n = WideCharToMultiByte(CP_ACP, 0, ai->ai_canonname, -1, nullptr, 0, nullptr, nullptr);
        char *a = n > 0 ? WsAlloc((SIZE_T)n) : nullptr;
        if (!a) {
            error = WSA_NOT_ENOUGH_MEMORY;
            break;
        }
        WideCharToMultiByte(CP_ACP, 0, ai->ai_canonname, -1, a, n, nullptr, nullptr);
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
        const char *name = nullptr;
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
    int error = name_info(addr, addrlen, host ? narrow_host : nullptr, hl, serv ? narrow_serv : nullptr, sl, flags);
    if (!error && host && hostlen)
        WsWiden(narrow_host, host, hostlen);
    if (!error && serv && servlen)
        WsWiden(narrow_serv, serv, servlen);
    SetLastError(error);
    return error;
}
