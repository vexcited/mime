/*
 * which records of the registry journal still matter. the journal only grows: every value ever written is in
 * it, and a replay at every boot reads all of it. what a record does is replace one thing, so only the last
 * record of each thing counts, and a key deleted takes everything older below it away:
 *
 *   - a value is set or deleted by name, and the last record of the name wins;
 *   - the descriptor of a key is set, the last record wins;
 *   - a key is created, which twice is once;
 *   - a key is deleted, and the records before it for the key or any key below it do nothing.
 *
 * the records are noted in order while the journal is replayed, and decided last to first, so that a deleted key
 * is known when the older records under it come. what is kept is in the order it had. the includer gives
 * CM_COMPACT_ALLOC and CM_COMPACT_FREE.
 */
#ifndef MIME_CM_JOURNAL_COMPACT_H
#define MIME_CM_JOURNAL_COMPACT_H

#include "journal_format.h"

typedef struct CmCompactRecord {
    uint32_t offset;
    uint32_t size;
    uint32_t key;
    uint8_t operation;
    uint8_t keep;
    uint16_t pad;
    uint64_t domain; /* what the record replaces, 0 for the deletion of a key */
} CmCompactRecord;

typedef struct CmCompactKey {
    uint64_t hash;
    uint32_t parent;
    uint32_t dead; /* a record for this key or one below it, older than the deletion of this key, does nothing */
} CmCompactKey;

typedef struct CmCompact {
    CmCompactRecord *records;
    uint32_t record_count, record_capacity;
    CmCompactKey *keys; /* number 0 is the root, above every path */
    uint32_t key_count, key_capacity;
    uint32_t *table; /* open addressing over keys by hash, key number + 1 */
    uint32_t table_size;
    bool failed;
    bool unknown; /* a record whose operation this build does not know: nothing is compacted */
} CmCompact;

#define CM_COMPACT_FNV_OFFSET 0xcbf29ce484222325ull
#define CM_COMPACT_FNV_PRIME 0x100000001b3ull

/* a unit of a name as the registry compares it: ascii letters in upper case. */
static inline uint32_t cm_compact_fold(uint32_t unit)
{
    return unit >= 'a' && unit <= 'z' ? unit - 32 : unit;
}

static inline uint64_t cm_compact_hash_unit(uint64_t hash, uint32_t unit)
{
    hash = (hash ^ (unit & 0xff)) * CM_COMPACT_FNV_PRIME;
    return (hash ^ (unit >> 8)) * CM_COMPACT_FNV_PRIME;
}

static inline uint64_t cm_compact_mix(uint64_t key, uint64_t tag, uint64_t detail)
{
    uint64_t h = (key + 1) * 0x9E3779B97F4A7C15ull ^ (tag << 56);
    h = (h ^ detail) * 0xC2B2AE3D27D4EB4Full;
    h ^= h >> 29;
    return h | 1;
}

static inline void cm_compact_free(CmCompact *c)
{
    if (!c)
        return;
    CM_COMPACT_FREE(c->records);
    CM_COMPACT_FREE(c->keys);
    CM_COMPACT_FREE(c->table);
    CM_COMPACT_FREE(c);
}

static inline CmCompact *cm_compact_create(void)
{
    CmCompact *c = CM_COMPACT_ALLOC(sizeof(*c));
    if (!c)
        return NULL;
    __builtin_memset(c, 0, sizeof(*c));
    c->record_capacity = 4096;
    c->key_capacity = 1024;
    c->table_size = 4096;
    c->records = CM_COMPACT_ALLOC((size_t)c->record_capacity * sizeof(*c->records));
    c->keys = CM_COMPACT_ALLOC((size_t)c->key_capacity * sizeof(*c->keys));
    c->table = CM_COMPACT_ALLOC((size_t)c->table_size * sizeof(*c->table));
    if (!c->records || !c->keys || !c->table) {
        cm_compact_free(c);
        return NULL;
    }
    __builtin_memset(c->table, 0, (size_t)c->table_size * sizeof(*c->table));
    c->key_count = 1;
    __builtin_memset(&c->keys[0], 0, sizeof(c->keys[0]));
    return c;
}

static inline bool cm_compact_grow_table(CmCompact *c)
{
    uint32_t size = c->table_size * 2;
    uint32_t *table = CM_COMPACT_ALLOC((size_t)size * sizeof(*table));
    if (!table)
        return false;
    __builtin_memset(table, 0, (size_t)size * sizeof(*table));
    for (uint32_t k = 1; k < c->key_count; k++) {
        uint32_t at = (uint32_t)(c->keys[k].hash >> 32) & (size - 1);
        while (table[at])
            at = (at + 1) & (size - 1);
        table[at] = k + 1;
    }
    CM_COMPACT_FREE(c->table);
    c->table = table;
    c->table_size = size;
    return true;
}

/* the number of the key with this hash of its path, made when there is none. */
static inline uint32_t cm_compact_key(CmCompact *c, uint64_t hash, uint32_t parent)
{
    uint32_t at = (uint32_t)(hash >> 32) & (c->table_size - 1);
    while (c->table[at]) {
        if (c->keys[c->table[at] - 1].hash == hash)
            return c->table[at] - 1;
        at = (at + 1) & (c->table_size - 1);
    }
    if (c->key_count == c->key_capacity) {
        uint32_t capacity = c->key_capacity * 2;
        CmCompactKey *keys = CM_COMPACT_ALLOC((size_t)capacity * sizeof(*keys));
        if (!keys) {
            c->failed = true;
            return 0;
        }
        __builtin_memcpy(keys, c->keys, (size_t)c->key_count * sizeof(*keys));
        CM_COMPACT_FREE(c->keys);
        c->keys = keys;
        c->key_capacity = capacity;
    }
    uint32_t id = c->key_count++;
    c->keys[id] = (CmCompactKey){hash, parent, 0};
    if ((uint64_t)c->key_count * 2 > c->table_size && !cm_compact_grow_table(c)) {
        c->failed = true;
        return 0;
    }
    /* the table may have grown: find the free slot again. */
    at = (uint32_t)(hash >> 32) & (c->table_size - 1);
    while (c->table[at])
        at = (at + 1) & (c->table_size - 1);
    c->table[at] = id + 1;
    return id;
}

/* notes the record at this offset of the journal: its path and name as the units of its payload. */
static inline void cm_compact_note(CmCompact *c, uint64_t offset, const CmJournalRecord *r, const uint8_t *payload)
{
    if (c->failed || c->unknown)
        return;
    if (!cm_journal_known(r->operation)) {
        c->unknown = true;
        return;
    }
    if (c->record_count == c->record_capacity) {
        uint32_t capacity = c->record_capacity * 2;
        CmCompactRecord *records = CM_COMPACT_ALLOC((size_t)capacity * sizeof(*records));
        if (!records) {
            c->failed = true;
            return;
        }
        __builtin_memcpy(records, c->records, (size_t)c->record_count * sizeof(*records));
        CM_COMPACT_FREE(c->records);
        c->records = records;
        c->record_capacity = capacity;
    }
    /* every prefix of the path that ends at a backslash is a key, and so is the whole path. */
    uint64_t hash = CM_COMPACT_FNV_OFFSET;
    uint32_t parent = 0;
    for (uint32_t i = 0; i + 1 < r->path_bytes; i += 2) {
        uint32_t unit = payload[i] | (uint32_t)payload[i + 1] << 8;
        if (unit == '\\' && i)
            parent = cm_compact_key(c, hash, parent);
        hash = cm_compact_hash_unit(hash, cm_compact_fold(unit));
    }
    uint32_t key = cm_compact_key(c, hash, parent);
    if (c->failed)
        return;
    CmCompactRecord *e = &c->records[c->record_count++];
    e->offset = (uint32_t)offset;
    e->size = cm_journal_size(r);
    e->key = key;
    e->operation = (uint8_t)r->operation;
    e->keep = 0;
    e->pad = 0;
    e->domain = 0;
    switch (r->operation) {
    case CM_JOURNAL_CREATE_KEY:
        e->domain = cm_compact_mix(key, 'C', r->type);
        break;
    case CM_JOURNAL_SET_VALUE:
    case CM_JOURNAL_DELETE_VALUE: {
        uint64_t name = CM_COMPACT_FNV_OFFSET;
        const uint8_t *text = payload + r->path_bytes;
        for (uint32_t i = 0; i + 1 < r->name_bytes; i += 2)
            name = cm_compact_hash_unit(name, cm_compact_fold(text[i] | (uint32_t)text[i + 1] << 8));
        e->domain = cm_compact_mix(key, 'V', name);
        break;
    }
    case CM_JOURNAL_SET_SECURITY:
        e->domain = cm_compact_mix(key, 'S', 0);
        break;
    default:
        break;
    }
}

/*
 * decides which records stay, last to first. returns false when it cannot, and true with the records marked and
 * the bytes of the ones that stay.
 */
static inline bool cm_compact_decide(CmCompact *c, uint64_t *kept_bytes, uint32_t *kept_count, uint32_t *largest)
{
    *kept_bytes = 0;
    *kept_count = 0;
    *largest = 0;
    if (c->failed || c->unknown)
        return false;
    uint32_t size = 1024;
    while (size < c->record_count * 2ull)
        size *= 2;
    uint64_t *seen = CM_COMPACT_ALLOC((size_t)size * sizeof(*seen));
    if (!seen)
        return false;
    __builtin_memset(seen, 0, (size_t)size * sizeof(*seen));
    for (uint32_t i = c->record_count; i--;) {
        CmCompactRecord *r = &c->records[i];
        bool dead = false;
        for (uint32_t k = r->key; k; k = c->keys[k].parent) {
            if (c->keys[k].dead) {
                dead = true;
                break;
            }
        }
        r->keep = 0;
        if (dead)
            continue;
        if (r->operation == CM_JOURNAL_DELETE_KEY) {
            r->keep = 1;
            c->keys[r->key].dead = 1;
        } else {
            uint32_t at = (uint32_t)(r->domain >> 20) & (size - 1);
            bool found = false;
            while (seen[at]) {
                if (seen[at] == r->domain) {
                    found = true;
                    break;
                }
                at = (at + 1) & (size - 1);
            }
            if (found)
                continue;
            seen[at] = r->domain;
            r->keep = 1;
        }
        *kept_bytes += r->size;
        (*kept_count)++;
        if (r->size > *largest)
            *largest = r->size;
    }
    CM_COMPACT_FREE(seen);
    return true;
}

#endif
