/*
 * the records of a lookup: the resolver's own form and the DNS_RECORD lists programs get, each record one block.
 */
#include "dns_private.h"

void *DnsAllocate(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void DnsRelease(void *memory)
{
    if (memory)
        HeapFree(GetProcessHeap(), 0, memory);
}

char *DnsDuplicate(const char *text)
{
    SIZE_T n = strlen(text) + 1;
    char *copy = DnsAllocate(n);
    if (copy)
        memcpy(copy, text, n);
    return copy;
}

DNS_RR *DnsRrNew(const char *name, WORD type, DWORD ttl, BYTE section)
{
    DNS_RR *rr = DnsAllocate(sizeof(*rr));
    if (!rr)
        return nullptr;
    rr->name = DnsDuplicate(name);
    if (!rr->name) {
        DnsRelease(rr);
        return nullptr;
    }
    rr->type = type;
    rr->ttl = ttl;
    rr->section = section;
    return rr;
}

void DnsRrFree(DNS_RR *list)
{
    while (list) {
        DNS_RR *next = list->next;
        DnsRelease(list->name);
        DnsRelease(list->host);
        DnsRelease(list->admin);
        DnsRelease(list->raw);
        for (DWORD i = 0; list->strings && i < list->count; i++)
            DnsRelease(list->strings[i]);
        DnsRelease(list->strings);
        DnsRelease(list);
        list = next;
    }
}

DNS_RR *DnsRrClone(const DNS_RR *list)
{
    DNS_RR *head = nullptr, **tail = &head;
    for (; list; list = list->next) {
        DNS_RR *rr = DnsRrNew(list->name, list->type, list->ttl, list->section);
        if (!rr)
            goto fail;
        *tail = rr;
        tail = &rr->next;
        memcpy(rr->soa, list->soa, sizeof(rr->soa));
        memcpy(rr->ip, list->ip, sizeof(rr->ip));
        rr->preference = list->preference;
        rr->weight = list->weight;
        rr->port = list->port;
        rr->count = list->count;
        if (list->host && !(rr->host = DnsDuplicate(list->host)))
            goto fail;
        if (list->admin && !(rr->admin = DnsDuplicate(list->admin)))
            goto fail;
        if (list->type == DNS_TYPE_TXT) {
            if (!(rr->strings = DnsAllocate(sizeof(char *) * (list->count ? list->count : 1))))
                goto fail;
            for (DWORD i = 0; i < list->count; i++)
                if (!(rr->strings[i] = DnsDuplicate(list->strings[i]))) {
                    rr->count = i;
                    goto fail;
                }
        } else if (list->raw) {
            if (!(rr->raw = DnsAllocate(list->count ? list->count : 1)))
                goto fail;
            memcpy(rr->raw, list->raw, list->count);
        }
    }
    return head;
fail:
    DnsRrFree(head);
    return nullptr;
}

/* ------------------------------------------------------------------ records between processes */

/*
 * a list as the dns client service hands it to dnsapi: the count, then each record's fixed part and its texts, a text a
 * 16 bit length and its bytes, 0xFFFF for none. the strings of a TXT record or the raw bytes of another follow.
 */
typedef struct {
    BYTE *at, *end;
    DWORD size;
} RR_PACKER;

static void pack(RR_PACKER *p, const void *data, DWORD length)
{
    if (p->at && p->end - p->at >= (INT_PTR)length) {
        memcpy(p->at, data, length);
        p->at += length;
    } else {
        p->at = nullptr;
    }
    p->size += length;
}

static void pack_text(RR_PACKER *p, const char *text)
{
    WORD length = text ? (WORD)strlen(text) : 0xFFFF;
    pack(p, &length, sizeof(length));
    if (text)
        pack(p, text, length);
}

DWORD DnsRrPack(const DNS_RR *list, BYTE *buffer, DWORD capacity)
{
    RR_PACKER p = {buffer, buffer ? buffer + capacity : nullptr, 0};
    DWORD count = 0;
    for (const DNS_RR *rr = list; rr; rr = rr->next)
        count++;
    pack(&p, &count, sizeof(count));
    for (const DNS_RR *rr = list; rr; rr = rr->next) {
        pack(&p, &rr->type, sizeof(rr->type));
        pack(&p, &rr->section, sizeof(rr->section));
        pack(&p, &rr->ttl, sizeof(rr->ttl));
        pack(&p, &rr->count, sizeof(rr->count));
        pack(&p, &rr->preference, sizeof(rr->preference));
        pack(&p, &rr->weight, sizeof(rr->weight));
        pack(&p, &rr->port, sizeof(rr->port));
        pack(&p, rr->soa, sizeof(rr->soa));
        pack(&p, rr->ip, sizeof(rr->ip));
        pack_text(&p, rr->name);
        pack_text(&p, rr->host);
        pack_text(&p, rr->admin);
        BYTE kind = rr->type == DNS_TYPE_TXT ? 1 : rr->raw ? 2 : 0;
        pack(&p, &kind, sizeof(kind));
        for (DWORD i = 0; kind == 1 && i < rr->count; i++)
            pack_text(&p, rr->strings[i]);
        if (kind == 2)
            pack(&p, rr->raw, rr->count);
    }
    return p.size;
}

typedef struct {
    const BYTE *at, *end;
} RR_READER;

static BOOL unpack(RR_READER *r, void *out, DWORD length)
{
    if (r->end - r->at < (INT_PTR)length)
        return FALSE;
    memcpy(out, r->at, length);
    r->at += length;
    return TRUE;
}

static BOOL unpack_text(RR_READER *r, char **out)
{
    WORD length;
    *out = nullptr;
    if (!unpack(r, &length, sizeof(length)))
        return FALSE;
    if (length == 0xFFFF)
        return TRUE;
    if (!(*out = DnsAllocate(length + 1u)))
        return FALSE;
    return unpack(r, *out, length);
}

DNS_RR *DnsRrUnpack(const BYTE *buffer, DWORD size, BOOL *ok)
{
    RR_READER r = {buffer, buffer + size};
    DNS_RR *head = nullptr, **tail = &head;
    DWORD count = 0;
    *ok = unpack(&r, &count, sizeof(count));
    for (DWORD n = 0; *ok && n < count; n++) {
        DNS_RR *rr = DnsAllocate(sizeof(*rr));
        if (!rr) {
            *ok = FALSE;
            break;
        }
        *tail = rr;
        tail = &rr->next;
        BYTE kind = 0;
        *ok = unpack(&r, &rr->type, sizeof(rr->type)) && unpack(&r, &rr->section, sizeof(rr->section)) &&
              unpack(&r, &rr->ttl, sizeof(rr->ttl)) && unpack(&r, &rr->count, sizeof(rr->count)) &&
              unpack(&r, &rr->preference, sizeof(rr->preference)) && unpack(&r, &rr->weight, sizeof(rr->weight)) &&
              unpack(&r, &rr->port, sizeof(rr->port)) && unpack(&r, rr->soa, sizeof(rr->soa)) &&
              unpack(&r, rr->ip, sizeof(rr->ip)) && unpack_text(&r, &rr->name) && unpack_text(&r, &rr->host) &&
              unpack_text(&r, &rr->admin) && unpack(&r, &kind, sizeof(kind)) && rr->name && kind <= 2;
        if (*ok && kind == 1) {
            DWORD strings = rr->count;
            rr->count = 0;
            *ok = strings <= 4096 && (rr->strings = DnsAllocate(sizeof(char *) * (strings ? strings : 1)));
            for (; *ok && rr->count < strings; rr->count++)
                *ok = unpack_text(&r, &rr->strings[rr->count]) && rr->strings[rr->count];
        } else if (*ok && kind == 2) {
            *ok = rr->count <= size && (rr->raw = DnsAllocate(rr->count)) && unpack(&r, rr->raw, rr->count);
        }
    }
    if (!*ok) {
        DnsRrFree(head);
        return nullptr;
    }
    return head;
}

/* ------------------------------------------------------------------ the character sets */

/* the bytes a text takes in a character set, with its terminator. */
static SIZE_T text_size(const char *utf8, int charset)
{
    if (charset == DnsCharSetUnicode) {
        int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
        return (SIZE_T)(n > 0 ? n : 1) * sizeof(WCHAR);
    }
    if (charset == DnsCharSetAnsi) {
        int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
        WCHAR *wide = DnsAllocate((SIZE_T)(n > 0 ? n : 1) * sizeof(WCHAR));
        SIZE_T bytes = 1;
        if (wide && n > 0) {
            MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, n);
            int a = WideCharToMultiByte(CP_ACP, 0, wide, -1, nullptr, 0, nullptr, nullptr);
            bytes = (SIZE_T)(a > 0 ? a : 1);
        }
        DnsRelease(wide);
        return bytes;
    }
    return strlen(utf8) + 1;
}

/* writes a text in a character set at a place sized by text_size. */
static void text_put(void *place, SIZE_T size, const char *utf8, int charset)
{
    if (charset == DnsCharSetUnicode) {
        if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, place, (int)(size / sizeof(WCHAR))) <= 0)
            *(WCHAR *)place = 0;
    } else if (charset == DnsCharSetAnsi) {
        int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
        WCHAR *wide = DnsAllocate((SIZE_T)(n > 0 ? n : 1) * sizeof(WCHAR));
        *(char *)place = 0;
        if (wide && n > 0) {
            MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, n);
            WideCharToMultiByte(CP_ACP, 0, wide, -1, place, (int)size, nullptr, nullptr);
        }
        DnsRelease(wide);
    } else {
        memcpy(place, utf8, size);
    }
}

/* ------------------------------------------------------------------ DNS_RECORD */

#define HEADER offsetof(DNS_RECORD, Data)

/* the size of the data of a record, as wDataLength counts it. */
static SIZE_T data_size(const DNS_RR *rr)
{
    switch (rr->type) {
    case DNS_TYPE_A:
        return sizeof(IP4_ADDRESS);
    case DNS_TYPE_AAAA:
        return sizeof(IP6_ADDRESS);
    case DNS_TYPE_NS:
    case DNS_TYPE_MD:
    case DNS_TYPE_MF:
    case DNS_TYPE_CNAME:
    case DNS_TYPE_MB:
    case DNS_TYPE_MG:
    case DNS_TYPE_MR:
    case DNS_TYPE_PTR:
    case DNS_TYPE_DNAME:
        return sizeof(DNS_PTR_DATA);
    case DNS_TYPE_MX:
    case DNS_TYPE_AFSDB:
        return sizeof(DNS_MX_DATA);
    case DNS_TYPE_SRV:
        return sizeof(DNS_SRV_DATA);
    case DNS_TYPE_SOA:
        return sizeof(DNS_SOA_DATA);
    case DNS_TYPE_TXT:
    case DNS_TYPE_HINFO:
        return offsetof(DNS_TXT_DATA, pStringArray) + rr->count * sizeof(PVOID);
    default:
        return offsetof(DNS_NULL_DATA, Data) + rr->count;
    }
}

static SIZE_T align(SIZE_T n)
{
    return (n + sizeof(PVOID) - 1) & ~(sizeof(PVOID) - 1);
}

static PVOID take(BYTE **strings, const char *text, int charset)
{
    SIZE_T size = text_size(text, charset);
    PVOID place = *strings;
    text_put(place, size, text, charset);
    *strings += size;
    return place;
}

static PDNS_RECORD build_one(const DNS_RR *rr, int charset, DWORD elapsed)
{
    const char *empty = "";
    const char *host = rr->host ? rr->host : empty;
    const char *admin = rr->admin ? rr->admin : empty;
    SIZE_T strings = text_size(rr->name, charset);
    switch (rr->type) {
    case DNS_TYPE_A:
    case DNS_TYPE_AAAA:
        break;
    case DNS_TYPE_SOA:
        strings += text_size(host, charset) + text_size(admin, charset);
        break;
    case DNS_TYPE_TXT:
    case DNS_TYPE_HINFO:
        for (DWORD i = 0; i < rr->count; i++)
            strings += text_size(rr->strings[i], charset);
        break;
    case DNS_TYPE_NS:
    case DNS_TYPE_MD:
    case DNS_TYPE_MF:
    case DNS_TYPE_CNAME:
    case DNS_TYPE_MB:
    case DNS_TYPE_MG:
    case DNS_TYPE_MR:
    case DNS_TYPE_PTR:
    case DNS_TYPE_DNAME:
    case DNS_TYPE_MX:
    case DNS_TYPE_AFSDB:
    case DNS_TYPE_SRV:
        strings += text_size(host, charset);
        break;
    default:
        break;
    }
    SIZE_T data = data_size(rr);
    PDNS_RECORD r = DnsAllocate(align(HEADER + data) + strings);
    if (!r)
        return nullptr;
    BYTE *body = (BYTE *)r + HEADER;
    BYTE *place = (BYTE *)r + align(HEADER + data);
    r->pName = take(&place, rr->name, charset);
    r->wType = rr->type;
    r->wDataLength = (WORD)data;
    r->Flags.S.Section = rr->section;
    r->Flags.S.CharSet = (DWORD)charset;
    r->dwTtl = rr->ttl > elapsed ? rr->ttl - elapsed : 0;
    switch (rr->type) {
    case DNS_TYPE_A:
        memcpy(body, rr->ip, 4);
        break;
    case DNS_TYPE_AAAA:
        memcpy(body, rr->ip, 16);
        break;
    case DNS_TYPE_SOA: {
        DNS_SOA_DATA *soa = (DNS_SOA_DATA *)body;
        soa->pNamePrimaryServer = take(&place, host, charset);
        soa->pNameAdministrator = take(&place, admin, charset);
        soa->dwSerialNo = rr->soa[0];
        soa->dwRefresh = rr->soa[1];
        soa->dwRetry = rr->soa[2];
        soa->dwExpire = rr->soa[3];
        soa->dwDefaultTtl = rr->soa[4];
        break;
    }
    case DNS_TYPE_TXT:
    case DNS_TYPE_HINFO: {
        DNS_TXT_DATA *txt = (DNS_TXT_DATA *)body;
        txt->dwStringCount = rr->count;
        for (DWORD i = 0; i < rr->count; i++)
            txt->pStringArray[i] = take(&place, rr->strings[i], charset);
        break;
    }
    case DNS_TYPE_MX:
    case DNS_TYPE_AFSDB:
        ((DNS_MX_DATA *)body)->pNameExchange = take(&place, host, charset);
        ((DNS_MX_DATA *)body)->wPreference = rr->preference;
        break;
    case DNS_TYPE_SRV: {
        DNS_SRV_DATA *srv = (DNS_SRV_DATA *)body;
        srv->pNameTarget = take(&place, host, charset);
        srv->wPriority = rr->preference;
        srv->wWeight = rr->weight;
        srv->wPort = rr->port;
        break;
    }
    case DNS_TYPE_NS:
    case DNS_TYPE_MD:
    case DNS_TYPE_MF:
    case DNS_TYPE_CNAME:
    case DNS_TYPE_MB:
    case DNS_TYPE_MG:
    case DNS_TYPE_MR:
    case DNS_TYPE_PTR:
    case DNS_TYPE_DNAME:
        ((DNS_PTR_DATA *)body)->pNameHost = take(&place, host, charset);
        break;
    default:
        ((DNS_NULL_DATA *)body)->dwByteCount = rr->count;
        if (rr->raw)
            memcpy(((DNS_NULL_DATA *)body)->Data, rr->raw, rr->count);
        break;
    }
    return r;
}

/* the list a query returns, in the order of the resolver's list. */
PDNS_RECORD DnsRecordsBuild(const DNS_RR *list, int charset, DWORD elapsed)
{
    PDNS_RECORD head = nullptr, *tail = &head;
    for (; list; list = list->next) {
        PDNS_RECORD r = build_one(list, charset, elapsed);
        if (!r) {
            DnsRecordsFree(head);
            return nullptr;
        }
        *tail = r;
        tail = &r->pNext;
    }
    return head;
}

void DnsRecordsFree(PDNS_RECORD list)
{
    while (list) {
        PDNS_RECORD next = list->pNext;
        DnsRelease(list);
        list = next;
    }
}
