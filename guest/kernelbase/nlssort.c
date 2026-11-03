#include "nlssort.h"
#include <stddef.h>

#define memcpy __builtin_memcpy
#define memset __builtin_memset
#define NLSSORT_ALLOC(size) NlsSortAlloc(size)
#define NLSSORT_FREE(p) NlsSortFree(p)

typedef struct SORT_STATE {
    const NLS_SORT_TABLES *Tables;
    const NLS_SORT_ORDER *Order;
    const uint16_t *PageMap;
    const uint16_t *Start;
    const uint16_t *End;
    const uint16_t *Cur;
    uint32_t Flags;
    uint8_t Mask;
    int Stringsort, IgnoreSymbols, LingCase, LingDiacritic, Digits, ThreeByte, Compare;
    uint16_t *Open, *Lead;
    int LeadCount, OpenSpecial;
    uint8_t *Primary, *PrimaryStart;
    uint16_t *Diacritic, *DiacriticStart;
    uint8_t *Case, *CaseStart;
    uint8_t *Extra[4];
    uint8_t *ExtraStart;
    int ExtraStride;
    uint8_t *ExtraCur;
    uint8_t *Special, *SpecialStart;
} SORT_STATE;

#define W_ALNUM(w) ((uint8_t)(w))
#define W_SCRIPT(w) ((uint8_t)((w) >> 8))
#define W_DIACRITIC(w) ((uint8_t)((w) >> 16))
#define W_CASE(w) ((uint8_t)((w) >> 24))

static inline uint32_t weight_of(const SORT_STATE *s, unsigned c)
{
    return s->Tables->Pages[((unsigned)s->PageMap[c >> 8] << 8) | (c & 0xFF)];
}

static inline int cased_script(uint8_t script)
{
    return script <= 0x14 || script == 41;
}

static inline uint8_t diacritic_byte(const SORT_STATE *s, uint32_t w)
{
    return s->LingDiacritic && cased_script(W_SCRIPT(w)) ? 2 : W_DIACRITIC(w);
}

static inline uint8_t case_byte(const SORT_STATE *s, uint32_t w)
{
    return s->LingCase && cased_script(W_SCRIPT(w)) ? 2 : (uint8_t)(W_CASE(w) & 0x3F);
}

static void emit_plain(SORT_STATE *s, uint32_t w)
{
    *s->Primary++ = W_SCRIPT(w);
    *s->Primary++ = W_ALNUM(w);
    *s->Diacritic++ = diacritic_byte(s, w);
    *s->Case++ = case_byte(s, w) & s->Mask;
}

static void emit_main(SORT_STATE *s, uint32_t w, int twice)
{
    uint8_t script = W_SCRIPT(w);
    *s->Primary++ = script;
    *s->Primary++ = W_ALNUM(w);
    if ((uint8_t)(script + 87) <= 6 || (s->ThreeByte && (uint8_t)(script + 64) <= 0x39))
        *s->Primary++ = W_DIACRITIC(w);
    else
        *s->Diacritic++ = diacritic_byte(s, w);
    *s->Case++ = case_byte(s, w) & s->Mask;
    if (twice) {
        *s->Primary++ = script;
        *s->Primary++ = W_ALNUM(w);
        *s->Diacritic = diacritic_byte(s, w);
        *s->Case = case_byte(s, w) & s->Mask;
        s->Diacritic++;
        s->Case++;
    }
}

static int jamo_index(unsigned c)
{
    if (c - 0x1100 <= 0xFF)
        return (int)(c - 0x1100);
    if (c - 0xA960 <= 0x1C)
        return (int)(c - 0xA960 + 256);
    if (c - 0xD7B0 <= 0x16)
        return (int)(c - 0xD7B0 + 256 + 29);
    return (int)c - 0xD697;
}

typedef struct JAMO_STATE {
    uint8_t Flags;
    int Filler;
    int8_t Lead, Vowel, Trail;
    uint8_t LeadVariant, VowelVariant, TrailVariant;
} JAMO_STATE;

static int is_lead_jamo(unsigned c)
{
    return c - 0x1100 <= 0x5F || c - 0xA960 <= 0x1C;
}

static int is_vowel_jamo(unsigned c)
{
    return c - 0x1160 <= 0x47 || c - 0xD7B0 <= 0x16;
}

static int is_trail_jamo(unsigned c)
{
    return c - 0x11A8 <= 0x57 || c - 0xD7CB <= 0x30;
}

static void jamo_update(const uint8_t *table, unsigned c, int kind, JAMO_STATE *j)
{
    const uint8_t *t = table + 8 * (size_t)jamo_index(c);
    j->Flags |= t[0];
    if ((int8_t)t[1] > j->Lead)
        j->Lead = (int8_t)t[1];
    if ((int8_t)t[2] > j->Vowel)
        j->Vowel = (int8_t)t[2];
    if ((int8_t)t[3] > j->Trail)
        j->Trail = (int8_t)t[3];
    uint8_t *variant = kind == 1 ? &j->LeadVariant : kind == 2 ? &j->VowelVariant : &j->TrailVariant;
    if (t[4] > *variant)
        *variant = t[4];
}

/* the weight of a sequence of conjoining jamo as one syllable: the number of characters it takes and its three variants. */
static int map_old_hangul(const SORT_STATE *s, const uint16_t *text, const uint16_t *end, uint16_t *word, uint8_t variants[3])
{
    const uint16_t *window = text;
    while (window < end && window - text < 3 && *window)
        window++;
    if (window == text)
        return 0;
    unsigned c = text[0];
    unsigned lead;
    const uint16_t *pos = text;
    JAMO_STATE j;
    memset(&j, 0, sizeof(j));
    if (is_lead_jamo(c)) {
        lead = c;
        pos = text + 1;
    } else if (is_vowel_jamo(c) || is_trail_jamo(c))
        lead = 0x115F;
    else
        return 0;
    if (lead == 0x115F)
        j.Filler = 1;
    const uint8_t *table = s->Tables->Jamo;
    jamo_update(table, lead, 1, &j);
    unsigned vowel = 0x1160;
    if (pos < window && is_vowel_jamo(*pos)) {
        vowel = *pos;
        pos++;
    }
    if (vowel == 0x1160)
        j.Filler = 1;
    jamo_update(table, vowel, 2, &j);
    if (pos < window && is_trail_jamo(*pos)) {
        jamo_update(table, *pos, 3, &j);
        pos++;
    }
    if (j.Flags == 0) {
        memset(&j, 0, sizeof(j));
        jamo_update(table, text[0], 1, &j);
        jamo_update(table, 0x1160, 2, &j);
        pos = text + 1;
        j.Filler = 1;
    }
    unsigned v = (uint16_t)((uint16_t)((int)j.Lead * 21 + j.Vowel) * 28 + j.Trail) - 0x5400u;
    v &= 0xFFFF;
    uint8_t lead_variant = j.LeadVariant;
    if (j.Filler) {
        v = (v - 1) & 0xFFFF;
        if (v < 0xAC00)
            v = 0x3260;
        lead_variant = (uint8_t)(lead_variant + 0x80);
    }
    *word = (uint16_t)weight_of(s, v);
    variants[0] = lead_variant > 2 ? lead_variant : 2;
    variants[1] = j.VowelVariant > 2 ? j.VowelVariant : 2;
    variants[2] = j.TrailVariant > 2 ? j.TrailVariant : 2;
    return (int)(pos - text);
}

static uint32_t expansion_of(const NLS_SORT_TABLES *t, unsigned index)
{
    if (index < t->ExpansionCount)
        return t->Expansions[index];
    for (uint32_t i = 0; i < t->ExpansionExtraCount; i++)
        if (t->ExpansionExtra[2 * i] == index)
            return t->ExpansionExtra[2 * i + 1];
    return 0;
}

static void process_special(SORT_STATE *s, uint32_t w)
{
    uint8_t script = W_SCRIPT(w);
    uint8_t dia = W_DIACRITIC(w);
    switch (script) {
    case 0:
        if (s->Cur[0] == 0xDB40 && s->Cur + 1 < s->End && (uint16_t)(s->Cur[1] - 0xDD00) <= 0xEF)
            s->Cur++;
        return;
    case 1: {
        uint8_t v = s->LingDiacritic ? 2 : dia;
        if (s->Compare && s->LingDiacritic)
            return;
        if (s->Compare) {
            /*
             * the marks that follow each other weigh as one, with the character before them when it has a weight of
             * its own for them. Else they are a weight by themselves, among those of the characters, or before them.
             */
            if (s->OpenSpecial)
                return;
            if (s->Open)
                *s->Open = (uint8_t)(*s->Open + v);
            else if (s->Diacritic == s->DiacriticStart) {
                s->Lead[s->LeadCount] = v;
                s->Open = &s->Lead[s->LeadCount++];
            } else {
                *s->Diacritic++ = v;
                s->Open = s->Diacritic - 1;
            }
        } else if (s->Diacritic > s->DiacriticStart)
            s->Diacritic[-1] = (uint8_t)(s->Diacritic[-1] + v);
        else
            *s->Diacritic++ = v;
        return;
    }
    case 2: {
        /* the characters an expansion gives are weights as they are in a sort key, and a comparison goes through them. */
        for (;;) {
            uint32_t entry = expansion_of(s->Tables, (w >> 16) & 0xFFFF);
            uint32_t first = weight_of(s, entry & 0xFFFF);
            if (s->Compare && W_SCRIPT(first) <= 12)
                process_special(s, first);
            else
                emit_plain(s, first);
            w = weight_of(s, entry >> 16);
            if (W_SCRIPT(w) != 2)
                break;
        }
        if (s->Compare && W_SCRIPT(w) <= 12)
            process_special(s, w);
        else
            emit_plain(s, w);
        return;
    }
    case 3: {
        uint8_t byte0 = W_ALNUM(w);
        uint16_t unit = 0xFFFF;
        uint8_t a4 = (uint8_t)((s->Mask & W_CASE(w)) | 0xC4);
        int stride = s->ExtraStride;
        int advance_extra = 0;
        if (byte0 > 1) {
            unit = (uint16_t)(byte0 | 0x2200);
            s->ExtraCur[0] = (uint8_t)((s->Mask & W_CASE(w) & 2) | 0xC4);
            s->ExtraCur[stride] = 3;
            s->ExtraCur[2 * stride] = a4 & 0xE4;
            s->ExtraCur[3 * stride] = a4 & 0xC5;
            advance_extra = 1;
        } else {
            for (const uint16_t *p = s->Cur - 1; p >= s->Start; p--) {
                uint32_t w2 = weight_of(s, *p);
                uint8_t sc = W_SCRIPT(w2);
                if (sc >= 3) {
                    if (sc != 3) {
                        unit = (uint16_t)w2;
                        break;
                    }
                    if (W_ALNUM(w2) > 1) {
                        unit = (uint16_t)(W_ALNUM(w2) | 0x2200);
                        uint8_t b = (uint8_t)((s->Mask & W_CASE(w2)) | 0xC4);
                        s->ExtraCur[0] = (uint8_t)((s->Mask & W_CASE(w2) & 2) | 0xC4);
                        s->ExtraCur[2 * stride] = b & 0xE4;
                        if (byte0 == 0) {
                            s->ExtraCur[stride] = 4;
                            s->ExtraCur[3 * stride] = b & 0xC5;
                        } else {
                            s->ExtraCur[stride] = 5;
                            unit = (uint16_t)((W_ALNUM(w2) & 0x87) | 0x2200);
                            s->ExtraCur[3 * stride] = a4 & 0xC5;
                        }
                        advance_extra = 1;
                        break;
                    }
                } else if (sc == 2)
                    break;
            }
        }
        *s->Diacritic = s->LingDiacritic ? 2 : dia;
        *s->Case = 2;
        *s->Primary++ = (uint8_t)(unit >> 8);
        *s->Primary++ = (uint8_t)unit;
        uint8_t hi = (uint8_t)(unit >> 8);
        if ((uint8_t)(hi + 87) <= 6 || (s->ThreeByte && (uint8_t)(hi + 64) <= 0x39)) {
            const uint16_t *p = s->Cur - 1;
            *s->Primary++ = p < s->Start ? 0 : W_DIACRITIC(weight_of(s, *p));
        }
        if (advance_extra)
            s->ExtraCur++;
        if (unit != 0xFFFF) {
            s->Diacritic++;
            s->Case++;
        }
        return;
    }
    case 4: {
        uint16_t word;
        uint8_t variants[3];
        int taken = map_old_hangul(s, s->Cur, s->End, &word, variants);
        if (taken > 0) {
            *s->Primary++ = (uint8_t)(word >> 8);
            *s->Primary++ = (uint8_t)word;
            for (int i = 0; i < 3; i++) {
                *s->Primary++ = 0xFF;
                *s->Primary++ = variants[i];
            }
            *s->Diacritic++ = 2;
            *s->Case++ = 2;
            s->Cur += taken - 1;
            return;
        }
        *s->Primary++ = W_ALNUM(w);
        *s->Primary++ = dia;
        *s->Diacritic++ = 2;
        *s->Case++ = W_CASE(w) & 0x3F;
        return;
    }
    case 5:
        *s->Primary++ = 0xFD;
        *s->Primary++ = 0xFF;
        *s->Primary++ = W_ALNUM(w);
        *s->Primary++ = dia;
        *s->Diacritic++ = 2;
        *s->Case++ = 2;
        return;
    case 6:
        if (!s->Stringsort) {
            unsigned position = (uint16_t)~(uint16_t)((unsigned)(s->Primary - s->PrimaryStart) >> 1);
            if (position <= 1)
                position = 2;
            uint8_t d = s->LingDiacritic && cased_script(script) ? 2 : dia;
            uint8_t c = (s->LingCase && cased_script(script)) ? 2 : (uint8_t)(W_CASE(w) & 0x3F);
            unsigned value = (uint16_t)((((unsigned)d * 8) | (uint8_t)(c & s->Mask)) + ((unsigned)W_ALNUM(w) << 8));
            *s->Special++ = (uint8_t)(position >> 8);
            *s->Special++ = (uint8_t)position;
            *s->Special++ = (uint8_t)(value >> 8);
            *s->Special++ = (uint8_t)value;
            return;
        }
        [[fallthrough]];
    default:
        if (script >= 6 && script <= 12) {
            if (s->IgnoreSymbols)
                return;
            *s->Primary++ = script;
            *s->Primary++ = W_ALNUM(w);
            *s->Diacritic++ = dia;
            *s->Case++ = (uint8_t)(s->Mask & W_CASE(w) & 0x3F);
        }
        return;
    }
}

static const uint16_t g_digit_ranges[] = {
    0x30,   0x39,   0x660,  0x669,  0x6f0,  0x6f9,  0x966,  0x96f,  0x9e6,  0x9ef,  0xa66,  0xa6f,  0xae6,  0xaef,
    0xb66,  0xb6f,  0xbe6,  0xbef,  0xc66,  0xc6f,  0xce6,  0xcef,  0xd66,  0xd6f,  0xe50,  0xe59,  0xed0,  0xed9,
    0xf20,  0xf29,  0x1040, 0x1049, 0x1090, 0x1099, 0x17e0, 0x17e9, 0x1810, 0x1819, 0x1946, 0x194f, 0x1bb0, 0x1bb9,
    0x1c40, 0x1c49, 0x1c50, 0x1c59, 0xa620, 0xa629, 0xa8d0, 0xa8d9, 0xa900, 0xa909, 0xaa50, 0xaa59, 0xff10, 0xff19};
static const uint16_t g_surrogate_digit_ranges[] = {0xdfce, 0xdfd7, 0xdfd8, 0xdfe1, 0xdfe2, 0xdfeb, 0xdfec, 0xdff5, 0xdff6, 0xdfff};
static const uint8_t g_extra_drop[4] = {0xC6, 0x03, 0xE4, 0xC5};
static const uint8_t g_extra_separator[4] = {0xFF, 0x02, 0xFF, 0xFF};

/* the first character of the range of digits the character is in, or 0. */
static unsigned digit_zero(const uint16_t *ranges, int count, unsigned c)
{
    int lo = 0, hi = count - 1, mid = 0;
    while (lo <= hi) {
        mid = (lo + hi) >> 1;
        if (c == ranges[mid])
            break;
        if (c >= ranges[mid])
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    int start = mid & ~1;
    if (ranges[start] > c || ranges[start + 1] < c)
        return 0;
    return ranges[start];
}

/* the longest contraction at the position: its weight and the number of characters it takes, or 0. */
static int order_has_contractions(const NLS_SORT_ORDER *o)
{
    int any = o->PairIndex != 0;
    for (int len = 2; len <= 8; len++)
        any |= o->Count[len] != 0;
    return any;
}

static int find_contraction(SORT_STATE *s, uint32_t *weight, int *twice)
{
    const NLS_SORT_ORDER *o = s->Order;
    const uint16_t *cur = s->Cur;
    int double_compression = (o->Flags & NLS_SORT_DOUBLE_COMPRESSION) != 0;
    ptrdiff_t remaining = s->End - cur;
    if (o->PairIndex) {
        if (remaining >= 2 && (unsigned)cur[0] - 0xD800u < 0x400u && (unsigned)cur[1] - 0xDC00u < 0x400u && o->PairIndex[cur[0] - 0xD800u]) {
            const uint8_t *p = o->PairData + ((size_t)(o->PairIndex[cur[0] - 0xD800u] - 1) * 1024 + (cur[1] - 0xDC00u)) * 3;
            uint32_t w = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
            if (w) {
                *weight = w | 0x02000000u;
                *twice = 0;
                return 2;
            }
        }
        return 0;
    }
    for (int len = 8; len >= 2; len--) {
        if (!o->Count[len])
            continue;
        int cap = len + double_compression;
        if (remaining < len)
            continue;
        int run = 0;
        while (run < cap && run < remaining && cur[run] <= o->Max && cur[run] >= o->Min)
            run++;
        if (run < len)
            continue;
        int skip = 0;
        if (double_compression && remaining > len) {
            uint32_t a = weight_of(s, cur[0]), b = weight_of(s, cur[1]);
            if (((a ^ b) & 0xE7FFFFFF) == 0)
                skip = 1;
        }
        int words = (len + 2 + 1) & ~1;
        const uint16_t *table = o->Entries[len];
        int lo = 0, hi = o->Count[len] - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            const uint16_t *entry = table + (size_t)mid * words;
            int diff = 0;
            for (int j = 0; j < len && !diff; j++)
                diff = (int)cur[skip + j] - (int)entry[j];
            if (!diff) {
                const uint16_t *w = entry + words - 2;
                *weight = (uint32_t)w[0] | ((uint32_t)w[1] << 16);
                *twice = skip;
                return skip + len;
            }
            if (diff >= 0)
                lo = mid + 1;
            else
                hi = mid - 1;
        }
    }
    return 0;
}

/* a run of digits as a number: the digit count and the digits, which is what sorts them by value. */
static int emit_number(SORT_STATE *s, uint32_t w)
{
    const uint16_t *p = s->Cur;
    unsigned c = *p;
    unsigned zero;
    unsigned high = 0;
    if ((uint16_t)(c - 0xDC00) <= 0x3FF && p > s->Start) {
        if (p[-1] != 0xD835)
            return 0;
        zero = digit_zero(g_surrogate_digit_ranges, 10, c);
        high = 0xD835;
    } else {
        zero = digit_zero(g_digit_ranges, 56, c);
    }
    if (!zero)
        return 0;
    int zeros = 0;
    const uint16_t *cur = p;
    while (*p == zero) {
        p++;
        zeros++;
        cur = p;
        if (high) {
            if (p >= s->End || *p != high) {
                cur = ++p;
                break;
            }
            cur = ++p;
        }
        if (p >= s->End || zeros >= 19)
            break;
    }
    int digits = 0;
    if (p < s->End && zeros < 19) {
        int total = zeros;
        while (p < s->End && total < 19) {
            unsigned d = *p;
            if (d < zero || d - 9 > zero)
                break;
            digits++;
            total++;
            p++;
            if (high) {
                if (p >= s->End || *p != high)
                    break;
                p++;
            }
        }
    }
    *s->Primary++ = 13;
    *s->Primary++ = 2;
    *s->Primary++ = (uint8_t)(digits + 2);
    *s->Diacritic++ = W_DIACRITIC(w);
    *s->Case++ = (uint8_t)(s->Mask & W_CASE(w) & 0x3F);
    if (digits & 1)
        *s->Primary = 0x20;
    for (int left = digits; left > 0; left--) {
        uint8_t nibble = (uint8_t)((uint8_t)*cur - (uint8_t)zero + 2);
        if (left & 1) {
            *s->Primary |= nibble;
            s->Primary++;
        } else
            *s->Primary = (uint8_t)(nibble << 4);
        cur++;
        if (high)
            cur++;
    }
    *s->Primary++ = (uint8_t)(0xFE - zeros);
    s->Cur = high ? cur - 2 : cur - 1;
    return 1;
}

static void weigh_one(SORT_STATE *s, uint32_t w, int has_contractions)
{
    uint8_t script = W_SCRIPT(w);
    if (script > 12) {
        if (has_contractions) {
            uint32_t cw;
            int twice = 0;
            int taken = find_contraction(s, &cw, &twice);
            if (taken) {
                s->Cur += taken - 1;
                if (W_SCRIPT(cw) == 13 && s->Digits && emit_number(s, cw))
                    return;
                emit_main(s, cw, twice);
                return;
            }
        }
        if (script == 13 && s->Digits && emit_number(s, w))
            return;
        emit_main(s, w, 0);
    } else
        process_special(s, w);
}

/* the weights of a string, in the arrays of the state, which come from the stack when they fit and from the heap when not. */
static uint8_t *weigh(SORT_STATE *s, const NLS_SORT_TABLES *tables, const NLS_SORT_ORDER *order, uint32_t flags,
                      const uint16_t *text, int length, int begin, uint8_t *stack, size_t stack_size)
{
    memset(s, 0, sizeof(*s));
    s->Tables = tables;
    s->Order = order;
    s->Flags = flags;
    s->PageMap = (flags & NLS_SORT_LINGUISTIC_CASING) ? order->LingPageMap : order->PageMap;
    if (!s->PageMap)
        s->PageMap = order->PageMap;
    s->Start = text;
    s->End = text + length;
    switch (flags & (NLS_SORT_IGNORECASE | NLS_SORT_IGNOREKANATYPE | NLS_SORT_IGNOREWIDTH)) {
    case NLS_SORT_IGNORECASE:
        s->Mask = 0xE7;
        break;
    case NLS_SORT_IGNOREKANATYPE:
        s->Mask = 0xDF;
        break;
    case NLS_SORT_IGNOREWIDTH:
        s->Mask = 0xFE;
        break;
    case NLS_SORT_IGNORECASE | NLS_SORT_IGNOREKANATYPE:
        s->Mask = 0xC7;
        break;
    case NLS_SORT_IGNORECASE | NLS_SORT_IGNOREWIDTH:
        s->Mask = 0xE6;
        break;
    case NLS_SORT_IGNOREKANATYPE | NLS_SORT_IGNOREWIDTH:
        s->Mask = 0xDE;
        break;
    case NLS_SORT_IGNORECASE | NLS_SORT_IGNOREKANATYPE | NLS_SORT_IGNOREWIDTH:
        s->Mask = 0xC6;
        break;
    default:
        s->Mask = 0xFF;
    }
    s->Stringsort = (flags & NLS_SORT_STRINGSORT) != 0;
    s->IgnoreSymbols = (flags & NLS_SORT_IGNORESYMBOLS) != 0;
    s->LingCase = (flags & NLS_SORT_LINGUISTIC_IGNORECASE) != 0;
    s->LingDiacritic = (flags & NLS_SORT_LINGUISTIC_IGNOREDIACRITIC) != 0;
    s->Digits = (flags & NLS_SORT_DIGITSASNUMBERS) != 0;
    s->ThreeByte = (order->Flags & NLS_SORT_THREE_BYTE_WEIGHTS) != 0;
    s->Compare = (flags & NLS_SORT_COMPARE) != 0;

    size_t units = (size_t)length * 12 + 16;
    size_t total = units * 15 + 64;
    uint8_t *arena = total <= stack_size ? stack : NLSSORT_ALLOC(total);
    if (!arena)
        return NULL;
    uint8_t *a = arena;
    s->Lead = (uint16_t *)a;
    a += units * 2;
    s->DiacriticStart = s->Diacritic = (uint16_t *)a;
    a += units * 2;
    s->PrimaryStart = s->Primary = a;
    a += units * 4;
    s->CaseStart = s->Case = a;
    a += units;
    s->ExtraStride = (int)units;
    s->ExtraStart = s->ExtraCur = a;
    for (int i = 0; i < 4; i++)
        s->Extra[i] = a + (size_t)i * units;
    a += units * 4;
    s->SpecialStart = s->Special = a;
    a += units * 2;

    int has_contractions = order_has_contractions(order);

    s->Cur = text + begin;
    while (s->Cur < s->End) {
        uint32_t w = weight_of(s, *s->Cur);
        uint8_t script = W_SCRIPT(w);
        uint16_t *d_before = s->Diacritic;
        weigh_one(s, w, has_contractions);
        s->Cur++;
        if (s->Diacritic != d_before && script != 1)
            s->Open = s->Diacritic - 1;
        else if (script != 1)
            s->Open = NULL;
        if (script != 1)
            s->OpenSpecial = script == 6 && !s->Stringsort;
    }
    return arena;
}

static uint32_t weight_of_char(const NLS_SORT_ORDER *order, const NLS_SORT_TABLES *tables, uint32_t flags, unsigned c)
{
    const uint16_t *map = (flags & NLS_SORT_LINGUISTIC_CASING) ? order->LingPageMap : order->PageMap;
    if (!map)
        map = order->PageMap;
    return tables->Pages[((unsigned)map[c >> 8] << 8) | (c & 0xFF)];
}

static int text_length(const uint16_t *text, int length)
{
    if (length < 0) {
        length = 0;
        while (text[length])
            length++;
        length++;
    }
    return length;
}

/*
 * the weights laid out in order as the values of a key: the primary weights, 1, the diacritics, 1, the cases, 1, the extra
 * weights and 1, the special weights and 0. A value is a byte, but for the diacritics of a comparison. The values are
 * written when out is not null, and the count is the result.
 */
static int assemble(const SORT_STATE *s, uint32_t flags, const NLS_SORT_ORDER *order, uint16_t *out)
{
    int n = 0;
#define PUT(v)                  \
    do {                        \
        uint16_t put_ = (v);    \
        if (out)                \
            out[n] = put_;      \
        n++;                    \
    } while (0)
    for (const uint8_t *p = s->PrimaryStart; p < s->Primary; p++)
        PUT(*p);
    PUT(1);
    const uint16_t *d_start = s->DiacriticStart, *d_end = s->Diacritic;
    if (!(flags & NLS_SORT_IGNORENONSPACE)) {
        /* the marks that come before any character weigh as one, before the diacritics of the characters. */
        if (s->Compare) {
            for (int i = 0; i < s->LeadCount; i++)
                PUT(s->Lead[i]);
            PUT(0);
        }
        if (order->Flags & NLS_SORT_REVERSE_DIACRITICS) {
            while (d_start < d_end && *d_start <= 2)
                d_start++;
            for (const uint16_t *p = d_end; p > d_start;)
                PUT(*--p);
        } else {
            while (d_end > d_start && d_end[-1] <= 2)
                d_end--;
            for (const uint16_t *p = d_start; p < d_end; p++)
                PUT(*p);
        }
    }
    PUT(1);
    if ((flags & (NLS_SORT_IGNORECASE | NLS_SORT_IGNOREWIDTH)) != (NLS_SORT_IGNORECASE | NLS_SORT_IGNOREWIDTH)) {
        const uint8_t *c_end = s->Case;
        while (c_end > s->CaseStart && c_end[-1] <= 2)
            c_end--;
        for (const uint8_t *p = s->CaseStart; p < c_end; p++)
            PUT(*p);
    }
    PUT(1);
    int extra_count = (int)(s->ExtraCur - s->ExtraStart);
    if (extra_count) {
        int first_level = 0;
        if (flags & NLS_SORT_IGNORENONSPACE) {
            first_level = 2;
            PUT(0xFF);
            PUT(0x02);
        }
        for (int l = first_level; l < 4; l++) {
            int count = extra_count;
            while (count > 0 && s->Extra[l][count - 1] == g_extra_drop[l])
                count--;
            for (int i = 0; i < count; i++)
                PUT(s->Extra[l][i]);
            PUT(g_extra_separator[l]);
        }
    }
    PUT(1);
    if (!s->IgnoreSymbols)
        for (const uint8_t *p = s->SpecialStart; p < s->Special; p++)
            PUT(*p);
    PUT(0);
#undef PUT
    return n;
}

#define WEIGH_STACK_WORDS 1024

int NlsSortKey(const NLS_SORT_TABLES *tables, const NLS_SORT_ORDER *order, uint32_t flags, const uint16_t *text, int length,
               uint8_t *out, int size)
{
    SORT_STATE st;
    SORT_STATE *s = &st;
    uint64_t stack64[WEIGH_STACK_WORDS];
    uint8_t *stack = (uint8_t *)stack64;
    length = text_length(text, length);
    flags &= ~NLS_SORT_COMPARE;
    uint8_t *arena = weigh(s, tables, order, flags, text, length, 0, stack, sizeof(stack64));
    if (!arena)
        return -2;
    int count = assemble(s, flags, order, NULL);
    int result = count;
    if (size != 0) {
        if (size < count)
            result = -1;
        else {
            uint16_t *values = NLSSORT_ALLOC((size_t)count * 2);
            if (!values)
                result = -2;
            else {
                assemble(s, flags, order, values);
                for (int i = 0; i < count; i++)
                    out[i] = (uint8_t)values[i];
                NLSSORT_FREE(values);
            }
        }
    }
    if (arena != stack)
        NLSSORT_FREE(arena);
    return result;
}

/*
 * strings of letters and digits of the ASCII set, which weigh by their primary weight and their case alone: 0 is returned
 * when a character is not one of them, or the order has a contraction, and the weights are then done in full.
 */
static int compare_plain(const NLS_SORT_TABLES *tables, const NLS_SORT_ORDER *order, uint32_t flags, const uint16_t *a, int alen,
                         const uint16_t *b, int blen)
{
    const uint16_t *map = (flags & NLS_SORT_LINGUISTIC_CASING) ? order->LingPageMap : order->PageMap;
    uint8_t mask = (flags & NLS_SORT_IGNORECASE) ? 0xE7 : 0xFF;
    int case_result = 2;
    int n = alen < blen ? alen : blen;
    int i;
    for (i = 0; i < n; i++) {
        unsigned ca = a[i], cb = b[i];
        if (ca > 0x7F || cb > 0x7F)
            return 0;
        uint32_t wa = tables->Pages[((unsigned)map[0] << 8) | ca], wb = tables->Pages[((unsigned)map[0] << 8) | cb];
        uint8_t sa = W_SCRIPT(wa), sb = W_SCRIPT(wb);
        if ((sa != 13 && sa != 14) || (sb != 13 && sb != 14) || W_DIACRITIC(wa) != 2 || W_DIACRITIC(wb) != 2)
            return 0;
        if ((uint16_t)wa != (uint16_t)wb) {
            /* the rest of both strings has to be plain too for the order to be settled here. */
            for (int j = i + 1; j < alen; j++)
                if (a[j] > 0x7F)
                    return 0;
            for (int j = i + 1; j < blen; j++)
                if (b[j] > 0x7F)
                    return 0;
            return (uint16_t)wa < (uint16_t)wb ? 1 : 3;
        }
        if (case_result == 2) {
            uint8_t ka = W_CASE(wa) & 0x3F & mask, kb = W_CASE(wb) & 0x3F & mask;
            if (ka != kb)
                case_result = ka < kb ? 1 : 3;
        }
    }
    for (int j = i; j < alen; j++) {
        unsigned c = a[j];
        if (c > 0x7F)
            return 0;
        uint32_t w = tables->Pages[((unsigned)map[0] << 8) | c];
        if ((W_SCRIPT(w) != 13 && W_SCRIPT(w) != 14) || W_DIACRITIC(w) != 2)
            return 0;
    }
    for (int j = i; j < blen; j++) {
        unsigned c = b[j];
        if (c > 0x7F)
            return 0;
        uint32_t w = tables->Pages[((unsigned)map[0] << 8) | c];
        if ((W_SCRIPT(w) != 13 && W_SCRIPT(w) != 14) || W_DIACRITIC(w) != 2)
            return 0;
    }
    if (alen != blen)
        return alen < blen ? 1 : 3;
    return case_result;
}

int NlsSortCompare(const NLS_SORT_TABLES *tables, const NLS_SORT_ORDER *order, uint32_t flags, const uint16_t *a, int alen,
                   const uint16_t *b, int blen)
{
    SORT_STATE sa, sb;
    uint64_t stack_a[WEIGH_STACK_WORDS], stack_b[WEIGH_STACK_WORDS];
    alen = text_length(a, alen);
    blen = text_length(b, blen);
    int has_contractions = order_has_contractions(order);
    if (!has_contractions && !(flags & (NLS_SORT_DIGITSASNUMBERS | NLS_SORT_IGNORENONSPACE | NLS_SORT_IGNOREWIDTH | NLS_SORT_IGNOREKANATYPE |
                                         NLS_SORT_LINGUISTIC_IGNORECASE | NLS_SORT_LINGUISTIC_IGNOREDIACRITIC | NLS_SORT_IGNORESYMBOLS))) {
        int r = compare_plain(tables, order, flags, a, alen, b, blen);
        if (r)
            return r;
    }
    flags |= NLS_SORT_COMPARE;
    int begin = 0;
    /* what the two strings start with alike is not weighed, but for a jamo, which goes with what follows it. */
    if (!has_contractions && !(flags & NLS_SORT_DIGITSASNUMBERS)) {
        int p = 0;
        while (p < alen && p < blen && a[p] == b[p] && a[p])
            p++;
        if (p == alen && p == blen)
            return 2;
        if (p > 0) {
            unsigned prev = a[p - 1];
            if ((prev - 0x1100 <= 0xFF) || (prev - 0xA960 <= 0x1C) || (prev - 0xD7B0 <= 0x16) || (prev - 0xD7CB <= 0x30))
                p = 0;
            else if (prev == 0xDB40)
                p--;
        }
        begin = p;
    }
    uint8_t *arena_a = weigh(&sa, tables, order, flags, a, alen, begin, (uint8_t *)stack_a, sizeof(stack_a));
    uint8_t *arena_b = arena_a ? weigh(&sb, tables, order, flags, b, blen, begin, (uint8_t *)stack_b, sizeof(stack_b)) : NULL;
    int result = -2;
    if (arena_a && arena_b) {
        int na = assemble(&sa, flags, order, NULL), nb = assemble(&sb, flags, order, NULL);
        uint16_t small[256];
        uint16_t *va = na + nb <= 256 ? small : NLSSORT_ALLOC((size_t)(na + nb) * 2);
        if (va) {
            uint16_t *vb = va + na;
            assemble(&sa, flags, order, va);
            assemble(&sb, flags, order, vb);
            int n = na < nb ? na : nb, c = 0;
            for (int i = 0; i < n && !c; i++)
                c = (int)va[i] - (int)vb[i];
            if (!c)
                c = na - nb;
            result = c < 0 ? 1 : c > 0 ? 3 : 2;
            if (va != small)
                NLSSORT_FREE(va);
        }
    }
    if (arena_a && arena_a != (uint8_t *)stack_a)
        NLSSORT_FREE(arena_a);
    if (arena_b && arena_b != (uint8_t *)stack_b)
        NLSSORT_FREE(arena_b);
    return result;
}

static uint32_t hash_bytes(uint32_t h, const uint8_t *p, const uint8_t *end)
{
    for (; p != end; p++)
        if (*p > 2)
            h = 131 * (h ^ *p);
    return h;
}

uint32_t NlsSortHash(const NLS_SORT_TABLES *tables, const NLS_SORT_ORDER *order, uint32_t flags, const uint16_t *text, int length)
{
    SORT_STATE st;
    SORT_STATE *s = &st;
    uint64_t stack64[WEIGH_STACK_WORDS];
    uint8_t *stack = (uint8_t *)stack64;
    length = text_length(text, length);
    flags &= ~NLS_SORT_COMPARE;
    uint8_t *arena = weigh(s, tables, order, flags, text, length, 0, stack, sizeof(stack64));
    if (!arena)
        return 0;
    const uint32_t seed = 0x811C9DC5u;
    uint32_t primary = hash_bytes(seed, s->PrimaryStart, s->Primary), diacritic = seed, letter_case = seed, extra = seed, special = seed;
    /* a string with nothing to weigh at the first level hashes as the empty one, whatever else it holds. */
    int empty = primary == seed;
    if (!empty && !(flags & NLS_SORT_IGNORENONSPACE)) {
        for (const uint16_t *p = s->DiacriticStart; p < s->Diacritic; p++)
            if (*p > 2)
                diacritic = 131 * (diacritic ^ (uint8_t)*p);
        extra = hash_bytes(seed, s->ExtraStart, s->ExtraCur);
    }
    if (!empty && (flags & (NLS_SORT_IGNORECASE | NLS_SORT_IGNOREWIDTH)) != (NLS_SORT_IGNORECASE | NLS_SORT_IGNOREWIDTH))
        letter_case = hash_bytes(seed, s->CaseStart, s->Case);
    if (!empty && !s->IgnoreSymbols)
        special = hash_bytes(seed, s->SpecialStart, s->Special);
    if (arena != stack)
        NLSSORT_FREE(arena);
    const uint32_t k = 0x9E3779B9u;
    uint32_t x = k * primary;
    x ^= x >> 16;
    x = k * (diacritic + x);
    x ^= x >> 16;
    x = k * (letter_case + x);
    x ^= x >> 16;
    x = k * (extra + x);
    x ^= x >> 16;
    x = k * (special + x);
    return x ^ (x >> 16);
}

typedef struct FIND_STATE {
    const NLS_SORT_TABLES *Tables;
    const NLS_SORT_ORDER *Order;
    uint32_t Flags;
} FIND_STATE;

static int find_script(const FIND_STATE *f, unsigned c)
{
    return W_SCRIPT(weight_of_char(f->Order, f->Tables, f->Flags, c));
}

/* a character the order takes no notice of: the ones without a weight, the symbols and the marks when the flags say so. */
static int find_ignored(const FIND_STATE *f, const uint16_t *text, int i, int n)
{
    int script = find_script(f, text[i]);
    if (script == 0 || ((f->Flags & NLS_SORT_IGNORESYMBOLS) && script >= 6 && script <= 12) ||
        ((f->Flags & (NLS_SORT_IGNORENONSPACE | NLS_SORT_LINGUISTIC_IGNOREDIACRITIC)) && script == 1))
        return 1;
    /* the variation selectors of the supplementary plane are a pair. */
    if (i > 0 && text[i - 1] == 0xDB40 && text[i] >= 0xDD00 && text[i] <= 0xDDEF)
        return 1;
    return text[i] == 0xDB40 && i + 1 < n && text[i + 1] >= 0xDD00 && text[i + 1] <= 0xDDEF;
}

/* whether a character is the first of a contraction of the order, which the tables keep sorted by their characters. */
static int starts_contraction(const NLS_SORT_ORDER *o, unsigned c)
{
    if (o->PairIndex)
        return c - 0xD800u < 0x400u && o->PairIndex[c - 0xD800u];
    if (c < o->Min || c > o->Max)
        return 0;
    for (int len = 2; len <= 8; len++) {
        int count = o->Count[len];
        if (!count)
            continue;
        int words = (len + 2 + 1) & ~1;
        const uint16_t *table = o->Entries[len];
        int lo = 0, hi = count;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (table[(size_t)mid * words] < c)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo < count && table[(size_t)lo * words] == c)
            return 1;
    }
    return 0;
}

#define FIND_PRIMARIES 12

/* the primary weights of the contractions that start with a character, added to the list; 0 when they do not fit or are too many to tell. */
static int contraction_primaries(const NLS_SORT_ORDER *o, unsigned c, uint16_t *list, int *count)
{
    if (o->PairIndex)
        return 0;
    for (int len = 2; len <= 8; len++) {
        int n = o->Count[len];
        if (!n)
            continue;
        int words = (len + 2 + 1) & ~1;
        const uint16_t *table = o->Entries[len];
        int lo = 0, hi = n;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (table[(size_t)mid * words] < c)
                lo = mid + 1;
            else
                hi = mid;
        }
        for (; lo < n && table[(size_t)lo * words] == c; lo++) {
            uint16_t primary = table[(size_t)lo * words + words - 2];
            if ((primary >> 8) <= 13)
                return 0;
            int known = 0;
            for (int k = 0; k < *count; k++)
                known |= list[k] == primary;
            if (known)
                continue;
            if (*count == FIND_PRIMARIES)
                return 0;
            list[(*count)++] = primary;
        }
    }
    return 1;
}

/* whether a match can start with a character: the first weight it can give has to be one the value starts with. */
static int start_possible(const NLS_SORT_ORDER *order, const NLS_SORT_TABLES *tables, uint32_t flags, unsigned c, int next,
                          const uint16_t *allowed, int allowed_count)
{
    uint32_t w = weight_of_char(order, tables, flags, c);
    uint8_t script = W_SCRIPT(w);
    uint16_t mine[FIND_PRIMARIES];
    int count = 0;
    if (script <= 6)
        return 1;
    if (script > 12)
        mine[count++] = (uint16_t)w;
    if (starts_contraction(order, c) && !contraction_primaries(order, c, mine, &count))
        return 1;
    if ((order->Flags & NLS_SORT_DOUBLE_COMPRESSION) && next >= 0 && starts_contraction(order, (unsigned)next) &&
        !contraction_primaries(order, (unsigned)next, mine, &count))
        return 1;
    for (int i = 0; i < count; i++)
        for (int k = 0; k < allowed_count; k++)
            if (mine[i] == allowed[k])
                return 1;
    return 0;
}

static int find_impl(const NLS_SORT_TABLES *tables, const NLS_SORT_ORDER *order, uint32_t flags, const uint16_t *text, int length,
                     const uint16_t *value, int value_length, int *found_length)
{
    FIND_STATE fs = {tables, order, flags & ~(NLS_SORT_FIND_FROMSTART | NLS_SORT_FIND_FROMEND | NLS_SORT_FIND_STARTSWITH | NLS_SORT_FIND_ENDSWITH)};
    uint32_t position = flags & (NLS_SORT_FIND_FROMSTART | NLS_SORT_FIND_FROMEND | NLS_SORT_FIND_STARTSWITH | NLS_SORT_FIND_ENDSWITH);
    if (!position)
        position = NLS_SORT_FIND_FROMSTART;
    int from_end = position == NLS_SORT_FIND_FROMEND || position == NLS_SORT_FIND_ENDSWITH;
    int value_empty = 1;
    for (int i = 0; i < value_length; i++)
        if (!find_ignored(&fs, value, i, value_length))
            value_empty = 0;
    if (value_empty) {
        *found_length = 0;
        return from_end ? length : 0;
    }
    int first = length, last = -1;
    for (int i = 0; i < length; i++)
        if (!find_ignored(&fs, text, i, length)) {
            if (first == length)
                first = i;
            last = i;
        }
    int trailing = 0;
    while (trailing < value_length && find_ignored(&fs, value, value_length - 1 - trailing, value_length))
        trailing++;
    /* a character that is in no contraction weighs by itself, so a start whose primary weight is not one the value can begin with is no start. */
    int filter = 0, allowed_count = 0;
    uint16_t allowed[FIND_PRIMARIES];
    for (int i = 0; i < value_length; i++)
        if (!find_ignored(&fs, value, i, value_length)) {
            uint32_t w = weight_of_char(order, tables, flags, value[i]);
            if (W_SCRIPT(w) > 13) {
                allowed[allowed_count++] = (uint16_t)w;
                filter = !starts_contraction(order, value[i]) || contraction_primaries(order, value[i], allowed, &allowed_count);
                /* with double compression a pair of the same letter weighs from its second one. */
                if (filter && (order->Flags & NLS_SORT_DOUBLE_COMPRESSION) && i + 1 < value_length && starts_contraction(order, value[i + 1]))
                    filter = contraction_primaries(order, value[i + 1], allowed, &allowed_count);
            }
            break;
        }
    for (int step = 0; step <= length; step++) {
        int start = from_end ? length - step : step;
        if (position == NLS_SORT_FIND_STARTSWITH && start > first)
            break;
        if (start >= length || find_ignored(&fs, text, start, length))
            continue;
        if (position == NLS_SORT_FIND_STARTSWITH && start != first)
            continue;
        if (filter && !start_possible(order, tables, flags, text[start], start + 1 < length ? text[start + 1] : -1, allowed, allowed_count))
            continue;
        for (int end = start; end <= length; end++) {
            /* what a value matches is not longer than a few weights for each of its characters. */
            if (end - start > 8 * value_length + 16)
                break;
            if (end < length && find_script(&fs, text[end]) == 1 && !find_ignored(&fs, text, end, length))
                continue;
            if (NlsSortCompare(tables, order, fs.Flags, text + start, end - start, value, value_length) != 2)
                continue;
            int stop = end, extra = 0;
            while (extra < trailing && stop < length && find_ignored(&fs, text, stop, length)) {
                stop++;
                extra++;
            }
            if (stop < length && find_script(&fs, text[stop]) == 1 && !find_ignored(&fs, text, stop, length))
                continue;
            if (position == NLS_SORT_FIND_ENDSWITH && stop <= last)
                continue;
            *found_length = stop - start;
            return start;
        }
    }
    return -1;
}

/* a search takes the null character for a character that only another one equals, though a comparison skips it. */
int NlsSortFind(const NLS_SORT_TABLES *tables, const NLS_SORT_ORDER *order, uint32_t flags, const uint16_t *text, int length,
                const uint16_t *value, int value_length, int *found_length)
{
    int has_null = 0;
    for (int i = 0; i < length; i++)
        has_null |= text[i] == 0;
    for (int i = 0; i < value_length; i++)
        has_null |= value[i] == 0;
    if (!has_null)
        return find_impl(tables, order, flags, text, length, value, value_length, found_length);
    uint16_t *copy = NlsSortAlloc((size_t)(length + value_length) * sizeof(uint16_t));
    if (!copy)
        return -1;
    for (int i = 0; i < length; i++)
        copy[i] = text[i] ? text[i] : 0xE000;
    for (int i = 0; i < value_length; i++)
        copy[length + i] = value[i] ? value[i] : 0xE000;
    int result = find_impl(tables, order, flags, copy, length, copy + length, value_length, found_length);
    NlsSortFree(copy);
    return result;
}

/* whether the characters of a string have a place in the order: the ones with a weight, and the few without one that are defined. */
int NlsSortIsDefined(const NLS_SORT_TABLES *tables, const NLS_SORT_ORDER *order, const uint16_t *text, int length)
{
    const uint16_t *map = order->PageMap;
    for (int i = 0; i < length; i++) {
        unsigned c = text[i];
        uint32_t w = tables->Pages[((unsigned)map[c >> 8] << 8) | (c & 0xFF)];
        if (W_SCRIPT(w)) {
            if (c - 0xDB80 <= 0x1D7F)
                return 0;
            if (c - 0xD800 <= 0x3FF) {
                if (++i >= length || (unsigned)text[i] - 0xDC00 > 0x3FF)
                    return 0;
            }
            continue;
        }
        if (c == 0xDB40) {
            if (i + 1 < length && (unsigned)text[i + 1] - 0xDD00 <= 0xEF) {
                i++;
                continue;
            }
            return 0;
        }
        if (c == 0 || c == 0xAD || c == 0x34F || c == 0x640 || c == 0xECC || c == 0x1806 || (c >= 0x180B && c <= 0x180D) ||
            (c >= 0x200C && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) || (c >= 0x2060 && c <= 0x2064) ||
            (c >= 0x206A && c <= 0x206F) || c == 0x3190 || c == 0x3191 || (c >= 0xFE00 && c <= 0xFE0F) || (c >= 0xFFF9 && c <= 0xFFFD))
            continue;
        return 0;
    }
    return 1;
}
