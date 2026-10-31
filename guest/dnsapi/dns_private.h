/*
 * the dns client of dnsapi: the records programs get, the messages on the wire and the lookups that make the records.
 */
#ifndef MIME_DNSAPI_PRIVATE_H
#define MIME_DNSAPI_PRIVATE_H

#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>

typedef LONG DNS_STATUS;
typedef DWORD IP4_ADDRESS;

typedef struct {
    DWORD IP6Dword[4];
} IP6_ADDRESS;

typedef struct {
    DWORD Section : 2;
    DWORD Delete : 1;
    DWORD CharSet : 2;
    DWORD Unused : 3;
    DWORD Reserved : 24;
} DNS_RECORD_FLAGS;

typedef struct {
    PVOID pNameHost;
} DNS_PTR_DATA;

typedef struct {
    PVOID pNameExchange;
    WORD wPreference;
    WORD Pad;
} DNS_MX_DATA;

typedef struct {
    PVOID pNameTarget;
    WORD wPriority;
    WORD wWeight;
    WORD wPort;
    WORD Pad;
} DNS_SRV_DATA;

typedef struct {
    DWORD dwStringCount;
    PVOID pStringArray[1];
} DNS_TXT_DATA;

typedef struct {
    PVOID pNamePrimaryServer;
    PVOID pNameAdministrator;
    DWORD dwSerialNo;
    DWORD dwRefresh;
    DWORD dwRetry;
    DWORD dwExpire;
    DWORD dwDefaultTtl;
} DNS_SOA_DATA;

typedef struct {
    DWORD dwByteCount;
    BYTE Data[1];
} DNS_NULL_DATA;

typedef struct _DNS_RECORD {
    struct _DNS_RECORD *pNext;
    PVOID pName;
    WORD wType;
    WORD wDataLength;
    union {
        DWORD DW;
        DNS_RECORD_FLAGS S;
    } Flags;
    DWORD dwTtl;
    DWORD dwReserved;
    union {
        IP4_ADDRESS IpAddress;
        IP6_ADDRESS Ip6Address;
        DNS_PTR_DATA PTR;
        DNS_MX_DATA MX;
        DNS_SRV_DATA SRV;
        DNS_TXT_DATA TXT;
        DNS_SOA_DATA SOA;
        DNS_NULL_DATA Null;
    } Data;
} DNS_RECORD, *PDNS_RECORD;
#ifdef _WIN64
MIME_FIELD_AT(DNS_RECORD, Flags, 20);
MIME_FIELD_AT(DNS_RECORD, Data, 32);
#else
MIME_FIELD_AT(DNS_RECORD, Flags, 12);
MIME_FIELD_AT(DNS_RECORD, Data, 24);
#endif

typedef enum {
    DnsFreeFlat = 0,
    DnsFreeRecordList,
    DnsFreeParsedMessageFields
} DNS_FREE_TYPE;

enum { DnsCharSetUnknown, DnsCharSetUnicode, DnsCharSetUtf8, DnsCharSetAnsi };
enum { DnsSectionQuestion, DnsSectionAnswer, DnsSectionAuthority, DnsSectionAdditional };

#define DNS_TYPE_A 0x0001
#define DNS_TYPE_NS 0x0002
#define DNS_TYPE_MD 0x0003
#define DNS_TYPE_MF 0x0004
#define DNS_TYPE_CNAME 0x0005
#define DNS_TYPE_SOA 0x0006
#define DNS_TYPE_MB 0x0007
#define DNS_TYPE_MG 0x0008
#define DNS_TYPE_MR 0x0009
#define DNS_TYPE_PTR 0x000C
#define DNS_TYPE_HINFO 0x000D
#define DNS_TYPE_MX 0x000F
#define DNS_TYPE_TXT 0x0010
#define DNS_TYPE_AFSDB 0x0012
#define DNS_TYPE_AAAA 0x001C
#define DNS_TYPE_SRV 0x0021
#define DNS_TYPE_DNAME 0x0027
#define DNS_TYPE_OPT 0x0029
#define DNS_TYPE_ALL 0x00FF

#define DNS_QUERY_ACCEPT_TRUNCATED_RESPONSE 0x00000001
#define DNS_QUERY_USE_TCP_ONLY 0x00000002
#define DNS_QUERY_NO_RECURSION 0x00000004
#define DNS_QUERY_BYPASS_CACHE 0x00000008
#define DNS_QUERY_NO_WIRE_QUERY 0x00000010
#define DNS_QUERY_NO_LOCAL_NAME 0x00000020
#define DNS_QUERY_NO_HOSTS_FILE 0x00000040
#define DNS_QUERY_NO_NETBT 0x00000080
#define DNS_QUERY_WIRE_ONLY 0x00000100
#define DNS_QUERY_RETURN_MESSAGE 0x00000200
#define DNS_QUERY_MULTICAST_ONLY 0x00000400
#define DNS_QUERY_NO_MULTICAST 0x00000800
#define DNS_QUERY_TREAT_AS_FQDN 0x00001000
#define DNS_QUERY_ADDRCONFIG 0x00002000
#define DNS_QUERY_DUAL_ADDR 0x00004000
#define DNS_QUERY_DONT_RESET_TTL_VALUES 0x00100000
#define DNS_QUERY_DISABLE_IDN_ENCODING 0x00200000

#define DNS_ERROR_RCODE_FORMAT_ERROR 9001L
#define DNS_ERROR_RCODE_SERVER_FAILURE 9002L
#define DNS_ERROR_RCODE_NAME_ERROR 9003L
#define DNS_ERROR_RCODE_NOT_IMPLEMENTED 9004L
#define DNS_ERROR_RCODE_REFUSED 9005L
#define DNS_INFO_NO_RECORDS 9501L
#define DNS_ERROR_BAD_PACKET 9502L
#define DNS_REQUEST_PENDING 9506L
#define DNS_ERROR_NON_RFC_NAME 9556L
#define DNS_ERROR_INVALID_NAME_CHAR 9560L
#define DNS_ERROR_NUMERIC_NAME 9561L
#define DNS_ERROR_NO_DNS_SERVERS 9852L

/* ------------------------------------------------------------------ resolved records */

/* a record as the resolver holds it: names are UTF-8 text without the final dot. */
typedef struct DNS_RR {
    struct DNS_RR *next;
    char *name;
    char *host;
    char *admin;
    char **strings;
    BYTE *raw;
    DWORD ttl;
    DWORD soa[5];
    DWORD count;
    WORD type;
    WORD preference;
    WORD weight;
    WORD port;
    BYTE section;
    BYTE ip[16];
} DNS_RR;

typedef struct {
    DNS_RR *records;
    DNS_STATUS status;
    DWORD negative_ttl;
    BYTE *message;
    DWORD message_length;
} DNS_ANSWER;

void *DnsAllocate(SIZE_T size);
void DnsRelease(void *memory);
char *DnsDuplicate(const char *text);
void DnsRrFree(DNS_RR *list);
DNS_RR *DnsRrNew(const char *name, WORD type, DWORD ttl, BYTE section);
DNS_RR *DnsRrClone(const DNS_RR *list);
void DnsRecordsFree(PDNS_RECORD list);
PDNS_RECORD DnsRecordsBuild(const DNS_RR *list, int charset, DWORD elapsed);

/* ------------------------------------------------------------------ the wire */

int DnsWireQuery(BYTE *out, int capacity, WORD id, const char *name, WORD type, BOOL recursion);
/* parses a message into its status and records; the question must match the one asked. */
BOOL DnsWireParse(const BYTE *message, int length, WORD id, const char *name, WORD type, BOOL *truncated,
                  DNS_ANSWER *answer);
BOOL DnsNameEqual(const char *a, const char *b);

/* ------------------------------------------------------------------ lookups */

#define DNS_MAX_SERVERS 8

/* a server a program asks for, as a socket address. */
typedef struct {
    alignas(8) BYTE address[128];
    int length;
} DNS_SERVER;

DNS_STATUS DnsLookup(const char *name, WORD type, ULONGLONG options, const DNS_SERVER *servers, int server_count,
                     volatile LONG *cancelled, DNS_ANSWER *answer);
void DnsHostName(char *buffer, DWORD size);
void DnsFullName(char *buffer, DWORD size);
DNS_STATUS DnsCheckName(const char *name, BOOL *numeric);
void DnsAnswerFree(DNS_ANSWER *answer);
void DnsCacheFlush(const char *name);

#endif
