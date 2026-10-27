/* the lzma decoder, written from the published description of the format. */
#include "stream.h"
#ifdef _WIN32
#include <rtl.h>
#else
#include <string.h>
#endif

#define PROB_INIT 1024
#define TOP_VALUE (1u << 24)
#define STATES 12
#define POS_STATES_MAX 16
#define MATCH_MIN 2

typedef struct LEN_PROBS {
    uint16_t Choice, Choice2;
    uint16_t Low[POS_STATES_MAX][8], Mid[POS_STATES_MAX][8];
    uint16_t High[256];
} LEN_PROBS;

typedef struct PROBS {
    uint16_t IsMatch[STATES][POS_STATES_MAX];
    uint16_t IsRep[STATES], IsRepG0[STATES], IsRepG1[STATES], IsRepG2[STATES];
    uint16_t IsRep0Long[STATES][POS_STATES_MAX];
    uint16_t PosSlot[4][64];
    uint16_t SpecPos[115];
    uint16_t Align[16];
    LEN_PROBS Len, RepLen;
    uint16_t Literal[];
} PROBS;

typedef struct RC {
    uint32_t Range, Code;
    const uint8_t *In, *End;
    bool Error;
} RC;

typedef struct LZMA {
    CBS_STREAM Base;
    CBS_STREAM *Input;
    PROBS *Probs;
    uint8_t *Dict;
    uint32_t DictSize, DictPos;
    bool Wrapped;
    uint32_t Lc, Lp, Pb;
    uint32_t State, Rep[4];
    uint32_t Pending; /* bytes of a match that did not fit in the last call. */
    uint64_t Processed; /* bytes decoded in all. */
    uint64_t Remaining; /* bytes still to be given out. */
    RC Rc;
    bool Finished;
} LZMA;

static inline void rc_normalize(RC *rc)
{
    if (rc->Range < TOP_VALUE) {
        rc->Range <<= 8;
        if (rc->In < rc->End)
            rc->Code = rc->Code << 8 | *rc->In++;
        else {
            rc->Code <<= 8;
            rc->Error = true;
        }
    }
}

static inline unsigned rc_bit(RC *rc, uint16_t *prob)
{
    rc_normalize(rc);
    uint32_t p = *prob;
    uint32_t bound = (rc->Range >> 11) * p;
    if (rc->Code < bound) {
        rc->Range = bound;
        *prob = (uint16_t)(p + ((2048 - p) >> 5));
        return 0;
    }
    rc->Range -= bound;
    rc->Code -= bound;
    *prob = (uint16_t)(p - (p >> 5));
    return 1;
}

static inline uint32_t rc_direct(RC *rc, unsigned count)
{
    uint32_t result = 0;
    while (count--) {
        rc_normalize(rc);
        rc->Range >>= 1;
        rc->Code -= rc->Range;
        uint32_t mask = 0u - (rc->Code >> 31);
        rc->Code += rc->Range & mask;
        result = (result << 1) + mask + 1;
    }
    return result;
}

static inline uint32_t tree(RC *rc, uint16_t *probs, unsigned bits)
{
    uint32_t m = 1;
    for (unsigned i = 0; i < bits; i++)
        m = (m << 1) + rc_bit(rc, probs + m);
    return m - (1u << bits);
}

static inline uint32_t tree_reverse(RC *rc, uint16_t *probs, unsigned bits)
{
    uint32_t m = 1, symbol = 0;
    for (unsigned i = 0; i < bits; i++) {
        unsigned bit = rc_bit(rc, probs + m);
        m = (m << 1) + bit;
        symbol |= (uint32_t)bit << i;
    }
    return symbol;
}

static inline uint32_t length(RC *rc, LEN_PROBS *p, unsigned pos_state)
{
    if (!rc_bit(rc, &p->Choice))
        return tree(rc, p->Low[pos_state], 3);
    if (!rc_bit(rc, &p->Choice2))
        return 8 + tree(rc, p->Mid[pos_state], 3);
    return 16 + tree(rc, p->High, 8);
}

/* copies up to count bytes of a match; returns how many were copied. */
static uint32_t copy_match(LZMA *z, uint32_t distance, uint32_t count, uint32_t limit)
{
    uint32_t room = limit - z->DictPos;
    if (count > room)
        count = room;
    uint32_t dst = z->DictPos;
    uint32_t src = dst >= distance + 1 ? dst - distance - 1 : dst + z->DictSize - distance - 1;
    uint8_t *dict = z->Dict;
    if (src + count <= z->DictSize && (src + count <= dst || src >= dst + count)) {
        memcpy(dict + dst, dict + src, count);
    } else {
        for (uint32_t i = 0; i < count; i++) {
            dict[dst + i] = dict[src];
            if (++src == z->DictSize)
                src = 0;
        }
    }
    z->DictPos += count;
    return count;
}

/* decodes until DictPos reaches limit, the stream ends or the data is bad. */
static int decode(LZMA *z, uint32_t limit)
{
    RC rc = z->Rc;
    PROBS *probs = z->Probs;
    uint8_t *dict = z->Dict;
    uint32_t pos = z->DictPos;
    uint32_t state = z->State;
    uint32_t rep0 = z->Rep[0], rep1 = z->Rep[1], rep2 = z->Rep[2], rep3 = z->Rep[3];
    uint64_t processed = z->Processed;
    const uint32_t pb_mask = (1u << z->Pb) - 1, lp_mask = (1u << z->Lp) - 1, lc = z->Lc;
    int status = CBS_OK;

    if (z->Pending) {
        z->DictPos = pos;
        uint32_t n = copy_match(z, rep0, z->Pending, limit);
        z->Pending -= n;
        processed += n;
        pos = z->DictPos;
    }
    while (pos < limit) {
        uint32_t pos_state = (uint32_t)processed & pb_mask;
        if (!rc_bit(&rc, &probs->IsMatch[state][pos_state])) {
            uint32_t prev = pos ? dict[pos - 1] : z->Wrapped ? dict[z->DictSize - 1] : 0;
            uint16_t *lit = probs->Literal + 0x300u * ((((uint32_t)processed & lp_mask) << lc) + (prev >> (8 - lc)));
            uint32_t symbol = 1;
            if (state >= 7) {
                uint32_t match_byte = dict[pos >= rep0 + 1 ? pos - rep0 - 1 : pos + z->DictSize - rep0 - 1];
                do {
                    uint32_t match_bit = (match_byte >> 7) & 1;
                    match_byte <<= 1;
                    unsigned bit = rc_bit(&rc, lit + ((1 + match_bit) << 8) + symbol);
                    symbol = symbol << 1 | bit;
                    if (match_bit != bit)
                        break;
                } while (symbol < 0x100);
            }
            while (symbol < 0x100)
                symbol = symbol << 1 | rc_bit(&rc, lit + symbol);
            dict[pos++] = (uint8_t)symbol;
            processed++;
            state = state < 4 ? 0 : state < 10 ? state - 3 : state - 6;
        } else {
            uint32_t len;
            if (!rc_bit(&rc, &probs->IsRep[state])) {
                rep3 = rep2;
                rep2 = rep1;
                rep1 = rep0;
                len = length(&rc, &probs->Len, pos_state);
                state = state < 7 ? 7 : 10;
                uint32_t slot = tree(&rc, probs->PosSlot[len < 3 ? len : 3], 6);
                if (slot < 4)
                    rep0 = slot;
                else {
                    unsigned bits = (slot >> 1) - 1;
                    rep0 = (2 | (slot & 1)) << bits;
                    if (slot < 14)
                        rep0 += tree_reverse(&rc, probs->SpecPos + rep0 - slot, bits);
                    else {
                        rep0 += rc_direct(&rc, bits - 4) << 4;
                        rep0 += tree_reverse(&rc, probs->Align, 4);
                    }
                    if (rep0 == 0xFFFFFFFFu) {
                        z->Finished = true;
                        break;
                    }
                }
            } else {
                if (!rc_bit(&rc, &probs->IsRepG0[state])) {
                    if (!rc_bit(&rc, &probs->IsRep0Long[state][pos_state])) {
                        /* a single byte from the last distance. */
                        if (rep0 >= (z->Wrapped ? z->DictSize : processed)) {
                            status = CBS_CORRUPT;
                            break;
                        }
                        dict[pos] = dict[pos >= rep0 + 1 ? pos - rep0 - 1 : pos + z->DictSize - rep0 - 1];
                        pos++;
                        processed++;
                        state = state < 7 ? 9 : 11;
                        continue;
                    }
                } else {
                    uint32_t distance;
                    if (!rc_bit(&rc, &probs->IsRepG1[state]))
                        distance = rep1;
                    else {
                        if (!rc_bit(&rc, &probs->IsRepG2[state]))
                            distance = rep2;
                        else {
                            distance = rep3;
                            rep3 = rep2;
                        }
                        rep2 = rep1;
                    }
                    rep1 = rep0;
                    rep0 = distance;
                }
                len = length(&rc, &probs->RepLen, pos_state);
                state = state < 7 ? 8 : 11;
            }
            len += MATCH_MIN;
            if (rep0 >= (z->Wrapped ? z->DictSize : processed) || rep0 >= z->DictSize) {
                status = CBS_CORRUPT;
                break;
            }
            z->DictPos = pos;
            z->Rep[0] = rep0;
            uint32_t n = copy_match(z, rep0, len, limit);
            pos = z->DictPos;
            processed += n;
            z->Pending = len - n;
        }
        if (rc.Error) {
            status = CBS_CORRUPT;
            break;
        }
    }
    if (rc.Error)
        status = CBS_CORRUPT;
    z->Rc = rc;
    z->DictPos = pos;
    z->State = state;
    z->Rep[0] = rep0;
    z->Rep[1] = rep1;
    z->Rep[2] = rep2;
    z->Rep[3] = rep3;
    z->Processed = processed;
    return status;
}

static int lzma_read(CBS_STREAM *stream, uint8_t *buffer, size_t want, size_t *got)
{
    LZMA *z = (LZMA *)stream;
    size_t total = 0;
    if ((uint64_t)want > z->Remaining)
        want = (size_t)z->Remaining;
    while (total < want && !z->Finished) {
        size_t chunk = want - total;
        if (chunk > z->DictSize - z->DictPos)
            chunk = z->DictSize - z->DictPos;
        uint32_t start = z->DictPos;
        int status = decode(z, start + (uint32_t)chunk);
        uint32_t made = z->DictPos - start;
        memcpy(buffer + total, z->Dict + start, made);
        total += made;
        z->Remaining -= made;
        if (z->DictPos == z->DictSize) {
            z->DictPos = 0;
            z->Wrapped = true;
        }
        if (status != CBS_OK) {
            *got = total;
            return status;
        }
        if (!made && !z->Pending)
            break;
    }
    *got = total;
    /* the stream ended before the size the container promised. */
    if (total < want)
        return CBS_CORRUPT;
    return CBS_OK;
}

static void lzma_destroy(CBS_STREAM *stream)
{
    LZMA *z = (LZMA *)stream;
    CbsStreamDestroy(z->Input);
    CbsFreeLarge(z->Dict, z->DictSize);
    CbsFree(z->Probs);
    CbsFree(z);
}

int CbsStreamLzma(CBS_STREAM **out, const uint8_t *props, size_t prop_size, CBS_STREAM *input, uint64_t out_size)
{
    *out = nullptr;
    if (prop_size < 5 || !input->Data)
        return CBS_UNSUPPORTED;
    uint32_t d = props[0];
    if (d >= 9 * 5 * 5)
        return CBS_BAD;
    uint32_t lc = d % 9, lp = d / 9 % 5, pb = d / 45;
    uint32_t dict_size = (uint32_t)props[1] | (uint32_t)props[2] << 8 | (uint32_t)props[3] << 16 | (uint32_t)props[4] << 24;
    if (dict_size < 4096)
        dict_size = 4096;
    /* a stream that is shorter than its dictionary never reads further back than its own length. */
    if (out_size < dict_size)
        dict_size = out_size ? (uint32_t)out_size : 1;
    if (input->Size < 5 || input->Data[0] != 0)
        return CBS_BAD;
    LZMA *z = CbsAllocZero(sizeof(LZMA));
    if (!z)
        return CBS_MEMORY;
    z->Base.Read = lzma_read;
    z->Base.Destroy = lzma_destroy;
    z->Input = input;
    z->Lc = lc;
    z->Lp = lp;
    z->Pb = pb;
    z->DictSize = dict_size;
    z->Remaining = out_size;
    size_t prob_count = (size_t)0x300 << (lc + lp);
    z->Probs = CbsAlloc(sizeof(PROBS) + prob_count * sizeof(uint16_t));
    z->Dict = CbsAllocLarge(dict_size);
    if (!z->Probs || !z->Dict) {
        z->Input = nullptr;
        lzma_destroy(&z->Base);
        return CBS_MEMORY;
    }
    uint16_t *p = (uint16_t *)z->Probs;
    for (size_t i = 0; i < (sizeof(PROBS) / sizeof(uint16_t)) + prob_count; i++)
        p[i] = PROB_INIT;
    const uint8_t *in = input->Data;
    z->Rc.Range = 0xFFFFFFFFu;
    z->Rc.Code = (uint32_t)in[1] << 24 | (uint32_t)in[2] << 16 | (uint32_t)in[3] << 8 | in[4];
    z->Rc.In = in + 5;
    z->Rc.End = in + input->Size;
    *out = &z->Base;
    return CBS_OK;
}
