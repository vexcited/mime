#include "lzms.h"

#include <stdlib.h>
#include <string.h>

#define LZMS_LITERAL_SYMS 256
#define LZMS_LENGTH_SYMS 54
#define LZMS_POWER_SYMS 8
#define LZMS_OFFSET_SLOTS 799
#define LZMS_MAX_SYMS LZMS_OFFSET_SLOTS
#define LZMS_MAX_CODE_LEN 15
#define LZMS_FAST_BITS 10
#define LZMS_SORT_BUCKETS 4096
#define LZMS_MAX_OUT (1u << 30)

#define LZMS_PROB_BITS 6
#define LZMS_PROB_MAX (1u << LZMS_PROB_BITS)

#define LZMS_X86_ID_WINDOW 65535
#define LZMS_X86_MAX_OFFSET 1023

/* how many symbols a code decodes before it is rebuilt from its frequencies. */
#define LZMS_LITERAL_REBUILD 1024
#define LZMS_OFFSET_REBUILD 1024
#define LZMS_LENGTH_REBUILD 512
#define LZMS_POWER_REBUILD 512

typedef struct LzmsProb {
    uint32_t zeros; /* zero bits among the last 64 */
    uint64_t history; /* newest bit in the low end */
} LzmsProb;

typedef struct LzmsCode {
    uint32_t num_syms;
    uint32_t rebuild_freq;
    uint32_t until_rebuild;
    uint32_t freqs[LZMS_MAX_SYMS];
    uint16_t fast[1 << LZMS_FAST_BITS]; /* symbol << 4 | length, 0 for slow path */
    uint16_t count[LZMS_MAX_CODE_LEN + 1];
    uint16_t offset[LZMS_MAX_CODE_LEN + 1];
    uint32_t first[LZMS_MAX_CODE_LEN + 1];
    uint16_t sorted[LZMS_MAX_SYMS];
} LzmsCode;

typedef struct RangeDecoder {
    const uint8_t *in;
    size_t size;
    size_t pos;
    uint32_t range;
    uint32_t code;
} RangeDecoder;

/* reads 16 bit words from the end of the chunk towards its start. */
typedef struct BackReader {
    const uint8_t *in;
    size_t pos; /* one past the next word to load */
    uint64_t buf; /* left aligned */
    uint32_t bits;
} BackReader;

struct MimeLzms {
    LzmsProb main_probs[16];
    LzmsProb match_probs[32];
    LzmsProb lz_probs[64];
    LzmsProb lz_rep_probs[2][64];
    LzmsProb delta_probs[64];
    LzmsProb delta_rep_probs[2][64];

    LzmsCode literal;
    LzmsCode lz_offset;
    LzmsCode length;
    LzmsCode delta_offset;
    LzmsCode delta_power;

    /* scratch space for code construction. */
    uint32_t keys[LZMS_MAX_SYMS];
    uint32_t node_freq[2 * LZMS_MAX_SYMS];
    uint16_t node_parent[2 * LZMS_MAX_SYMS];
    uint8_t node_depth[2 * LZMS_MAX_SYMS];
    uint8_t lens[LZMS_MAX_SYMS];
    uint16_t sort_start[LZMS_SORT_BUCKETS + 1];

    int32_t x86_last_use[65536];
};

/*
 * both slot tables are runs of slots sharing a count of extra bits. a
 * slot covers base to base + 2^extra - 1 and the next slot starts after.
 */
static constexpr uint8_t g_offset_runs[][2] = {
    {0, 8},   {2, 9},   {3, 7},   {4, 10},  {5, 15},  {6, 15},  {7, 20},
    {8, 20},  {9, 30},  {10, 33}, {11, 40}, {12, 42}, {13, 45}, {14, 60},
    {15, 73}, {16, 80}, {17, 85}, {18, 95}, {19, 105}, {20, 6}, {30, 1},
};

static constexpr uint8_t g_length_runs[][2] = {
    {0, 26}, {1, 4}, {2, 6}, {3, 4}, {4, 5}, {5, 2}, {6, 1},
    {7, 1},  {8, 1}, {9, 1}, {10, 1}, {16, 1}, {30, 1},
};

static uint32_t g_offset_base[LZMS_OFFSET_SLOTS + 1];
static uint8_t g_offset_extra[LZMS_OFFSET_SLOTS];
static uint32_t g_length_base[LZMS_LENGTH_SYMS + 1];
static uint8_t g_length_extra[LZMS_LENGTH_SYMS];

static void fill_slots(const uint8_t (*runs)[2], size_t run_count, uint32_t *base, uint8_t *extra)
{
    uint32_t next = 1;
    size_t slot = 0;
    for (size_t r = 0; r < run_count; r++) {
        for (uint32_t i = 0; i < runs[r][1]; i++) {
            base[slot] = next;
            extra[slot] = runs[r][0];
            next += 1u << runs[r][0];
            slot++;
        }
    }
    base[slot] = next;
}

static void init_slots(void)
{
    static bool done;
    if (done)
        return;
    fill_slots(g_offset_runs, MIME_ARRAY_COUNT(g_offset_runs), g_offset_base, g_offset_extra);
    fill_slots(g_length_runs, MIME_ARRAY_COUNT(g_length_runs), g_length_base, g_length_extra);
    done = true;
}

/* a chunk only uses the offset slots that can reach back to its start. */
static uint32_t offset_slot_count(size_t out_size)
{
    if (out_size < 2)
        return 0;
    uint32_t far = (uint32_t)(out_size - 1);
    uint32_t slot = 0;
    while (slot + 1 < LZMS_OFFSET_SLOTS && g_offset_base[slot + 1] <= far)
        slot++;
    return slot + 1;
}

MimeLzms *mime_lzms_create(void)
{
    init_slots();
    return mime_xcalloc(1, sizeof(MimeLzms));
}

void mime_lzms_destroy(MimeLzms *lzms)
{
    free(lzms);
}

static inline uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static inline uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline void wr_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void probs_init(LzmsProb *probs, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        probs[i].zeros = 48;
        probs[i].history = 0x55555555u;
    }
}

static void rd_init(RangeDecoder *rd, const uint8_t *in, size_t size)
{
    rd->in = in;
    rd->size = size;
    rd->pos = 4;
    rd->range = 0xFFFFFFFFu;
    rd->code = (uint32_t)rd_le16(in) << 16 | rd_le16(in + 2);
}

/* decodes one bit with the probability selected by state, then shifts it into state. */
static inline uint32_t rd_bit(RangeDecoder *rd, uint32_t *state, uint32_t mask, LzmsProb *probs)
{
    LzmsProb *p = &probs[*state];
    uint32_t prob = p->zeros;
    if (prob == 0)
        prob = 1;
    else if (prob >= LZMS_PROB_MAX)
        prob = LZMS_PROB_MAX - 1;

    if (rd->range <= 0xFFFF) {
        rd->range <<= 16;
        rd->code <<= 16;
        if (rd->pos + 2 <= rd->size) {
            rd->code |= rd_le16(rd->in + rd->pos);
            rd->pos += 2;
        }
    }
    uint32_t bound = (rd->range >> LZMS_PROB_BITS) * prob;
    uint32_t bit;
    if (rd->code < bound) {
        rd->range = bound;
        bit = 0;
    } else {
        rd->range -= bound;
        rd->code -= bound;
        bit = 1;
    }
    p->zeros += (uint32_t)(p->history >> 63) - bit;
    p->history = p->history << 1 | bit;
    *state = (*state << 1 | bit) & mask;
    return bit;
}

static void br_init(BackReader *br, const uint8_t *in, size_t size)
{
    br->in = in;
    br->pos = size;
    br->buf = 0;
    br->bits = 0;
}

/* past the start of the chunk the stream reads as zeros. */
static inline void br_refill(BackReader *br)
{
    while (br->bits <= 48) {
        uint32_t word = 0;
        if (br->pos >= 2) {
            br->pos -= 2;
            word = rd_le16(br->in + br->pos);
        }
        br->buf |= (uint64_t)word << (48 - br->bits);
        br->bits += 16;
    }
}

static inline uint32_t br_read(BackReader *br, uint32_t n)
{
    if (n == 0)
        return 0;
    br_refill(br);
    uint32_t v = (uint32_t)(br->buf >> (64 - n));
    br->buf <<= n;
    br->bits -= n;
    return v;
}

static int compare_keys(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

/*
 * computes code lengths the way the format demands: symbols sorted by
 * frequency then value, a huffman tree that takes leaves before internal
 * nodes on ties, and the resulting lengths handed out longest first in
 * that same sorted order.
 */
static bool compute_lengths(MimeLzms *z, const LzmsCode *c)
{
    uint32_t n = c->num_syms;
    memset(z->lens, 0, n);
    if (n == 1) {
        z->lens[0] = 1;
        return true;
    }
    uint32_t max_freq = 0;
    for (uint32_t s = 0; s < n; s++)
        max_freq = MIME_MAX(max_freq, c->freqs[s]);
    if (max_freq < LZMS_SORT_BUCKETS) {
        /* frequencies stay small, so a counting sort keeps symbol order on ties. */
        uint16_t *start = z->sort_start;
        memset(start, 0, (max_freq + 2) * sizeof(start[0]));
        for (uint32_t s = 0; s < n; s++)
            start[c->freqs[s] + 1]++;
        for (uint32_t f = 1; f <= max_freq + 1; f++)
            start[f] += start[f - 1];
        for (uint32_t s = 0; s < n; s++)
            z->keys[start[c->freqs[s]]++] = c->freqs[s] << 10 | s;
    } else {
        for (uint32_t s = 0; s < n; s++)
            z->keys[s] = c->freqs[s] << 10 | s;
        qsort(z->keys, n, sizeof(z->keys[0]), compare_keys);
    }

    for (uint32_t i = 0; i < n; i++)
        z->node_freq[i] = z->keys[i] >> 10;
    uint32_t leaf = 0, inner = n, end = n;
    while (end < 2 * n - 1) {
        uint32_t sum = 0;
        for (int pick = 0; pick < 2; pick++) {
            uint32_t node;
            if (leaf < n && (inner == end || z->node_freq[leaf] <= z->node_freq[inner]))
                node = leaf++;
            else
                node = inner++;
            z->node_parent[node] = (uint16_t)end;
            sum += z->node_freq[node];
        }
        z->node_freq[end++] = sum;
    }

    /* parents always come after their children. */
    uint32_t root = 2 * n - 2;
    z->node_depth[root] = 0;
    uint32_t len_count[LZMS_MAX_CODE_LEN + 1] = {};
    for (uint32_t i = root; i-- > 0;) {
        uint32_t depth = z->node_depth[z->node_parent[i]] + 1u;
        if (depth > LZMS_MAX_CODE_LEN)
            return false;
        z->node_depth[i] = (uint8_t)depth;
        if (i < n)
            len_count[depth]++;
    }

    uint32_t k = 0;
    for (uint32_t len = LZMS_MAX_CODE_LEN; len >= 1; len--) {
        for (uint32_t i = 0; i < len_count[len]; i++)
            z->lens[z->keys[k++] & 1023] = (uint8_t)len;
    }
    return true;
}

static bool code_build(MimeLzms *z, LzmsCode *c)
{
    c->until_rebuild = c->rebuild_freq;
    memset(c->count, 0, sizeof(c->count));
    memset(c->fast, 0, sizeof(c->fast));
    if (c->num_syms == 0)
        return true;
    if (!compute_lengths(z, c))
        return false;

    for (uint32_t s = 0; s < c->num_syms; s++)
        c->count[z->lens[s]]++;
    c->count[0] = 0;
    uint32_t code = 0;
    uint16_t off = 0;
    for (int len = 1; len <= LZMS_MAX_CODE_LEN; len++) {
        c->first[len] = code;
        c->offset[len] = off;
        off += c->count[len];
        code = (code + c->count[len]) << 1;
    }
    uint16_t next[LZMS_MAX_CODE_LEN + 1];
    memcpy(next, c->offset, sizeof(next));
    for (uint32_t s = 0; s < c->num_syms; s++) {
        if (z->lens[s])
            c->sorted[next[z->lens[s]]++] = (uint16_t)s;
    }
    for (int len = 1; len <= LZMS_FAST_BITS; len++) {
        uint32_t span = 1u << (LZMS_FAST_BITS - len);
        for (uint32_t i = 0; i < c->count[len]; i++) {
            uint32_t sym = c->sorted[c->offset[len] + i];
            uint32_t start = (c->first[len] + i) << (LZMS_FAST_BITS - len);
            for (uint32_t j = 0; j < span; j++)
                c->fast[start + j] = (uint16_t)(sym << 4 | (uint32_t)len);
        }
    }
    return true;
}

static bool code_init(MimeLzms *z, LzmsCode *c, uint32_t num_syms, uint32_t rebuild_freq)
{
    c->num_syms = num_syms;
    c->rebuild_freq = rebuild_freq;
    for (uint32_t s = 0; s < num_syms; s++)
        c->freqs[s] = 1;
    return code_build(z, c);
}

/* returns the symbol, or -1 on a bad code. */
static inline int code_decode(MimeLzms *z, LzmsCode *c, BackReader *br)
{
    br_refill(br);
    int sym = -1;
    uint16_t e = c->fast[br->buf >> (64 - LZMS_FAST_BITS)];
    if (e) {
        br->buf <<= e & 15;
        br->bits -= e & 15;
        sym = e >> 4;
    } else {
        uint32_t bits = (uint32_t)(br->buf >> (64 - LZMS_MAX_CODE_LEN));
        for (int len = LZMS_FAST_BITS + 1; len <= LZMS_MAX_CODE_LEN; len++) {
            uint32_t idx = (bits >> (LZMS_MAX_CODE_LEN - len)) - c->first[len];
            if (idx < c->count[len]) {
                br->buf <<= len;
                br->bits -= (uint32_t)len;
                sym = c->sorted[c->offset[len] + idx];
                break;
            }
        }
        if (sym < 0)
            return -1;
    }

    c->freqs[sym]++;
    if (--c->until_rebuild == 0) {
        if (!code_build(z, c))
            return -1;
        for (uint32_t s = 0; s < c->num_syms; s++)
            c->freqs[s] = (c->freqs[s] >> 1) + 1;
    }
    return sym;
}

static inline bool decode_offset(MimeLzms *z, LzmsCode *c, BackReader *br, uint32_t *out)
{
    int slot = code_decode(z, c, br);
    if (slot < 0)
        return false;
    *out = g_offset_base[slot] + br_read(br, g_offset_extra[slot]);
    return true;
}

static inline bool decode_length(MimeLzms *z, BackReader *br, uint32_t *out)
{
    int slot = code_decode(z, &z->length, br);
    if (slot < 0)
        return false;
    *out = g_length_base[slot] + br_read(br, g_length_extra[slot]);
    return true;
}

/*
 * picks repeat entry idx and moves it to the front of the queue. a match
 * of the same kind just before has already been pushed in front, while
 * the format only inserts it after one more item, so it is skipped.
 */
static inline uint64_t take_recent(uint64_t *queue, uint32_t idx, uint32_t skip)
{
    uint32_t slot = idx + skip;
    uint64_t v = queue[slot];
    for (uint32_t i = slot; i > 0; i--)
        queue[i] = queue[i - 1];
    queue[0] = v;
    return v;
}

static inline void push_recent(uint64_t *queue, uint64_t v)
{
    queue[3] = queue[2];
    queue[2] = queue[1];
    queue[1] = queue[0];
    queue[0] = v;
}

/* reads the repeat index from up to two range coded decisions. */
static inline uint32_t decode_rep_index(RangeDecoder *rd, uint32_t *states, LzmsProb (*probs)[64])
{
    if (!rd_bit(rd, &states[0], 63, probs[0]))
        return 0;
    if (!rd_bit(rd, &states[1], 63, probs[1]))
        return 1;
    return 2;
}

/*
 * one candidate x86 instruction at data[i]; returns where scanning resumes.
 * the compressor turned the relative operand of some call, lea, mov and
 * locked add instructions into an absolute one when it believed the data
 * was code, judged by how recently the same target was referenced.
 */
static size_t undo_x86_at(MimeLzms *z, uint8_t *data, size_t i, int32_t *last_x86)
{
    uint32_t op_len;
    int32_t max_offset = LZMS_X86_MAX_OFFSET;
    switch (data[i]) {
    case 0xFF:
        if (data[i + 1] != 0x15)
            return i + 1;
        op_len = 2;
        break;
    case 0xF0:
        if (data[i + 1] != 0x83 || data[i + 2] != 0x05)
            return i + 1;
        op_len = 3;
        break;
    case 0x48:
    case 0x4C:
        if ((data[i + 2] & 7) != 5)
            return i + 1;
        if (data[i + 1] != 0x8D && !(data[i + 1] == 0x8B && data[i] == 0x48 && !(data[i + 2] & 0xF0)))
            return i + 1;
        op_len = 3;
        break;
    case 0xE8:
        op_len = 1;
        max_offset >>= 1;
        break;
    case 0xE9:
        return i + 5;
    default:
        return i + 1;
    }

    int32_t pos = (int32_t)i;
    uint8_t *operand = data + i + op_len;
    if (pos - *last_x86 <= max_offset)
        wr_le32(operand, rd_le32(operand) - (uint32_t)pos);
    uint16_t target = (uint16_t)((uint32_t)pos + rd_le16(operand));

    int32_t last_byte = pos + (int32_t)op_len + 3;
    if (last_byte - z->x86_last_use[target] <= LZMS_X86_ID_WINDOW)
        *last_x86 = last_byte;
    z->x86_last_use[target] = last_byte;
    return i + op_len + 4;
}

static void undo_x86(MimeLzms *z, uint8_t *data, size_t size)
{
    if (size <= 17)
        return;
    for (size_t i = 0; i < 65536; i++)
        z->x86_last_use[i] = -LZMS_X86_ID_WINDOW - 1;
    int32_t last_x86 = -LZMS_X86_MAX_OFFSET - 1;
    /* the first byte is never an opcode and nothing starts in the last 16. */
    size_t end = size - 16;
    size_t i = 1;
    while (i < end) {
        uint8_t b = data[i];
        if (b == 0xE8 || b == 0xE9 || b == 0x48 || b == 0x4C || b == 0xF0 || b == 0xFF)
            i = undo_x86_at(z, data, i, &last_x86);
        else
            i++;
    }
}

bool mime_lzms_decompress(MimeLzms *z, const void *in_ptr, size_t in_size, void *out_ptr, size_t out_size)
{
    const uint8_t *in = in_ptr;
    uint8_t *out = out_ptr;
    if (in_size < 4 || (in_size & 1) || out_size > LZMS_MAX_OUT)
        return false;

    probs_init(z->main_probs, MIME_ARRAY_COUNT(z->main_probs));
    probs_init(z->match_probs, MIME_ARRAY_COUNT(z->match_probs));
    probs_init(z->lz_probs, MIME_ARRAY_COUNT(z->lz_probs));
    probs_init(&z->lz_rep_probs[0][0], 2 * 64);
    probs_init(z->delta_probs, MIME_ARRAY_COUNT(z->delta_probs));
    probs_init(&z->delta_rep_probs[0][0], 2 * 64);

    uint32_t slots = offset_slot_count(out_size);
    if (!code_init(z, &z->literal, LZMS_LITERAL_SYMS, LZMS_LITERAL_REBUILD) ||
        !code_init(z, &z->lz_offset, slots, LZMS_OFFSET_REBUILD) ||
        !code_init(z, &z->length, LZMS_LENGTH_SYMS, LZMS_LENGTH_REBUILD) ||
        !code_init(z, &z->delta_offset, slots, LZMS_OFFSET_REBUILD) ||
        !code_init(z, &z->delta_power, LZMS_POWER_SYMS, LZMS_POWER_REBUILD))
        return false;

    RangeDecoder rd;
    BackReader br;
    rd_init(&rd, in, in_size);
    br_init(&br, in, in_size);

    uint32_t main_state = 0, match_state = 0, lz_state = 0, delta_state = 0;
    uint32_t lz_rep_states[2] = {}, delta_rep_states[2] = {};
    uint64_t lz_recent[4] = {1, 2, 3, 4};
    uint64_t delta_recent[4] = {1, 2, 3, 4}; /* power << 32 | raw offset */
    enum { ITEM_LITERAL, ITEM_LZ, ITEM_DELTA } prev = ITEM_LITERAL;

    size_t pos = 0;
    while (pos < out_size) {
        if (!rd_bit(&rd, &main_state, 15, z->main_probs)) {
            int sym = code_decode(z, &z->literal, &br);
            if (sym < 0)
                return false;
            out[pos++] = (uint8_t)sym;
            prev = ITEM_LITERAL;
            continue;
        }

        if (!rd_bit(&rd, &match_state, 31, z->match_probs)) {
            uint32_t offset, length;
            if (!rd_bit(&rd, &lz_state, 63, z->lz_probs)) {
                if (!decode_offset(z, &z->lz_offset, &br, &offset))
                    return false;
                push_recent(lz_recent, offset);
            } else {
                uint32_t idx = decode_rep_index(&rd, lz_rep_states, z->lz_rep_probs);
                offset = (uint32_t)take_recent(lz_recent, idx, prev == ITEM_LZ);
            }
            prev = ITEM_LZ;
            if (!decode_length(z, &br, &length))
                return false;
            if (offset == 0 || offset > pos || length > out_size - pos)
                return false;
            const uint8_t *src = out + pos - offset;
            uint8_t *dst = out + pos;
            if (offset >= 8) {
                uint32_t i = 0;
                for (; i + 8 <= length; i += 8)
                    memcpy(dst + i, src + i, 8);
                for (; i < length; i++)
                    dst[i] = src[i];
            } else {
                for (uint32_t i = 0; i < length; i++)
                    dst[i] = src[i];
            }
            pos += length;
            continue;
        }

        /* delta match: each byte adds the difference of two earlier bytes. */
        uint64_t pair;
        if (!rd_bit(&rd, &delta_state, 63, z->delta_probs)) {
            int power = code_decode(z, &z->delta_power, &br);
            uint32_t raw;
            if (power < 0 || !decode_offset(z, &z->delta_offset, &br, &raw))
                return false;
            pair = (uint64_t)power << 32 | raw;
            push_recent(delta_recent, pair);
        } else {
            uint32_t idx = decode_rep_index(&rd, delta_rep_states, z->delta_rep_probs);
            pair = take_recent(delta_recent, idx, prev == ITEM_DELTA);
        }
        prev = ITEM_DELTA;
        uint32_t length;
        if (!decode_length(z, &br, &length))
            return false;
        uint32_t power = (uint32_t)(pair >> 32);
        if (power >= 32)
            return false;
        uint64_t span = 1ull << power;
        uint64_t offset = (uint64_t)(uint32_t)pair << power;
        if (offset + span > pos || length > out_size - pos)
            return false;
        for (uint32_t i = 0; i < length; i++, pos++)
            out[pos] = (uint8_t)(out[pos - offset] + out[pos - span] - out[pos - offset - span]);
    }

    undo_x86(z, out, out_size);
    return true;
}
