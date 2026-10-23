/* the private registry journal uses little endian fields and crc32 records. */
#ifndef MIME_CM_JOURNAL_FORMAT_H
#define MIME_CM_JOURNAL_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#define CM_JOURNAL_HEADER 56u
#define CM_JOURNAL_MAX_PATH 4094u
#define CM_JOURNAL_MAX_NAME 65534u
#define CM_JOURNAL_MAX_DATA (64u << 20)
#define CM_JOURNAL_MAX_FILE (256u << 20)
#define CM_JOURNAL_CREATE_KEY 1u
#define CM_JOURNAL_SET_VALUE 2u
#define CM_JOURNAL_DELETE_VALUE 3u
#define CM_JOURNAL_DELETE_KEY 4u
#define CM_JOURNAL_SET_SECURITY 5u
#define CM_JOURNAL_MAX_SECURITY (64u << 10)
#define CM_JOURNAL_LINK 1u

typedef struct CmJournalRecord {
    uint64_t sequence;
    uint32_t operation;
    uint32_t type;
    uint32_t path_bytes;
    uint32_t name_bytes;
    uint32_t data_bytes;
    uint64_t last_write;
} CmJournalRecord;

static inline uint32_t cm_journal_get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline uint64_t cm_journal_get64(const uint8_t *p)
{
    return cm_journal_get32(p) | (uint64_t)cm_journal_get32(p + 4) << 32;
}

static inline void cm_journal_put32(uint8_t *p, uint32_t n)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(n >> (i * 8));
}

static inline void cm_journal_put64(uint8_t *p, uint64_t n)
{
    cm_journal_put32(p, (uint32_t)n);
    cm_journal_put32(p + 4, (uint32_t)(n >> 32));
}

/* the operations this build applies; any other record is framed, checked and skipped. */
static inline bool cm_journal_known(uint32_t operation)
{
    return operation >= CM_JOURNAL_CREATE_KEY && operation <= CM_JOURNAL_SET_SECURITY;
}

static inline uint32_t cm_journal_size(const CmJournalRecord *r)
{
    return CM_JOURNAL_HEADER + r->path_bytes + r->name_bytes + r->data_bytes;
}

/* slicing by eight: every boot checks the whole log record by record. */
static inline const uint32_t (*cm_journal_crc_tables(void))[256]
{
    static uint32_t table[8][256];
    static uint32_t ready;
    if (!__atomic_load_n(&ready, __ATOMIC_ACQUIRE)) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? (c >> 1) ^ 0xedb88320u : c >> 1;
            table[0][i] = c;
        }
        for (uint32_t i = 0; i < 256; i++)
            for (int k = 1; k < 8; k++)
                table[k][i] = table[0][table[k - 1][i] & 255] ^ (table[k - 1][i] >> 8);
        __atomic_store_n(&ready, 1, __ATOMIC_RELEASE);
    }
    return table;
}

static inline uint32_t cm_journal_crc_update(const uint32_t (*t)[256], uint32_t crc, const uint8_t *p, size_t size)
{
    while (size >= 8) {
        uint32_t a = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        uint32_t b = (uint32_t)p[4] | (uint32_t)p[5] << 8 | (uint32_t)p[6] << 16 | (uint32_t)p[7] << 24;
        a ^= crc;
        crc = t[7][a & 255] ^ t[6][(a >> 8) & 255] ^ t[5][(a >> 16) & 255] ^ t[4][a >> 24] ^ t[3][b & 255] ^
              t[2][(b >> 8) & 255] ^ t[1][(b >> 16) & 255] ^ t[0][b >> 24];
        p += 8;
        size -= 8;
    }
    while (size--)
        crc = t[0][(crc ^ *p++) & 255] ^ (crc >> 8);
    return crc;
}

/* the crc field itself counts as four zero bytes. */
static inline uint32_t cm_journal_crc(const uint8_t *p, size_t size)
{
    const uint32_t (*t)[256] = cm_journal_crc_tables();
    static const uint8_t zeros[4] = {0, 0, 0, 0};
    uint32_t crc = UINT32_MAX;
    size_t head = size < 48 ? size : 48;
    crc = cm_journal_crc_update(t, crc, p, head);
    if (size > 48) {
        size_t field = size - 48 < 4 ? size - 48 : 4;
        crc = cm_journal_crc_update(t, crc, zeros, field);
    }
    if (size > 52)
        crc = cm_journal_crc_update(t, crc, p + 52, size - 52);
    return ~crc;
}

static inline void cm_journal_header(uint8_t *p, const CmJournalRecord *r)
{
    cm_journal_put32(p, 0x314a4d43u);
    cm_journal_put32(p + 4, cm_journal_size(r));
    cm_journal_put64(p + 8, r->sequence);
    cm_journal_put32(p + 16, r->operation);
    cm_journal_put32(p + 20, r->type);
    cm_journal_put32(p + 24, r->path_bytes);
    cm_journal_put32(p + 28, r->name_bytes);
    cm_journal_put32(p + 32, r->data_bytes);
    cm_journal_put32(p + 36, 0);
    cm_journal_put64(p + 40, r->last_write);
    cm_journal_put32(p + 48, 0);
    cm_journal_put32(p + 52, 0);
}

static inline void cm_journal_seal(uint8_t *p, size_t size)
{
    cm_journal_put32(p + 48, cm_journal_crc(p, size));
}

static inline bool cm_journal_decode(const uint8_t *p, size_t size, CmJournalRecord *r)
{
    if (size < CM_JOURNAL_HEADER || cm_journal_get32(p) != 0x314a4d43u)
        return false;
    r->sequence = cm_journal_get64(p + 8);
    r->operation = cm_journal_get32(p + 16);
    r->type = cm_journal_get32(p + 20);
    r->path_bytes = cm_journal_get32(p + 24);
    r->name_bytes = cm_journal_get32(p + 28);
    r->data_bytes = cm_journal_get32(p + 32);
    r->last_write = cm_journal_get64(p + 40);
    /* every record has this framing, so a reader can step over operations a later build adds. */
    if (!r->sequence || r->path_bytes > CM_JOURNAL_MAX_PATH || r->name_bytes > CM_JOURNAL_MAX_NAME ||
        r->data_bytes > CM_JOURNAL_MAX_DATA || cm_journal_get32(p + 4) != cm_journal_size(r))
        return false;
    if (!cm_journal_known(r->operation))
        return true;
    if (cm_journal_get32(p + 36) || cm_journal_get32(p + 52) || r->path_bytes < 22 || (r->path_bytes & 1) ||
        (r->name_bytes & 1))
        return false;
    switch (r->operation) {
    case CM_JOURNAL_CREATE_KEY:
        return !(r->type & ~CM_JOURNAL_LINK) && !r->name_bytes && !r->data_bytes;
    case CM_JOURNAL_SET_VALUE:
        return true;
    case CM_JOURNAL_DELETE_VALUE:
        return !r->type && !r->data_bytes;
    case CM_JOURNAL_DELETE_KEY:
        return !r->type && !r->name_bytes && !r->data_bytes;
    default:
        return !r->type && !r->name_bytes && r->data_bytes >= 20 && r->data_bytes <= CM_JOURNAL_MAX_SECURITY;
    }
}

static inline bool cm_journal_validate(const uint8_t *p, size_t size, uint64_t sequence)
{
    CmJournalRecord r;
    if (!cm_journal_decode(p, size, &r) || r.sequence != sequence || size != cm_journal_size(&r) ||
        cm_journal_get32(p + 48) != cm_journal_crc(p, size))
        return false;
    if (!cm_journal_known(r.operation))
        return true;
    static const char prefix[] = "\\REGISTRY\\";
    const uint8_t *path = p + CM_JOURNAL_HEADER;
    for (size_t i = 0; i < sizeof(prefix) - 1; i++)
        if (path[i * 2] != (uint8_t)prefix[i] || path[i * 2 + 1])
            return false;
    /* name units: no zero, no empty part between backslashes, no part longer than 255 units. */
    unsigned segment = 0;
    for (const uint8_t *c = path + (sizeof(prefix) - 1) * 2, *end = path + r.path_bytes; c < end; c += 2) {
        unsigned unit = c[0] | (unsigned)c[1] << 8;
        if (unit == '\\') {
            if (!segment)
                return false;
            segment = 0;
        } else if (!unit || ++segment > 255) {
            return false;
        }
    }
    return segment != 0;
}

#endif
