/*
 * dns messages: the question a lookup sends and the records its answer holds.
 */
#include "dns_private.h"

#define FLAG_QR 0x8000
#define FLAG_TC 0x0200
#define FLAG_RD 0x0100

static void put16(BYTE *p, WORD v)
{
    p[0] = (BYTE)(v >> 8);
    p[1] = (BYTE)v;
}

static WORD get16(const BYTE *p)
{
    return (WORD)((p[0] << 8) | p[1]);
}

static DWORD get32(const BYTE *p)
{
    return ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | p[3];
}

/* the name as labels, or the size it needs when it does not fit, or -1 when it is no name. */
static int encode_name(BYTE *out, int capacity, const char *name)
{
    int used = 0;
    if (!strcmp(name, ".") || !*name) {
        if (capacity < 1)
            return -1;
        out[0] = 0;
        return 1;
    }
    const char *p = name;
    while (*p) {
        const char *dot = strchr(p, '.');
        SIZE_T n = dot ? (SIZE_T)(dot - p) : strlen(p);
        if (!n || n > 63 || used + 1 + (int)n + 1 > 255 || used + 1 + (int)n + 1 > capacity)
            return -1;
        out[used++] = (BYTE)n;
        memcpy(out + used, p, n);
        used += (int)n;
        p += n;
        if (*p == '.')
            p++;
    }
    out[used++] = 0;
    return used;
}

int DnsWireQuery(BYTE *out, int capacity, WORD id, const char *name, WORD type, BOOL recursion)
{
    if (capacity < 12 + 4)
        return -1;
    memset(out, 0, 12);
    put16(out, id);
    put16(out + 2, recursion ? FLAG_RD : 0);
    put16(out + 4, 1);
    int n = encode_name(out + 12, capacity - 12 - 4, name);
    if (n < 0)
        return -1;
    put16(out + 12 + n, type);
    put16(out + 14 + n, 1);
    return 12 + n + 4;
}

/* reads a name at a place of the message into text, and where the record goes on after it. */
static BOOL read_name(const BYTE *m, int length, int at, char *text, int capacity, int *next)
{
    int used = 0, hops = 0, end = -1;
    for (;;) {
        if (at >= length)
            return FALSE;
        BYTE n = m[at];
        if ((n & 0xC0) == 0xC0) {
            if (at + 1 >= length)
                return FALSE;
            if (end < 0)
                end = at + 2;
            at = ((n & 0x3F) << 8) | m[at + 1];
            if (++hops > 64)
                return FALSE;
            continue;
        }
        if (n & 0xC0)
            return FALSE;
        at++;
        if (!n)
            break;
        if (at + n > length || used + n + 2 > capacity)
            return FALSE;
        if (used)
            text[used++] = '.';
        memcpy(text + used, m + at, n);
        used += n;
        at += n;
    }
    if (!used)
        text[used++] = '.';
    text[used] = 0;
    *next = end < 0 ? at : end;
    return TRUE;
}

BOOL DnsNameEqual(const char *a, const char *b)
{
    SIZE_T n = strlen(a), m = strlen(b);
    if (n > 1 && a[n - 1] == '.')
        n--;
    if (m > 1 && b[m - 1] == '.')
        m--;
    return n == m && !_strnicmp(a, b, n);
}

static BOOL address_type(WORD type)
{
    return type == DNS_TYPE_A || type == DNS_TYPE_AAAA;
}

/* a record of the message, or null when it is of no use; false when the message is broken. */
static BOOL read_record(const BYTE *m, int length, int *at, BYTE section, DNS_RR **out, WORD *type_out,
                        DWORD *ttl_out)
{
    char name[300];
    int p;
    *out = nullptr;
    if (!read_name(m, length, *at, name, sizeof(name), &p) || p + 10 > length)
        return FALSE;
    WORD type = get16(m + p);
    DWORD ttl = get32(m + p + 4);
    int rdlength = get16(m + p + 8);
    int rd = p + 10;
    if (rd + rdlength > length)
        return FALSE;
    *at = rd + rdlength;
    *type_out = type;
    *ttl_out = ttl;
    if (type == DNS_TYPE_OPT)
        return TRUE;
    DNS_RR *rr = DnsRrNew(name, type, ttl, section);
    if (!rr)
        return TRUE;
    char text[300], other[300];
    int q;
    BOOL ok = TRUE;
    const BYTE *d = m + rd;
    switch (type) {
    case DNS_TYPE_A:
        ok = rdlength == 4;
        if (ok)
            memcpy(rr->ip, d, 4);
        break;
    case DNS_TYPE_AAAA:
        ok = rdlength == 16;
        if (ok)
            memcpy(rr->ip, d, 16);
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
        ok = read_name(m, length, rd, text, sizeof(text), &q);
        if (ok)
            rr->host = DnsDuplicate(text);
        break;
    case DNS_TYPE_MX:
    case DNS_TYPE_AFSDB:
        ok = rdlength >= 3 && read_name(m, length, rd + 2, text, sizeof(text), &q);
        if (ok) {
            rr->preference = get16(d);
            rr->host = DnsDuplicate(text);
        }
        break;
    case DNS_TYPE_SRV:
        ok = rdlength >= 7 && read_name(m, length, rd + 6, text, sizeof(text), &q);
        if (ok) {
            rr->preference = get16(d);
            rr->weight = get16(d + 2);
            rr->port = get16(d + 4);
            rr->host = DnsDuplicate(text);
        }
        break;
    case DNS_TYPE_SOA:
        ok = read_name(m, length, rd, text, sizeof(text), &q) && read_name(m, length, q, other, sizeof(other), &q) &&
             q + 20 <= rd + rdlength;
        if (ok) {
            rr->host = DnsDuplicate(text);
            rr->admin = DnsDuplicate(other);
            for (int i = 0; i < 5; i++)
                rr->soa[i] = get32(m + q + 4 * i);
        }
        break;
    case DNS_TYPE_TXT:
    case DNS_TYPE_HINFO: {
        int count = 0;
        for (int o = 0; o < rdlength; o += 1 + d[o])
            count++;
        ok = TRUE;
        for (int o = 0, i = 0; o < rdlength && ok; i++) {
            if (o + 1 + d[o] > rdlength) {
                ok = FALSE;
                break;
            }
            if (!rr->strings)
                rr->strings = DnsAllocate(sizeof(char *) * (SIZE_T)(count ? count : 1));
            if (!rr->strings)
                break;
            char *s = DnsAllocate((SIZE_T)d[o] + 1);
            if (!s)
                break;
            memcpy(s, d + o + 1, d[o]);
            rr->strings[i] = s;
            rr->count = (DWORD)i + 1;
            o += 1 + d[o];
        }
        break;
    }
    default:
        rr->raw = DnsAllocate((SIZE_T)rdlength);
        rr->count = rr->raw ? (DWORD)rdlength : 0;
        if (rr->raw)
            memcpy(rr->raw, d, (SIZE_T)rdlength);
        break;
    }
    if (!ok) {
        DnsRrFree(rr);
        return FALSE;
    }
    if (type != DNS_TYPE_A && type != DNS_TYPE_AAAA && type != DNS_TYPE_SOA && type != DNS_TYPE_TXT &&
        type != DNS_TYPE_HINFO && !rr->raw && !rr->host) {
        /* a name that could not be copied. */
        DnsRrFree(rr);
        return TRUE;
    }
    *out = rr;
    return TRUE;
}

BOOL DnsWireParse(const BYTE *m, int length, WORD id, const char *name, WORD type, BOOL *truncated,
                  DNS_ANSWER *answer)
{
    if (length < 12 || get16(m) != id)
        return FALSE;
    WORD flags = get16(m + 2);
    if (!(flags & FLAG_QR) || ((flags >> 11) & 0xF))
        return FALSE;
    int rcode = flags & 0xF;
    int counts[3] = {get16(m + 6), get16(m + 8), get16(m + 10)};
    int questions = get16(m + 4);
    int at = 12;
    if (questions) {
        char asked[300];
        int next;
        if (!read_name(m, length, at, asked, sizeof(asked), &next) || next + 4 > length)
            return FALSE;
        if (!DnsNameEqual(asked, name) || get16(m + next) != type)
            return FALSE;
        at = next + 4;
        for (int i = 1; i < questions; i++) {
            if (!read_name(m, length, at, asked, sizeof(asked), &next) || next + 4 > length)
                return FALSE;
            at = next + 4;
        }
    } else if (!rcode) {
        return FALSE;
    }
    *truncated = (flags & FLAG_TC) != 0;
    answer->records = nullptr;
    answer->negative_ttl = 0;
    DNS_RR **tail = &answer->records;
    BOOL have_soa = FALSE;
    for (int section = 0; section < 3; section++) {
        for (int i = 0; i < counts[section]; i++) {
            DNS_RR *rr;
            WORD rtype;
            DWORD ttl;
            if (!read_record(m, length, &at, (BYTE)(section + 1), &rr, &rtype, &ttl)) {
                /* a message cut short keeps what came before the cut. */
                if (*truncated)
                    goto done;
                DnsRrFree(answer->records);
                answer->records = nullptr;
                return FALSE;
            }
            if (!rr)
                continue;
            BOOL keep = FALSE;
            if (section == 0)
                keep = rr->type == type || type == DNS_TYPE_ALL || rr->type == DNS_TYPE_CNAME ||
                       rr->type == DNS_TYPE_DNAME;
            else if (section == 2) {
                if (rr->type == DNS_TYPE_SOA && !have_soa) {
                    have_soa = TRUE;
                    DWORD least = rr->soa[4] < rr->ttl ? rr->soa[4] : rr->ttl;
                    answer->negative_ttl = least;
                }
            } else {
                keep = address_type(rr->type);
            }
            if (!keep) {
                DnsRrFree(rr);
                continue;
            }
            *tail = rr;
            tail = &rr->next;
        }
    }
done:
    if (rcode) {
        DnsRrFree(answer->records);
        answer->records = nullptr;
        answer->status = 9000 + rcode;
        return TRUE;
    }
    BOOL any = FALSE;
    for (DNS_RR *rr = answer->records; rr; rr = rr->next)
        any |= rr->section == DnsSectionAnswer;
    answer->status = any ? ERROR_SUCCESS : DNS_INFO_NO_RECORDS;
    if (!any) {
        DnsRrFree(answer->records);
        answer->records = nullptr;
    }
    return TRUE;
}
