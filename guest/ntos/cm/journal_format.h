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

static inline uint32_t cm_journal_size(const CmJournalRecord *r)
{
    return CM_JOURNAL_HEADER + r->path_bytes + r->name_bytes + r->data_bytes;
}

static inline uint32_t cm_journal_crc(const uint8_t *p, size_t size)
{
    static const uint32_t table[16] = {
        0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu,
        0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
        0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
        0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu,
    };
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; i++) {
        crc ^= i >= 48 && i < 52 ? 0 : p[i];
        crc = (crc >> 4) ^ table[crc & 15];
        crc = (crc >> 4) ^ table[crc & 15];
    }
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
    if (size < CM_JOURNAL_HEADER || cm_journal_get32(p) != 0x314a4d43u ||
        cm_journal_get32(p + 36) || cm_journal_get32(p + 52))
        return false;
    r->sequence = cm_journal_get64(p + 8);
    r->operation = cm_journal_get32(p + 16);
    r->type = cm_journal_get32(p + 20);
    r->path_bytes = cm_journal_get32(p + 24);
    r->name_bytes = cm_journal_get32(p + 28);
    r->data_bytes = cm_journal_get32(p + 32);
    r->last_write = cm_journal_get64(p + 40);
    if (!r->sequence || r->path_bytes < 22 || r->path_bytes > CM_JOURNAL_MAX_PATH ||
        r->name_bytes > CM_JOURNAL_MAX_NAME || r->data_bytes > CM_JOURNAL_MAX_DATA ||
        (r->path_bytes & 1) || (r->name_bytes & 1) || cm_journal_get32(p + 4) != cm_journal_size(r))
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
    case CM_JOURNAL_SET_SECURITY:
        return !r->type && !r->name_bytes && r->data_bytes >= 20 && r->data_bytes <= CM_JOURNAL_MAX_SECURITY;
    default:
        return false;
    }
}

static inline bool cm_journal_validate(const uint8_t *p, size_t size, uint64_t sequence)
{
    CmJournalRecord r;
    if (!cm_journal_decode(p, size, &r) || r.sequence != sequence || size != cm_journal_size(&r) ||
        cm_journal_get32(p + 48) != cm_journal_crc(p, size))
        return false;
    static const char prefix[] = "\\REGISTRY\\";
    const uint8_t *path = p + CM_JOURNAL_HEADER;
    for (size_t i = 0; i < sizeof(prefix) - 1; i++)
        if (path[i * 2] != (uint8_t)prefix[i] || path[i * 2 + 1])
            return false;
    unsigned segment = 0;
    for (size_t i = (sizeof(prefix) - 1) * 2; i < r.path_bytes; i += 2) {
        uint16_t c = (uint16_t)path[i] | (uint16_t)path[i + 1] << 8;
        if (!c)
            return false;
        if (c == '\\') {
            if (!segment)
                return false;
            segment = 0;
        } else if (++segment > 255) {
            return false;
        }
    }
    return segment != 0;
}

#endif
