/*
 * the llvm bitstream reader. records are read with the abbreviations of
 * their block and of blockinfo, and kept as 64-bit operands, code first.
 */
#include "bitcode.h"

enum { ABBREV_END_BLOCK, ABBREV_ENTER_SUBBLOCK, ABBREV_DEFINE, ABBREV_UNABBREVIATED, ABBREV_FIRST };
enum { OP_LITERAL, OP_FIXED, OP_VBR, OP_ARRAY, OP_CHAR6, OP_BLOB };
enum { BLOCKINFO_SETBID = 1 };

typedef struct AbbrevOp {
    uint8_t kind;
    uint64_t value;
} AbbrevOp;

typedef struct Abbrev {
    uint32_t count;
    AbbrevOp *ops;
} Abbrev;

typedef struct AbbrevList {
    Abbrev *items;
    uint32_t count, capacity;
} AbbrevList;

/* abbreviations blockinfo gives to blocks of an id. */
typedef struct BlockInfo {
    uint32_t id;
    AbbrevList abbrevs;
} BlockInfo;

typedef struct Reader {
    const uint8_t *data;
    size_t bits, pos;
    BcArena *arena;
    BlockInfo *info;
    uint32_t info_count, info_capacity;
    int failed;
} Reader;

#define CHUNK_SIZE (64 << 10)

void *bc_arena_alloc(BcArena *a, size_t size)
{
    size = (size + 15) & ~(size_t)15;
    if ((size_t)(a->end - a->at) < size) {
        size_t chunk = size + 16 > CHUNK_SIZE ? size + 16 : CHUNK_SIZE;
        uint8_t *p = mime_alloc(a->alloc, chunk);
        if (!p)
            return nullptr;
        *(void **)p = a->chunks;
        a->chunks = p;
        a->at = p + 16;
        a->end = p + chunk;
    }
    void *p = a->at;
    a->at += size;
    __builtin_memset(p, 0, size);
    return p;
}

void bc_arena_free(BcArena *a)
{
    void *p = a->chunks;
    while (p) {
        void *next = *(void **)p;
        mime_free(a->alloc, p);
        p = next;
    }
    a->chunks = nullptr;
    a->at = a->end = nullptr;
}

BcMark bc_arena_mark(const BcArena *a) { return (BcMark){a->chunks, a->at, a->end}; }

void bc_arena_release(BcArena *a, BcMark mark)
{
    while (a->chunks && a->chunks != mark.chunks) {
        void *next = *(void **)a->chunks;
        mime_free(a->alloc, a->chunks);
        a->chunks = next;
    }
    a->at = mark.at;
    a->end = mark.end;
}

/* doubles the capacity in the arena. */
static void *grow(BcArena *a, void *items, uint32_t count, uint32_t *capacity, size_t size)
{
    if (count < *capacity)
        return items;
    uint32_t want = *capacity ? *capacity * 2 : 8;
    void *p = bc_arena_alloc(a, want * size);
    if (!p)
        return nullptr;
    if (items)
        __builtin_memcpy(p, items, count * size);
    *capacity = want;
    return p;
}

static uint64_t fixed(Reader *r, uint32_t width)
{
    if (!width)
        return 0;
    if (width > 64 || r->pos + width > r->bits) {
        r->failed = 1;
        r->pos = r->bits;
        return 0;
    }
    uint64_t v = 0;
    for (uint32_t got = 0; got < width;) {
        size_t byte = r->pos >> 3;
        uint32_t shift = r->pos & 7, take = 8 - shift;
        if (take > width - got)
            take = width - got;
        v |= (uint64_t)((r->data[byte] >> shift) & ((1u << take) - 1)) << got;
        got += take;
        r->pos += take;
    }
    return v;
}

static uint64_t vbr(Reader *r, uint32_t width)
{
    if (width < 2 || width > 32) {
        r->failed = 1;
        return 0;
    }
    uint64_t v = 0, hi = 1ull << (width - 1);
    for (uint32_t shift = 0; shift < 64 + width; shift += width - 1) {
        uint64_t piece = fixed(r, width);
        if (r->failed)
            return 0;
        if (shift < 64)
            v |= (piece & (hi - 1)) << shift;
        if (!(piece & hi))
            return v;
    }
    r->failed = 1;
    return 0;
}

static void align32(Reader *r)
{
    r->pos = (r->pos + 31) & ~(size_t)31;
    if (r->pos > r->bits)
        r->failed = 1;
}

static BlockInfo *block_info(Reader *r, uint32_t id, int create)
{
    for (uint32_t i = 0; i < r->info_count; i++)
        if (r->info[i].id == id)
            return &r->info[i];
    if (!create)
        return nullptr;
    r->info = grow(r->arena, r->info, r->info_count, &r->info_capacity, sizeof(BlockInfo));
    if (!r->info) {
        r->failed = 1;
        return nullptr;
    }
    BlockInfo *b = &r->info[r->info_count++];
    __builtin_memset(b, 0, sizeof(*b));
    b->id = id;
    return b;
}

static void add_abbrev(Reader *r, AbbrevList *list, Abbrev a)
{
    list->items = grow(r->arena, list->items, list->count, &list->capacity, sizeof(Abbrev));
    if (!list->items) {
        r->failed = 1;
        return;
    }
    list->items[list->count++] = a;
}

static void define_abbrev(Reader *r, AbbrevList *list)
{
    uint32_t count = (uint32_t)vbr(r, 5);
    if (r->failed || count > 1024) {
        r->failed = 1;
        return;
    }
    Abbrev a = {0, bc_arena_alloc(r->arena, (count ? count : 1) * sizeof(AbbrevOp))};
    if (!a.ops) {
        r->failed = 1;
        return;
    }
    for (uint32_t i = 0; i < count && !r->failed; i++) {
        AbbrevOp op = {};
        if (fixed(r, 1)) {
            op.kind = OP_LITERAL;
            op.value = vbr(r, 8);
        } else {
            op.kind = (uint8_t)fixed(r, 3);
            if (op.kind == OP_FIXED || op.kind == OP_VBR)
                op.value = vbr(r, 5);
            else if (op.kind != OP_ARRAY && op.kind != OP_CHAR6 && op.kind != OP_BLOB)
                r->failed = 1;
        }
        a.ops[a.count++] = op;
    }
    add_abbrev(r, list, a);
}

static uint64_t scalar(Reader *r, const AbbrevOp *op)
{
    static const char CHAR6[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._";
    switch (op->kind) {
    case OP_LITERAL: return op->value;
    case OP_FIXED: return fixed(r, (uint32_t)op->value);
    case OP_VBR: return vbr(r, (uint32_t)op->value);
    case OP_CHAR6: return (uint8_t)CHAR6[fixed(r, 6)];
    default: r->failed = 1; return 0;
    }
}

typedef struct Ops {
    uint64_t *items;
    uint32_t count, capacity;
} Ops;

static void push(Reader *r, Ops *ops, uint64_t v)
{
    ops->items = grow(r->arena, ops->items, ops->count, &ops->capacity, sizeof(uint64_t));
    if (!ops->items) {
        r->failed = 1;
        return;
    }
    ops->items[ops->count++] = v;
}

static BcRecord *read_abbreviated(Reader *r, const Abbrev *a)
{
    BcRecord *rec = bc_arena_alloc(r->arena, sizeof(BcRecord));
    Ops ops = {};
    if (!rec) {
        r->failed = 1;
        return nullptr;
    }
    for (uint32_t i = 0; i < a->count && !r->failed; i++) {
        const AbbrevOp *op = &a->ops[i];
        if (op->kind == OP_ARRAY) {
            if (i + 2 != a->count) {
                r->failed = 1;
                break;
            }
            uint64_t n = vbr(r, 6);
            const AbbrevOp *element = &a->ops[++i];
            for (uint64_t k = 0; k < n && !r->failed; k++)
                push(r, &ops, scalar(r, element));
        } else if (op->kind == OP_BLOB) {
            uint64_t n = vbr(r, 6);
            align32(r);
            if (r->failed || n > (r->bits - r->pos) / 8) {
                r->failed = 1;
                break;
            }
            rec->blob = r->data + r->pos / 8;
            rec->blob_size = (uint32_t)n;
            r->pos += n * 8;
            align32(r);
        } else {
            push(r, &ops, scalar(r, op));
        }
    }
    if (r->failed || !ops.count) {
        r->failed = 1;
        return nullptr;
    }
    rec->code = (uint32_t)ops.items[0];
    rec->ops = ops.items + 1;
    rec->count = ops.count - 1;
    return rec;
}

static BcRecord *read_unabbreviated(Reader *r)
{
    BcRecord *rec = bc_arena_alloc(r->arena, sizeof(BcRecord));
    if (!rec) {
        r->failed = 1;
        return nullptr;
    }
    rec->code = (uint32_t)vbr(r, 6);
    uint64_t n = vbr(r, 6);
    if (r->failed || n > (r->bits - r->pos)) {
        r->failed = 1;
        return nullptr;
    }
    rec->count = (uint32_t)n;
    rec->ops = bc_arena_alloc(r->arena, (n ? n : 1) * sizeof(uint64_t));
    if (!rec->ops) {
        r->failed = 1;
        return nullptr;
    }
    for (uint64_t i = 0; i < n && !r->failed; i++)
        rec->ops[i] = vbr(r, 6);
    return rec;
}

typedef struct Entries {
    BcEntry *items;
    uint32_t count, capacity;
} Entries;

static void add_entry(Reader *r, Entries *e, BcRecord *record, BcBlock *block)
{
    e->items = grow(r->arena, e->items, e->count, &e->capacity, sizeof(BcEntry));
    if (!e->items) {
        r->failed = 1;
        return;
    }
    e->items[e->count++] = (BcEntry){record, block};
}

static BcBlock *read_block(Reader *r, uint32_t id, uint32_t width, int depth);

/* up to the block's end, or the stream's end at the top level. */
static void read_entries(Reader *r, BcBlock *block, uint32_t width, int depth)
{
    AbbrevList local = {};
    BlockInfo *setting = nullptr;
    Entries entries = {};
    while (!r->failed) {
        if (depth == 0 && r->bits - r->pos < 32)
            break;
        uint32_t abbrev = (uint32_t)fixed(r, width);
        if (r->failed)
            break;
        if (abbrev == ABBREV_END_BLOCK) {
            if (depth == 0)
                r->failed = 1;
            align32(r);
            break;
        }
        if (abbrev == ABBREV_ENTER_SUBBLOCK) {
            uint32_t sub = (uint32_t)vbr(r, 8);
            uint32_t sub_width = (uint32_t)vbr(r, 4);
            align32(r);
            fixed(r, 32);
            BcBlock *child = read_block(r, sub, sub_width, depth + 1);
            if (child)
                add_entry(r, &entries, nullptr, child);
            continue;
        }
        if (abbrev == ABBREV_DEFINE) {
            if (block->id == 0) {
                if (!setting) {
                    r->failed = 1;
                    break;
                }
                define_abbrev(r, &setting->abbrevs);
            } else {
                define_abbrev(r, &local);
            }
            continue;
        }
        BcRecord *rec;
        if (abbrev == ABBREV_UNABBREVIATED) {
            rec = read_unabbreviated(r);
        } else {
            uint32_t index = abbrev - ABBREV_FIRST;
            BlockInfo *inherited = block_info(r, block->id, 0);
            uint32_t inherited_count = inherited ? inherited->abbrevs.count : 0;
            const Abbrev *a = nullptr;
            if (index < inherited_count)
                a = &inherited->abbrevs.items[index];
            else if (index - inherited_count < local.count)
                a = &local.items[index - inherited_count];
            if (!a) {
                r->failed = 1;
                break;
            }
            rec = read_abbreviated(r, a);
        }
        if (!rec)
            break;
        if (block->id == 0) {
            /* SETBID says which block the following abbreviations are for. */
            if (rec->code == BLOCKINFO_SETBID && rec->count)
                setting = block_info(r, (uint32_t)rec->ops[0], 1);
            continue;
        }
        add_entry(r, &entries, rec, nullptr);
    }
    block->entries = entries.items;
    block->count = entries.count;
}

static BcBlock *read_block(Reader *r, uint32_t id, uint32_t width, int depth)
{
    if (depth > 32 || width < 2 || width > 32) {
        r->failed = 1;
        return nullptr;
    }
    BcBlock *block = bc_arena_alloc(r->arena, sizeof(BcBlock));
    if (!block) {
        r->failed = 1;
        return nullptr;
    }
    block->id = id;
    read_entries(r, block, width, depth);
    return r->failed ? nullptr : block;
}

static uint32_t read32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

BcBlock *bc_parse(const uint8_t *data, size_t size, BcArena *arena)
{
    /* some producers put a wrapper header first: magic, version, offset, size, cpu. */
    if (size >= 20 && read32(data) == 0x0B17C0DE) {
        uint32_t offset = read32(data + 8), length = read32(data + 12);
        if (offset > size || length > size - offset)
            return nullptr;
        data += offset;
        size = length;
    }
    if (size < 4 || data[0] != 'B' || data[1] != 'C' || data[2] != 0xC0 || data[3] != 0xDE || (size & 3))
        return nullptr;
    Reader r = {data, size * 8, 32, arena, nullptr, 0, 0, 0};
    BcBlock *top = bc_arena_alloc(arena, sizeof(BcBlock));
    if (!top)
        return nullptr;
    top->id = ~0u;
    read_entries(&r, top, 2, 0);
    return r.failed ? nullptr : top;
}
