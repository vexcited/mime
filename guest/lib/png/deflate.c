/*
 * zlib encoder. matches come from hash chains of positions whose next three bytes hash alike, and each block
 * of up to 32768 literals and matches gets its own huffman codes.
 */
#include "deflate.h"

#define memcpy __builtin_memcpy
#define memset __builtin_memset

#include "inflate.h"

#define WINDOW 32768
#define HASH_BITS 15
#define HASH_SIZE (1 << HASH_BITS)
#define MIN_MATCH 3
#define MAX_MATCH 258
#define BLOCK_TOKENS 32768
#define MAX_BITS 15

/* distance 0 marks a literal, its byte stored in length. */
typedef struct Token {
    uint16_t length;
    uint16_t distance;
} Token;

typedef struct Out {
    const MimeAllocator *alloc;
    uint8_t *data;
    size_t size, capacity;
    uint32_t bits;
    unsigned count;
    int failed;
} Out;

static void out_byte(Out *o, uint8_t b)
{
    if (o->failed)
        return;
    if (o->size == o->capacity) {
        size_t cap = o->capacity ? o->capacity * 2 : 65536;
        uint8_t *grown = mime_alloc(o->alloc, cap);
        if (!grown) {
            o->failed = 1;
            return;
        }
        if (o->data) {
            memcpy(grown, o->data, o->size);
            mime_free(o->alloc, o->data);
        }
        o->data = grown;
        o->capacity = cap;
    }
    o->data[o->size++] = b;
}

static void put_bits(Out *o, uint32_t value, unsigned n)
{
    o->bits |= value << o->count;
    o->count += n;
    while (o->count >= 8) {
        out_byte(o, (uint8_t)o->bits);
        o->bits >>= 8;
        o->count -= 8;
    }
}

static void flush_bits(Out *o)
{
    if (o->count)
        out_byte(o, (uint8_t)o->bits);
    o->bits = 0;
    o->count = 0;
}

/* ------------------------------------------------------------------ format tables */

static const uint16_t g_length_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                           31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t g_length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t g_dist_base[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                         33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                         1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t g_dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
static const uint8_t g_clen_order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

static int length_code(int length)
{
    int c = 28;
    while (c > 0 && g_length_base[c] > length)
        c--;
    return c;
}

static int dist_code(int distance)
{
    int c = 29;
    while (c > 0 && g_dist_base[c] > distance)
        c--;
    return c;
}

/* ------------------------------------------------------------------ huffman codes */

/* no code length may exceed limit: rebuild the tree with flatter frequencies until it fits. */
typedef struct Node {
    uint32_t weight;
    int left, right;
} Node;

static int tree_depths(const uint32_t *freq, int n, uint8_t *lengths, int limit, Node *nodes, int *order)
{
    uint32_t f[320];
    for (int i = 0; i < n; i++)
        f[i] = freq[i];
    for (;;) {
        int count = 0;
        for (int i = 0; i < n; i++) {
            lengths[i] = 0;
            if (f[i])
                order[count++] = i;
        }
        if (count == 0)
            return 0;
        if (count == 1) {
            lengths[order[0]] = 1;
            return 1;
        }
        /* sort leaves by weight, then merge them with the queue of built nodes. */
        for (int i = 1; i < count; i++) {
            int v = order[i], j = i - 1;
            while (j >= 0 && f[order[j]] > f[v]) {
                order[j + 1] = order[j];
                j--;
            }
            order[j + 1] = v;
        }
        for (int i = 0; i < count; i++) {
            nodes[i].weight = f[order[i]];
            nodes[i].left = nodes[i].right = -1;
        }
        int leaf = 0, queue = count, made = count, total = 2 * count - 1;
        while (made < total) {
            int pick[2];
            for (int k = 0; k < 2; k++) {
                if (leaf < count && (queue >= made || nodes[leaf].weight <= nodes[queue].weight))
                    pick[k] = leaf++;
                else
                    pick[k] = queue++;
            }
            nodes[made].weight = nodes[pick[0]].weight + nodes[pick[1]].weight;
            nodes[made].left = pick[0];
            nodes[made].right = pick[1];
            made++;
        }
        /* a parent is built after its children, so walking the nodes backwards yields depths from the root. */
        int depth[640];
        depth[total - 1] = 0;
        int deepest = 0;
        for (int i = total - 1; i >= count; i--) {
            depth[nodes[i].left] = depth[i] + 1;
            depth[nodes[i].right] = depth[i] + 1;
        }
        for (int i = 0; i < count; i++) {
            lengths[order[i]] = (uint8_t)depth[i];
            if (depth[i] > deepest)
                deepest = depth[i];
        }
        if (deepest <= limit)
            return deepest;
        for (int i = 0; i < n; i++)
            if (f[i])
                f[i] = (f[i] + 1) / 2 ? (f[i] + 1) / 2 : 1;
        /* halving may leave the shape unchanged, a floor keeps it moving. */
        for (int i = 0; i < n; i++)
            if (f[i] && f[i] < 2 && freq[i] > 1)
                f[i] = 2;
    }
}

static void canonical_codes(const uint8_t *lengths, int n, uint16_t *codes)
{
    unsigned count[MAX_BITS + 1] = {0}, next[MAX_BITS + 2] = {0};
    for (int i = 0; i < n; i++)
        count[lengths[i]]++;
    count[0] = 0;
    unsigned code = 0;
    for (int b = 1; b <= MAX_BITS; b++) {
        code = (code + count[b - 1]) << 1;
        next[b] = code;
    }
    for (int i = 0; i < n; i++) {
        unsigned len = lengths[i];
        if (!len) {
            codes[i] = 0;
            continue;
        }
        unsigned c = next[len]++;
        unsigned reversed = 0;
        for (unsigned k = 0; k < len; k++)
            reversed |= ((c >> k) & 1u) << (len - 1 - k);
        codes[i] = (uint16_t)reversed;
    }
}

/* ------------------------------------------------------------------ blocks */

static void write_block(Out *o, const Token *tokens, int count, int final, const uint8_t *raw, size_t raw_len)
{
    uint32_t lit_freq[286] = {0}, dist_freq[30] = {0};
    for (int i = 0; i < count; i++) {
        if (!tokens[i].distance) {
            lit_freq[tokens[i].length]++;
        } else {
            lit_freq[257 + length_code(tokens[i].length)]++;
            dist_freq[dist_code(tokens[i].distance)]++;
        }
    }
    lit_freq[256] = 1;
    uint8_t lit_len[286], dist_len[30];
    Node nodes[640];
    int order[320];
    tree_depths(lit_freq, 286, lit_len, MAX_BITS, nodes, order);
    int dist_used = 0;
    for (int i = 0; i < 30; i++)
        dist_used += dist_freq[i] != 0;
    if (!dist_used)
        dist_freq[0] = 1;
    tree_depths(dist_freq, 30, dist_len, MAX_BITS, nodes, order);
    if (dist_used <= 1) {
        /* some decoders need a second distance code to see a complete tree. */
        int other = dist_len[0] ? 1 : 0;
        dist_len[0] = 1;
        dist_len[other] = 1;
    }
    int hlit = 286, hdist = 30;
    while (hlit > 257 && !lit_len[hlit - 1])
        hlit--;
    while (hdist > 1 && !dist_len[hdist - 1])
        hdist--;
    /* both alphabets' code lengths form one run, coded with the code length alphabet. */
    uint8_t all[316];
    for (int i = 0; i < hlit; i++)
        all[i] = lit_len[i];
    for (int i = 0; i < hdist; i++)
        all[hlit + i] = dist_len[i];
    int total = hlit + hdist;
    uint8_t syms[320], extra[320];
    int nsyms = 0;
    for (int i = 0; i < total;) {
        int run = 1;
        while (i + run < total && all[i + run] == all[i])
            run++;
        if (all[i] == 0 && run >= 3) {
            int r = run > 138 ? 138 : run;
            syms[nsyms] = r >= 11 ? 18 : 17;
            extra[nsyms++] = (uint8_t)(r >= 11 ? r - 11 : r - 3);
            i += r;
        } else if (all[i] != 0 && run >= 4) {
            syms[nsyms] = all[i];
            extra[nsyms++] = 0;
            int r = run - 1 > 6 ? 6 : run - 1;
            syms[nsyms] = 16;
            extra[nsyms++] = (uint8_t)(r - 3);
            i += 1 + r;
        } else {
            syms[nsyms] = all[i];
            extra[nsyms++] = 0;
            i++;
        }
    }
    uint32_t clen_freq[19] = {0};
    for (int i = 0; i < nsyms; i++)
        clen_freq[syms[i]]++;
    uint8_t clen_len[19];
    tree_depths(clen_freq, 19, clen_len, 7, nodes, order);
    uint16_t clen_code[19], lit_code[286], dist_code_[30];
    canonical_codes(clen_len, 19, clen_code);
    canonical_codes(lit_len, 286, lit_code);
    canonical_codes(dist_len, 30, dist_code_);
    int hclen = 19;
    while (hclen > 4 && !clen_len[g_clen_order[hclen - 1]])
        hclen--;

    /* the size as a stored block, used instead when smaller. */
    uint64_t bits = 3 + 5 + 5 + 4 + 3ull * hclen;
    for (int i = 0; i < nsyms; i++)
        bits += clen_len[syms[i]] + (syms[i] == 16 ? 2 : syms[i] == 17 ? 3 : syms[i] == 18 ? 7 : 0);
    for (int i = 0; i < count; i++) {
        if (!tokens[i].distance) {
            bits += lit_len[tokens[i].length];
        } else {
            int lc = length_code(tokens[i].length), dc = dist_code(tokens[i].distance);
            bits += lit_len[257 + lc] + g_length_extra[lc] + dist_len[dc] + g_dist_extra[dc];
        }
    }
    bits += lit_len[256];
    if (raw && raw_len <= 65535 && (bits + 7) / 8 > raw_len + 5) {
        put_bits(o, final ? 1 : 0, 1);
        put_bits(o, 0, 2);
        flush_bits(o);
        out_byte(o, (uint8_t)raw_len);
        out_byte(o, (uint8_t)(raw_len >> 8));
        out_byte(o, (uint8_t)~raw_len);
        out_byte(o, (uint8_t)(~raw_len >> 8));
        for (size_t i = 0; i < raw_len; i++)
            out_byte(o, raw[i]);
        return;
    }
    put_bits(o, final ? 1 : 0, 1);
    put_bits(o, 2, 2);
    put_bits(o, (uint32_t)(hlit - 257), 5);
    put_bits(o, (uint32_t)(hdist - 1), 5);
    put_bits(o, (uint32_t)(hclen - 4), 4);
    for (int i = 0; i < hclen; i++)
        put_bits(o, clen_len[g_clen_order[i]], 3);
    for (int i = 0; i < nsyms; i++) {
        put_bits(o, clen_code[syms[i]], clen_len[syms[i]]);
        if (syms[i] == 16)
            put_bits(o, extra[i], 2);
        else if (syms[i] == 17)
            put_bits(o, extra[i], 3);
        else if (syms[i] == 18)
            put_bits(o, extra[i], 7);
    }
    for (int i = 0; i < count; i++) {
        if (!tokens[i].distance) {
            put_bits(o, lit_code[tokens[i].length], lit_len[tokens[i].length]);
        } else {
            int lc = length_code(tokens[i].length), dc = dist_code(tokens[i].distance);
            put_bits(o, lit_code[257 + lc], lit_len[257 + lc]);
            if (g_length_extra[lc])
                put_bits(o, (uint32_t)(tokens[i].length - g_length_base[lc]), g_length_extra[lc]);
            put_bits(o, dist_code_[dc], dist_len[dc]);
            if (g_dist_extra[dc])
                put_bits(o, (uint32_t)(tokens[i].distance - g_dist_base[dc]), g_dist_extra[dc]);
        }
    }
    put_bits(o, lit_code[256], lit_len[256]);
}

/* ------------------------------------------------------------------ encoder */

static uint32_t hash3(const uint8_t *p)
{
    uint32_t v = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;
    return (v * 2654435761u) >> (32 - HASH_BITS);
}

int mime_deflate_zlib(const void *src, size_t src_len, int level, const MimeAllocator *alloc, void **out,
                      size_t *out_len)
{
    const uint8_t *in = src;
    Out o = {alloc, nullptr, 0, 0, 0, 0, 0};
    int32_t *head = nullptr;
    uint32_t *prev = nullptr;
    Token *tokens = nullptr;
    *out = nullptr;
    *out_len = 0;
    if (level < 0)
        level = 6;
    if (level > 9)
        level = 9;
    out_byte(&o, 0x78);
    out_byte(&o, level >= 7 ? 0xDA : (level >= 3 ? 0x9C : 0x01));
    if (level == 0 || !src_len) {
        /* stored blocks of up to 65535 bytes. */
        size_t pos = 0;
        do {
            size_t n = src_len - pos > 65535 ? 65535 : src_len - pos;
            int last = pos + n >= src_len;
            out_byte(&o, last ? 1 : 0);
            out_byte(&o, (uint8_t)n);
            out_byte(&o, (uint8_t)(n >> 8));
            out_byte(&o, (uint8_t)~n);
            out_byte(&o, (uint8_t)(~n >> 8));
            for (size_t i = 0; i < n; i++)
                out_byte(&o, in[pos + i]);
            pos += n;
        } while (pos < src_len);
    } else {
        head = mime_alloc(alloc, HASH_SIZE * sizeof(int32_t));
        prev = mime_alloc(alloc, WINDOW * sizeof(uint32_t));
        tokens = mime_alloc(alloc, BLOCK_TOKENS * sizeof(Token));
        if (!head || !prev || !tokens) {
            o.failed = 1;
        } else {
            for (int i = 0; i < HASH_SIZE; i++)
                head[i] = -1;
            int chain = 4 * level;
            int count = 0;
            size_t block_start = 0, pos = 0;
            while (pos < src_len && !o.failed) {
                int best_len = 0, best_dist = 0;
                if (pos + MIN_MATCH <= src_len) {
                    uint32_t h = hash3(in + pos);
                    int32_t cand = head[h];
                    int tries = chain;
                    size_t max = src_len - pos > MAX_MATCH ? MAX_MATCH : src_len - pos;
                    while (cand >= 0 && pos - (size_t)cand <= WINDOW && tries--) {
                        const uint8_t *a = in + cand, *b = in + pos;
                        int len = 0;
                        if (a[best_len < (int)max ? best_len : 0] == b[best_len < (int)max ? best_len : 0] || !best_len) {
                            while ((size_t)len < max && a[len] == b[len])
                                len++;
                            if (len > best_len) {
                                best_len = len;
                                best_dist = (int)(pos - (size_t)cand);
                                if ((size_t)len == max)
                                    break;
                            }
                        }
                        uint32_t next = prev[(size_t)cand & (WINDOW - 1)];
                        if (next == (uint32_t)-1 || (int32_t)next >= cand)
                            break;
                        cand = (int32_t)next;
                    }
                }
                size_t advance;
                if (best_len >= MIN_MATCH) {
                    tokens[count].length = (uint16_t)best_len;
                    tokens[count].distance = (uint16_t)best_dist;
                    advance = (size_t)best_len;
                } else {
                    tokens[count].length = in[pos];
                    tokens[count].distance = 0;
                    advance = 1;
                }
                count++;
                for (size_t k = 0; k < advance; k++, pos++) {
                    if (pos + MIN_MATCH <= src_len) {
                        uint32_t h = hash3(in + pos);
                        prev[pos & (WINDOW - 1)] = (uint32_t)head[h];
                        head[h] = (int32_t)pos;
                    }
                }
                if (count == BLOCK_TOKENS || pos >= src_len) {
                    size_t raw_len = pos - block_start;
                    write_block(&o, tokens, count, pos >= src_len, in + block_start, raw_len);
                    count = 0;
                    block_start = pos;
                }
            }
            flush_bits(&o);
        }
    }
    uint32_t adler = mime_inflate_adler32(1, src, src_len);
    out_byte(&o, (uint8_t)(adler >> 24));
    out_byte(&o, (uint8_t)(adler >> 16));
    out_byte(&o, (uint8_t)(adler >> 8));
    out_byte(&o, (uint8_t)adler);
    mime_free(alloc, head);
    mime_free(alloc, prev);
    mime_free(alloc, tokens);
    if (o.failed) {
        mime_free(alloc, o.data);
        return MIME_DEFLATE_ERR_NOMEM;
    }
    *out = o.data;
    *out_len = o.size;
    return MIME_DEFLATE_OK;
}
