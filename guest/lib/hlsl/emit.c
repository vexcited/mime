#include "ir.h"

bool ir_is_marker(uint16_t op);
bool ir_has_side_effects(const IrIns *ins);
void ir_cfg_liveness(IrShader *s, uint32_t *block_count, uint32_t **start, uint32_t **end, uint64_t **live_out,
                     uint32_t *words);
uint32_t ir_ins_uses(const IrIns *ins, uint32_t *out, uint32_t max);

/* ------------------------------------------------------------------ words */

typedef struct Words {
    uint32_t *data;
    uint32_t count, capacity;
} Words;

static void put(Hlsl *h, Words *w, uint32_t v) { HL_PUSH(h, w->data, w->count, w->capacity, v); }

/* ------------------------------------------------------------------ live ranges */

typedef struct Range {
    uint32_t from, to;
} Range;

typedef struct RangeList {
    Range *items;
    uint32_t count, capacity;
} RangeList;

typedef struct VReg {
    RangeList ranges;
    uint32_t uses;
    int32_t slot;
    uint32_t hint;
    /* the instruction whose vector result it is part of, plus one. */
    uint32_t group;
} VReg;

typedef struct Alloc {
    IrShader *s;
    Hlsl *h;
    VReg *v;
    RangeList *slots;
    uint32_t slot_count;
    uint32_t temps;
} Alloc;

static void add_range(Alloc *a, uint32_t v, uint32_t from, uint32_t to)
{
    RangeList *r = &a->v[v].ranges;
    /* ranges come in descending order: extend the last one when they touch. */
    if (r->count) {
        Range *last = &r->items[r->count - 1];
        if (to + 1 >= last->from && from <= last->to + 1) {
            if (from < last->from)
                last->from = from;
            if (to > last->to)
                last->to = to;
            return;
        }
    }
    HL_PUSH(a->h, r->items, r->count, r->capacity, ((Range){from, to}));
}

static void build_ranges(Alloc *a)
{
    IrShader *s = a->s;
    uint32_t blocks, *start, *end, words;
    uint64_t *live_out;
    ir_cfg_liveness(s, &blocks, &start, &end, &live_out, &words);
    uint64_t *live = hl_alloc(a->h, words * 8 + 8);
    uint32_t uses[96];
    for (uint32_t b = blocks; b-- > 0;) {
        __builtin_memcpy(live, live_out + (size_t)b * words, words * 8);
        uint32_t bfrom = start[b] * 2, bto = end[b] * 2;
        for (uint32_t w = 0; w < words; w++)
            for (uint64_t bits = live[w]; bits; bits &= bits - 1) {
                uint32_t v = w * 64 + (uint32_t)__builtin_ctzll(bits);
                add_range(a, v, bfrom, bto);
            }
        for (uint32_t i = end[b]; i-- > start[b];) {
            const IrIns *ins = &s->ins[i];
            if (ins->op == IR_NOP)
                continue;
            for (uint32_t k = 0; k < ins->dst_count; k++) {
                uint32_t d = ins->dst[k];
                if ((live[d >> 6] >> (d & 63)) & 1) {
                    RangeList *r = &a->v[d].ranges;
                    if (r->count && r->items[r->count - 1].from <= 2 * i + 1)
                        r->items[r->count - 1].from = 2 * i + 1;
                    live[d >> 6] &= ~(1ull << (d & 63));
                } else if (a->v[d].uses) {
                    add_range(a, d, 2 * i + 1, 2 * i + 1);
                }
            }
            uint32_t n = ir_ins_uses(ins, uses, 96);
            for (uint32_t k = 0; k < n; k++) {
                uint32_t u = uses[k];
                if (!((live[u >> 6] >> (u & 63)) & 1)) {
                    add_range(a, u, bfrom, 2 * i);
                    live[u >> 6] |= 1ull << (u & 63);
                }
            }
        }
    }
    /* ascending order, merged. */
    for (uint32_t v = 0; v <= s->vregs; v++) {
        RangeList *r = &a->v[v].ranges;
        for (uint32_t i = 0; i < r->count / 2; i++) {
            Range t = r->items[i];
            r->items[i] = r->items[r->count - 1 - i];
            r->items[r->count - 1 - i] = t;
        }
        uint32_t out = 0;
        for (uint32_t i = 0; i < r->count; i++) {
            if (out && r->items[i].from <= r->items[out - 1].to + 1) {
                if (r->items[i].to > r->items[out - 1].to)
                    r->items[out - 1].to = r->items[i].to;
            } else {
                r->items[out++] = r->items[i];
            }
        }
        r->count = out;
    }
}

static bool overlaps(const RangeList *a, const RangeList *b)
{
    uint32_t i = 0, j = 0;
    while (i < a->count && j < b->count) {
        const Range *x = &a->items[i], *y = &b->items[j];
        if (x->to < y->from)
            i++;
        else if (y->to < x->from)
            j++;
        else
            return true;
    }
    return false;
}

static void occupy(Alloc *a, uint32_t slot, const RangeList *r)
{
    while (slot >= a->slot_count) {
        uint32_t n = a->slot_count ? a->slot_count * 2 : 64;
        a->slots = hl_realloc(a->h, a->slots, a->slot_count * sizeof(RangeList), n * sizeof(RangeList));
        a->slot_count = n;
    }
    RangeList *s = &a->slots[slot];
    /* merge the two sorted lists. */
    Range *merged = hl_alloc(a->h, (s->count + r->count + 1) * sizeof(Range));
    uint32_t i = 0, j = 0, n = 0;
    while (i < s->count || j < r->count) {
        if (j >= r->count || (i < s->count && s->items[i].from < r->items[j].from))
            merged[n++] = s->items[i++];
        else
            merged[n++] = r->items[j++];
    }
    s->items = merged;
    s->count = n;
    s->capacity = n + 1;
    if (slot / 4 + 1 > a->temps)
        a->temps = slot / 4 + 1;
}

static bool slot_free(Alloc *a, uint32_t slot, const RangeList *r)
{
    if (slot >= a->slot_count)
        return true;
    return !overlaps(&a->slots[slot], r);
}

static void allocate(Alloc *a)
{
    IrShader *s = a->s;
    Hlsl *h = a->h;
    uint32_t n = s->vregs + 1;
    uint32_t *order = hl_alloc(h, n * sizeof(uint32_t));
    uint32_t count = 0;
    for (uint32_t v = 1; v < n; v++)
        if (a->v[v].ranges.count)
            order[count++] = v;
    /* by first range start: a counting pass over positions would be faster; shell sort is enough. */
    for (uint32_t gap = count / 2; gap; gap /= 2)
        for (uint32_t i = gap; i < count; i++) {
            uint32_t x = order[i], key = a->v[x].ranges.items[0].from;
            uint32_t j = i;
            while (j >= gap && a->v[order[j - gap]].ranges.items[0].from > key) {
                order[j] = order[j - gap];
                j -= gap;
            }
            order[j] = x;
        }
    for (uint32_t k = 0; k < count; k++) {
        uint32_t v = order[k];
        VReg *vr = &a->v[v];
        if (vr->slot >= 0)
            continue;
        if (vr->group) {
            /* the used results of a vector instruction share a register. */
            const IrIns *ins = &s->ins[vr->group - 1];
            uint32_t members[4], m = 0;
            for (uint32_t i = 0; i < ins->dst_count; i++)
                if (a->v[ins->dst[i]].ranges.count)
                    members[m++] = ins->dst[i];
            for (uint32_t reg = 0;; reg++) {
                uint32_t taken = 0, picks[4];
                bool ok = true;
                for (uint32_t i = 0; i < m && ok; i++) {
                    ok = false;
                    for (uint32_t c = 0; c < 4; c++) {
                        if (taken & (1u << c))
                            continue;
                        if (slot_free(a, reg * 4 + c, &a->v[members[i]].ranges)) {
                            picks[i] = c;
                            taken |= 1u << c;
                            ok = true;
                            break;
                        }
                    }
                }
                if (ok) {
                    for (uint32_t i = 0; i < m; i++) {
                        a->v[members[i]].slot = (int32_t)(reg * 4 + picks[i]);
                        occupy(a, reg * 4 + picks[i], &a->v[members[i]].ranges);
                    }
                    break;
                }
            }
            continue;
        }
        int32_t chosen = -1;
        if (vr->hint && a->v[vr->hint].slot >= 0 && slot_free(a, (uint32_t)a->v[vr->hint].slot, &vr->ranges))
            chosen = a->v[vr->hint].slot;
        for (uint32_t slot = 0; chosen < 0; slot++)
            if (slot_free(a, slot, &vr->ranges))
                chosen = (int32_t)slot;
        vr->slot = chosen;
        occupy(a, (uint32_t)chosen, &vr->ranges);
    }
}

/* ------------------------------------------------------------------ operands */

#define TOKEN_OPERAND(components, selection, sel_bits, type, dims)                                                 \
    ((uint32_t)(components) | (uint32_t)(selection) << 2 | (uint32_t)(sel_bits) << 4 | (uint32_t)(type) << 12 |     \
     (uint32_t)(dims) << 20)

enum { COMP_0, COMP_1, COMP_4 };

typedef struct Emit {
    IrShader *s;
    Hlsl *h;
    Alloc *a;
    Words *w;
    /* scratch registers for gathered vector operands. */
    uint32_t scratch_base, scratch_used, scratch_max;
    bool sm51;
    /* the instruction being written, for results that go to outputs. */
    const IrIns *current;
    /* hull shaders: 0 in the control point phase, 1 in the fork phase. */
    uint32_t phase;
} Emit;

static uint32_t swizzle_bits(const uint8_t sw[4]) { return sw[0] | sw[1] << 2 | sw[2] << 4 | (uint32_t)sw[3] << 6; }

static uint32_t reg_of(Emit *e, uint32_t v) { return e->a->v[v].slot >= 0 ? (uint32_t)e->a->v[v].slot / 4 : 0; }
static uint32_t comp_of(Emit *e, uint32_t v) { return e->a->v[v].slot >= 0 ? (uint32_t)e->a->v[v].slot % 4 : 0; }

static uint32_t modifier_token(uint8_t mod)
{
    uint32_t m = (mod & MODIFIER_NEG ? 1u : 0u) | (mod & MODIFIER_ABS ? 2u : 0u);
    return 1u | m << 6;
}

/* an index: immediate, or immediate plus a register component. */
static void put_relative(Emit *e, uint32_t vreg)
{
    put(e->h, e->w, TOKEN_OPERAND(COMP_4, SELECT_SELECT1, comp_of(e, vreg), OPERAND_TEMP, 1));
    put(e->h, e->w, reg_of(e, vreg));
}

/* the token of an operand with indices, some relative. */
static void put_indexed(Emit *e, uint32_t head, const uint32_t *imm, const uint32_t *rel, uint32_t dims, uint8_t mod)
{
    for (uint32_t i = 0; i < dims; i++)
        head |= (uint32_t)(rel[i] ? (imm[i] ? INDEX_IMMEDIATE32_RELATIVE : INDEX_RELATIVE) : INDEX_IMMEDIATE32)
                << (22 + 3 * i);
    if (mod)
        head |= 1u << 31;
    put(e->h, e->w, head);
    if (mod)
        put(e->h, e->w, modifier_token(mod));
    for (uint32_t i = 0; i < dims; i++) {
        if (!rel[i] || imm[i])
            put(e->h, e->w, imm[i]);
        if (rel[i])
            put_relative(e, rel[i]);
    }
}

/* the cb slot of an ir buffer, and the input register numbering of 2d inputs. */
static uint32_t cb_slot(Emit *e, uint32_t buffer) { return e->s->cbuffers[buffer].slot; }

/* a source operand from scalars of the same register; sel is the swizzle, or SELECT1 for one. */
static void put_source(Emit *e, const IrSrc *src, const uint8_t sw[4], bool select1)
{
    uint32_t selection = select1 ? SELECT_SELECT1 : SELECT_SWIZZLE;
    uint32_t bits = select1 ? sw[0] : swizzle_bits(sw);
    uint32_t imm[3] = {}, rel[3] = {};
    switch (src->kind) {
    case SRC_REG:
        imm[0] = reg_of(e, src->v);
        put_indexed(e, TOKEN_OPERAND(COMP_4, selection, bits, OPERAND_TEMP, 1), imm, rel, 1, src->mod);
        return;
    case SRC_CB:
        imm[0] = cb_slot(e, src->v);
        imm[1] = src->index;
        rel[1] = src->rel;
        if (e->sm51) {
            imm[2] = imm[1];
            rel[2] = rel[1];
            rel[1] = 0;
            imm[1] = imm[0];
            imm[0] = src->v;
            put_indexed(e, TOKEN_OPERAND(COMP_4, selection, bits, OPERAND_CONSTANT_BUFFER, 3), imm, rel, 3, src->mod);
            return;
        }
        put_indexed(e, TOKEN_OPERAND(COMP_4, selection, bits, OPERAND_CONSTANT_BUFFER, 2), imm, rel, 2, src->mod);
        return;
    case SRC_IN:
    case SRC_OUT_CP: {
        uint8_t stage = e->s->profile.stage;
        if (stage == STAGE_GEOMETRY || stage == STAGE_HULL || stage == STAGE_DOMAIN) {
            uint32_t type = OPERAND_INPUT;
            if (src->kind == SRC_OUT_CP)
                type = OPERAND_OUTPUT_CONTROL_POINT;
            else if (stage == STAGE_DOMAIN || (stage == STAGE_HULL && e->phase))
                type = OPERAND_INPUT_CONTROL_POINT;
            imm[0] = src->index;
            rel[0] = src->rel;
            imm[1] = src->v;
            put_indexed(e, TOKEN_OPERAND(COMP_4, selection, bits, type, 2), imm, rel, 2, src->mod);
            return;
        }
        imm[0] = src->v;
        rel[0] = src->rel;
        put_indexed(e, TOKEN_OPERAND(COMP_4, selection, bits, OPERAND_INPUT, 1), imm, rel, 1, src->mod);
        return;
    }
    case SRC_SV: {
        bool scalar = src->v == OPERAND_INPUT_THREAD_ID_IN_GROUP_FLATTENED || src->v == OPERAND_INPUT_PRIMITIVEID ||
                      src->v == OPERAND_INPUT_COVERAGE_MASK || src->v == OPERAND_INPUT_GS_INSTANCE_ID ||
                      src->v == OPERAND_OUTPUT_CONTROL_POINT_ID || src->v == OPERAND_INNER_COVERAGE;
        uint32_t head = scalar ? TOKEN_OPERAND(COMP_1, 0, 0, src->v, 0) : TOKEN_OPERAND(COMP_4, selection, bits, src->v, 0);
        put_indexed(e, head, imm, rel, 0, src->mod);
        return;
    }
    case SRC_PATCH:
        imm[0] = src->v;
        put_indexed(e, TOKEN_OPERAND(COMP_4, selection, bits, OPERAND_INPUT_PATCH_CONSTANT, 1), imm, rel, 1, src->mod);
        return;
    case SRC_ICB:
        imm[0] = src->index;
        rel[0] = src->rel;
        put_indexed(e, TOKEN_OPERAND(COMP_4, selection, bits, OPERAND_IMMEDIATE_CONSTANT_BUFFER, 1), imm, rel, 1,
                    src->mod);
        return;
    case SRC_X:
        imm[0] = src->v;
        imm[1] = src->index;
        rel[1] = src->rel;
        put_indexed(e, TOKEN_OPERAND(COMP_4, selection, bits, OPERAND_INDEXABLE_TEMP, 2), imm, rel, 2, src->mod);
        return;
    default:
        put_indexed(e, TOKEN_OPERAND(COMP_4, selection, bits, OPERAND_TEMP, 1), imm, rel, 1, 0);
        return;
    }
}

static uint8_t src_comp(Emit *e, const IrSrc *s)
{
    if (s->kind == SRC_REG)
        return (uint8_t)comp_of(e, s->v);
    return s->comp;
}

/* whether the scalars name components of one register, so one operand reads them. */
static bool same_register(Emit *e, const IrSrc *a, const IrSrc *b)
{
    if (a->kind != b->kind || a->mod != b->mod)
        return false;
    switch (a->kind) {
    case SRC_REG:
        return reg_of(e, a->v) == reg_of(e, b->v);
    case SRC_CB:
    case SRC_X:
    case SRC_OUT_CP:
    case SRC_PATCH:
        return a->v == b->v && a->index == b->index && a->rel == b->rel;
    case SRC_IN:
        return a->v == b->v && a->index == b->index && a->rel == b->rel;
    case SRC_SV:
        return a->v == b->v;
    case SRC_ICB:
        return a->index == b->index && a->rel == b->rel;
    default:
        return false;
    }
}

static void put_mov_scalar(Emit *e, uint32_t reg, uint32_t comp, const IrSrc *src);

static void put_immediate(Emit *e, const IrSrc *srcs, uint32_t n)
{
    if (n == 1) {
        put(e->h, e->w, TOKEN_OPERAND(COMP_1, 0, 0, OPERAND_IMMEDIATE32, 0));
        put(e->h, e->w, srcs[0].v);
        return;
    }
    put(e->h, e->w, TOKEN_OPERAND(COMP_4, SELECT_SWIZZLE, 0xE4, OPERAND_IMMEDIATE32, 0));
    for (uint32_t i = 0; i < 4; i++)
        put(e->h, e->w, srcs[i < n ? i : n - 1].v);
}

/* moves the scalars of a group into a scratch register when one operand cannot read them. */
typedef struct Group {
    const IrSrc *srcs;
    uint32_t n;
    /* the scratch register gathered into, or ~0. */
    uint32_t scratch;
} Group;

static bool group_direct(Emit *e, const IrSrc *srcs, uint32_t n)
{
    bool imm = true;
    for (uint32_t i = 0; i < n; i++)
        imm &= srcs[i].kind == SRC_IMM && !srcs[i].mod;
    if (imm)
        return true;
    for (uint32_t i = 1; i < n; i++)
        if (!same_register(e, &srcs[0], &srcs[i]))
            return false;
    return srcs[0].kind != SRC_IMM;
}

static void prepare_group(Emit *e, Group *g)
{
    g->scratch = ~0u;
    if (group_direct(e, g->srcs, g->n))
        return;
    uint32_t reg = e->scratch_base + e->scratch_used++;
    if (e->scratch_used > e->scratch_max)
        e->scratch_max = e->scratch_used;
    for (uint32_t i = 0; i < g->n; i++)
        put_mov_scalar(e, reg, i, &g->srcs[i]);
    g->scratch = reg;
}

static void put_group(Emit *e, const Group *g, bool select1)
{
    if (g->scratch != ~0u) {
        uint8_t sw[4];
        for (uint32_t i = 0; i < 4; i++)
            sw[i] = (uint8_t)(i < g->n ? i : g->n - 1);
        uint32_t imm[1] = {g->scratch}, rel[1] = {};
        put_indexed(e, TOKEN_OPERAND(COMP_4, select1 ? SELECT_SELECT1 : SELECT_SWIZZLE, select1 ? 0 : swizzle_bits(sw),
                                     OPERAND_TEMP, 1), imm, rel, 1, 0);
        return;
    }
    bool imm = true;
    for (uint32_t i = 0; i < g->n; i++)
        imm &= g->srcs[i].kind == SRC_IMM && !g->srcs[i].mod;
    if (imm) {
        put_immediate(e, g->srcs, select1 ? 1 : g->n);
        return;
    }
    uint8_t sw[4];
    for (uint32_t i = 0; i < 4; i++)
        sw[i] = src_comp(e, &g->srcs[i < g->n ? i : g->n - 1]);
    put_source(e, &g->srcs[0], sw, select1);
}

/* one scalar source, replicated across the swizzle or selected. */
static void put_scalar_source(Emit *e, const IrSrc *src, bool select1)
{
    if (src->kind == SRC_IMM && !src->mod) {
        put(e->h, e->w, TOKEN_OPERAND(COMP_1, 0, 0, OPERAND_IMMEDIATE32, 0));
        put(e->h, e->w, src->v);
        return;
    }
    if (src->kind == SRC_IMM) {
        put(e->h, e->w, TOKEN_OPERAND(COMP_1, 0, 0, OPERAND_IMMEDIATE32, 0) | 1u << 31);
        put(e->h, e->w, modifier_token(src->mod));
        put(e->h, e->w, src->v);
        return;
    }
    uint8_t c = src_comp(e, src);
    uint8_t sw[4] = {c, c, c, c};
    put_source(e, src, sw, select1);
}

static void put_dst_temp(Emit *e, uint32_t reg, uint32_t mask)
{
    put(e->h, e->w, TOKEN_OPERAND(COMP_4, SELECT_MASK, mask, OPERAND_TEMP, 1 ) | INDEX_IMMEDIATE32 << 22);
    put(e->h, e->w, reg);
}

static void put_null(Emit *e) { put(e->h, e->w, TOKEN_OPERAND(COMP_0, 0, 0, OPERAND_NULL, 0)); }

static void put_output(Emit *e, const IrDst *d)
{
    uint32_t imm[2] = {d->v}, rel[2] = {};
    if (d->kind == DST_SPECIAL)
        put(e->h, e->w, TOKEN_OPERAND(COMP_1, 0, 0, d->v, 0));
    else
        put_indexed(e, TOKEN_OPERAND(COMP_4, SELECT_MASK, 1u << d->comp, OPERAND_OUTPUT, 1), imm, rel, 1, 0);
}

static void put_dst_vreg(Emit *e, uint32_t v)
{
    if (e->current && e->current->to_output && v == e->current->dst[0]) {
        put_output(e, &e->current->output);
        return;
    }
    if (!v || e->a->v[v].slot < 0) {
        put_null(e);
        return;
    }
    put_dst_temp(e, reg_of(e, v), 1u << comp_of(e, v));
}

/* the opcode token; the length goes in when the instruction ends. */
static uint32_t begin(Emit *e, uint32_t token)
{
    put(e->h, e->w, token);
    return e->w->count - 1;
}

static void finish(Emit *e, uint32_t at)
{
    uint32_t length = e->w->count - at;
    e->w->data[at] |= length << 24;
}

static void put_mov_scalar(Emit *e, uint32_t reg, uint32_t comp, const IrSrc *src)
{
    uint32_t at = begin(e, OP_MOV);
    put_dst_temp(e, reg, 1u << comp);
    put_scalar_source(e, src, false);
    finish(e, at);
}

/* ------------------------------------------------------------------ resources */

static void put_resource(Emit *e, const IrIns *ins, uint8_t kind, uint32_t id, uint32_t rel_reg, const uint8_t sw[4],
                         bool as_dst, uint32_t mask)
{
    uint32_t type, slot = 0;
    IrShader *s = e->s;
    switch (kind) {
    case SRC_TGSM:
        type = OPERAND_THREAD_GROUP_SHARED_MEMORY;
        slot = id;
        break;
    case SRC_SV:
        type = id;
        break;
    default: {
        const IrResource *r = &s->resources[id];
        type = r->kind == SRC_UAV ? OPERAND_UNORDERED_ACCESS_VIEW : r->kind == SRC_SAMPLER ? OPERAND_SAMPLER
                                                                                             : OPERAND_RESOURCE;
        slot = r->slot + (r->dynamic ? 0 : 0);
        break;
    }
    }
    uint32_t head = as_dst ? TOKEN_OPERAND(COMP_4, SELECT_MASK, mask, type, 1)
                           : TOKEN_OPERAND(COMP_4, SELECT_SWIZZLE, swizzle_bits(sw), type, 1);
    if (kind == SRC_SV)
        head = TOKEN_OPERAND(COMP_4, SELECT_SWIZZLE, swizzle_bits(sw), type, 0);
    uint32_t imm[3] = {slot}, rel[3] = {rel_reg};
    if (kind == SRC_SV) {
        put_indexed(e, head, imm, rel, 0, 0);
        return;
    }
    if (e->sm51 && kind != SRC_TGSM) {
        const IrResource *r = &s->resources[id];
        imm[0] = r->id;
        imm[1] = r->slot;
        rel[0] = 0;
        rel[1] = rel_reg;
        put_indexed(e, (head & ~(3u << 20)) | 2u << 20, imm, rel, 2, 0);
        return;
    }
    put_indexed(e, head, imm, rel, 1, 0);
}

static const uint8_t identity[4] = {0, 1, 2, 3};

/* ------------------------------------------------------------------ instructions */

static bool vector_result(uint16_t op)
{
    switch (op) {
    case OP_SAMPLE: case OP_SAMPLE_B: case OP_SAMPLE_D: case OP_SAMPLE_L: case OP_SAMPLE_C: case OP_SAMPLE_C_LZ:
    case OP_LD: case OP_LD_MS: case OP_LD_UAV_TYPED: case OP_LD_STRUCTURED: case OP_LD_RAW: case OP_GATHER4:
    case OP_GATHER4_C: case OP_GATHER4_PO: case OP_GATHER4_PO_C: case OP_RESINFO: case OP_LOD: case OP_SAMPLE_POS:
    case OP_BUFINFO: case OP_SAMPLE_INFO:
        return true;
    default:
        return false;
    }
}

/* the destination of a vector result: the mask of the used parts and the swizzle that puts them there. */
static uint32_t vector_dst(Emit *e, const IrIns *ins, uint8_t sw[4], uint32_t *reg)
{
    uint32_t mask = 0;
    *reg = 0;
    for (uint32_t i = 0; i < 4; i++)
        sw[i] = (uint8_t)(ins->res_first + i < 4 ? ins->res_first + i : 3);
    for (uint32_t i = 0; i < ins->dst_count; i++) {
        uint32_t v = ins->dst[i];
        if (e->a->v[v].slot < 0)
            continue;
        uint32_t c = comp_of(e, v);
        *reg = reg_of(e, v);
        mask |= 1u << c;
        sw[c] = (uint8_t)(ins->res_first + i);
    }
    return mask;
}

static uint32_t sample_offsets(const IrIns *ins)
{
    return 1u | (uint32_t)(ins->offset[0] & 0xF) << 9 | (uint32_t)(ins->offset[1] & 0xF) << 13 |
           (uint32_t)(ins->offset[2] & 0xF) << 17;
}

static void emit_ins(Emit *e, const IrIns *ins)
{
    Hlsl *h = e->h;
    e->scratch_used = 0;
    e->current = ins;
    uint16_t op = ins->op;
    if (op == IR_NOP)
        return;
    /* the operand groups, gathered first. */
    Group groups[8];
    uint32_t group_count = 0;
    if (ins->group_count) {
        uint32_t at = 0;
        for (uint32_t i = 0; i < ins->group_count; i++) {
            groups[i] = (Group){ins->src + at, ins->groups[i], ~0u};
            at += ins->groups[i];
        }
        group_count = ins->group_count;
    } else {
        for (uint32_t i = 0; i < ins->src_count && i < 8; i++)
            groups[i] = (Group){ins->src + i, 1, ~0u};
        group_count = ins->src_count < 8 ? ins->src_count : 8;
    }
    for (uint32_t i = 0; i < group_count; i++)
        if (groups[i].n > 1)
            prepare_group(e, &groups[i]);

    uint32_t token = op;
    if (ins->flags & IRF_SAT)
        token |= 1u << 13;
    if (ins->flags & IRF_NZ)
        token |= 1u << 18;

    if (op == IR_STORE) {
        const IrDst *d = &ins->store;
        uint32_t at = begin(e, OP_MOV);
        uint32_t imm[2] = {}, rel[2] = {};
        switch (d->kind) {
        case DST_OUT:
            imm[0] = d->v;
            if (e->s->profile.stage == STAGE_GEOMETRY || true)
                put_indexed(e, TOKEN_OPERAND(COMP_4, SELECT_MASK, 1u << d->comp, OPERAND_OUTPUT, 1), imm, rel, 1, 0);
            break;
        case DST_X:
            imm[0] = d->v;
            imm[1] = d->index;
            rel[1] = d->rel;
            put_indexed(e, TOKEN_OPERAND(COMP_4, SELECT_MASK, 1u << d->comp, OPERAND_INDEXABLE_TEMP, 2), imm, rel, 2, 0);
            break;
        case DST_SPECIAL:
            put(h, e->w, TOKEN_OPERAND(COMP_1, 0, 0, d->v, 0));
            break;
        default:
            put_null(e);
        }
        put_scalar_source(e, &ins->src[0], false);
        finish(e, at);
        return;
    }
    if (op == OP_MOV && ins->dst_count == 1 && ins->src[0].kind == SRC_REG && !ins->src[0].mod && !(ins->flags & IRF_SAT) &&
        e->a->v[ins->dst[0]].slot >= 0 && e->a->v[ins->dst[0]].slot == e->a->v[ins->src[0].v].slot)
        return;
    if (op == OP_MOV && ins->dst_count == 1 && e->a->v[ins->dst[0]].slot < 0 && !ins->to_output)
        return;

    switch (op) {
    case OP_IF: case OP_BREAKC: case OP_CONTINUEC: case OP_RETC: case OP_DISCARD: case OP_SWITCH: {
        uint32_t at = begin(e, token);
        put_scalar_source(e, &ins->src[0], true);
        finish(e, at);
        return;
    }
    case OP_CASE: {
        uint32_t at = begin(e, OP_CASE);
        put(h, e->w, TOKEN_OPERAND(COMP_1, 0, 0, OPERAND_IMMEDIATE32, 0));
        put(h, e->w, ins->extra);
        finish(e, at);
        return;
    }
    case OP_ELSE: case OP_ENDIF: case OP_LOOP: case OP_ENDLOOP: case OP_BREAK: case OP_CONTINUE: case OP_DEFAULT:
    case OP_ENDSWITCH: case OP_RET: case OP_EMIT: case OP_CUT: case OP_EMITTHENCUT: {
        uint32_t at = begin(e, op);
        finish(e, at);
        return;
    }
    case OP_SYNC: {
        uint32_t at = begin(e, op | ins->extra << 11);
        finish(e, at);
        return;
    }
    case OP_SINCOS: case OP_UDIV: {
        uint32_t at = begin(e, token);
        put_dst_vreg(e, ins->dst[0]);
        put_dst_vreg(e, ins->dst[1]);
        for (uint32_t i = 0; i < ins->src_count; i++)
            put_scalar_source(e, &ins->src[i], false);
        finish(e, at);
        return;
    }
    case OP_IMUL: case OP_UMUL: {
        uint32_t at = begin(e, token);
        put_null(e);
        put_dst_vreg(e, ins->dst[0]);
        put_scalar_source(e, &ins->src[0], false);
        put_scalar_source(e, &ins->src[1], false);
        finish(e, at);
        return;
    }
    case OP_DP2: case OP_DP3: case OP_DP4: {
        uint32_t at = begin(e, token);
        put_dst_vreg(e, ins->dst[0]);
        put_group(e, &groups[0], false);
        put_group(e, &groups[1], false);
        finish(e, at);
        return;
    }
    case OP_EVAL_SNAPPED: case OP_EVAL_SAMPLE_INDEX: case OP_EVAL_CENTROID: {
        uint32_t at = begin(e, token);
        put_dst_vreg(e, ins->dst[0]);
        put_scalar_source(e, &ins->src[0], false);
        if (op == OP_EVAL_SAMPLE_INDEX)
            put_scalar_source(e, &ins->src[1], true);
        if (op == OP_EVAL_SNAPPED)
            put_group(e, &groups[1], false);
        finish(e, at);
        return;
    }
    default:
        break;
    }

    if (vector_result(op)) {
        uint8_t sw[4];
        uint32_t reg;
        uint32_t mask = vector_dst(e, ins, sw, &reg);
        if (op == OP_RESINFO && ins->uint_result)
            token |= 2u << 11;
        if (op == OP_SAMPLE_INFO && ins->uint_result)
            token |= 1u << 11;
        bool offsets = ins->offset[0] || ins->offset[1] || ins->offset[2];
        if (offsets)
            token |= 1u << 31;
        uint32_t at = begin(e, token);
        if (offsets)
            put(h, e->w, sample_offsets(ins));
        if (mask)
            put_dst_temp(e, reg, mask);
        else
            put_null(e);
        uint8_t ssw[4] = {(uint8_t)ins->extra, (uint8_t)ins->extra, (uint8_t)ins->extra, (uint8_t)ins->extra};
        switch (op) {
        case OP_SAMPLE: case OP_SAMPLE_B: case OP_SAMPLE_L: case OP_SAMPLE_C: case OP_SAMPLE_C_LZ: case OP_SAMPLE_D:
            put_group(e, &groups[0], false);
            put_resource(e, ins, ins->res_kind, ins->res, ins->res_rel, sw, false, 0);
            put_resource(e, ins, SRC_SAMPLER, ins->sampler, ins->sampler_rel, identity, false, 0);
            if (op == OP_SAMPLE_D) {
                put_group(e, &groups[1], false);
                put_group(e, &groups[2], false);
            } else if (op != OP_SAMPLE) {
                put_group(e, &groups[1], true);
            }
            break;
        case OP_GATHER4: case OP_GATHER4_C:
            put_group(e, &groups[0], false);
            put_resource(e, ins, ins->res_kind, ins->res, ins->res_rel, sw, false, 0);
            put_resource(e, ins, SRC_SAMPLER, ins->sampler, ins->sampler_rel, ssw, false, 0);
            if (op == OP_GATHER4_C)
                put_group(e, &groups[1], true);
            break;
        case OP_GATHER4_PO: case OP_GATHER4_PO_C:
            put_group(e, &groups[0], false);
            put_group(e, &groups[1], false);
            put_resource(e, ins, ins->res_kind, ins->res, ins->res_rel, sw, false, 0);
            put_resource(e, ins, SRC_SAMPLER, ins->sampler, ins->sampler_rel, ssw, false, 0);
            if (op == OP_GATHER4_PO_C)
                put_group(e, &groups[2], true);
            break;
        case OP_LD: case OP_LD_UAV_TYPED:
            put_group(e, &groups[0], false);
            put_resource(e, ins, ins->res_kind, ins->res, ins->res_rel, sw, false, 0);
            break;
        case OP_LD_MS:
            put_group(e, &groups[0], false);
            put_resource(e, ins, ins->res_kind, ins->res, ins->res_rel, sw, false, 0);
            put_group(e, &groups[1], true);
            break;
        case OP_LD_STRUCTURED:
            put_scalar_source(e, &ins->src[0], true);
            put_scalar_source(e, &ins->src[1], true);
            put_resource(e, ins, ins->res_kind, ins->res, ins->res_rel, sw, false, 0);
            break;
        case OP_LD_RAW:
            put_scalar_source(e, &ins->src[0], true);
            put_resource(e, ins, ins->res_kind, ins->res, ins->res_rel, sw, false, 0);
            break;
        case OP_RESINFO:
            put_scalar_source(e, &ins->src[0], true);
            put_resource(e, ins, ins->res_kind, ins->res, ins->res_rel, sw, false, 0);
            break;
        case OP_BUFINFO: case OP_SAMPLE_INFO:
            put_resource(e, ins, ins->res_kind, ins->res, ins->res_rel, sw, false, 0);
            break;
        case OP_SAMPLE_POS:
            put_resource(e, ins, ins->res_kind, ins->res, ins->res_rel, sw, false, 0);
            put_scalar_source(e, &ins->src[0], true);
            break;
        case OP_LOD:
            put_group(e, &groups[0], false);
            put_resource(e, ins, ins->res_kind, ins->res, ins->res_rel, sw, false, 0);
            put_resource(e, ins, SRC_SAMPLER, ins->sampler, ins->sampler_rel, identity, false, 0);
            break;
        }
        finish(e, at);
        return;
    }

    switch (op) {
    case OP_STORE_STRUCTURED: case OP_STORE_RAW: case OP_STORE_UAV_TYPED: {
        /* the view, as the destination with the mask of the stored components. */
        uint32_t value_group = op == OP_STORE_STRUCTURED ? 2 : 1;
        uint32_t n = groups[value_group].n;
        uint32_t mask = op == OP_STORE_UAV_TYPED ? 0xF : (1u << n) - 1;
        uint32_t at = begin(e, token);
        put_resource(e, ins, ins->res_kind, ins->res, ins->res_rel, identity, true, mask);
        if (op == OP_STORE_STRUCTURED) {
            put_scalar_source(e, &ins->src[0], true);
            put_scalar_source(e, &ins->src[1], true);
        } else if (op == OP_STORE_RAW) {
            put_scalar_source(e, &ins->src[0], true);
        } else {
            put_group(e, &groups[0], false);
        }
        put_group(e, &groups[value_group], false);
        finish(e, at);
        return;
    }
    case OP_ATOMIC_AND: case OP_ATOMIC_OR: case OP_ATOMIC_XOR: case OP_ATOMIC_IADD: case OP_ATOMIC_IMAX:
    case OP_ATOMIC_IMIN: case OP_ATOMIC_UMAX: case OP_ATOMIC_UMIN: case OP_ATOMIC_CMP_STORE: {
        uint32_t at = begin(e, token);
        put_resource(e, ins, ins->res_kind, ins->res, ins->res_rel, identity, true, 1);
        put_group(e, &groups[0], false);
        for (uint32_t i = 1; i < group_count; i++)
            put_group(e, &groups[i], true);
        finish(e, at);
        return;
    }
    case OP_IMM_ATOMIC_IADD: case OP_IMM_ATOMIC_AND: case OP_IMM_ATOMIC_OR: case OP_IMM_ATOMIC_XOR:
    case OP_IMM_ATOMIC_EXCH: case OP_IMM_ATOMIC_CMP_EXCH: case OP_IMM_ATOMIC_IMAX: case OP_IMM_ATOMIC_IMIN:
    case OP_IMM_ATOMIC_UMAX: case OP_IMM_ATOMIC_UMIN: {
        uint32_t at = begin(e, token);
        put_dst_vreg(e, ins->dst_count ? ins->dst[0] : 0);
        put_resource(e, ins, ins->res_kind, ins->res, ins->res_rel, identity, true, 1);
        put_group(e, &groups[0], false);
        for (uint32_t i = 1; i < group_count; i++)
            put_group(e, &groups[i], true);
        finish(e, at);
        return;
    }
    case OP_IMM_ATOMIC_ALLOC: case OP_IMM_ATOMIC_CONSUME: {
        uint32_t at = begin(e, token);
        put_dst_vreg(e, ins->dst[0]);
        put_resource(e, ins, ins->res_kind, ins->res, ins->res_rel, identity, true, 1);
        finish(e, at);
        return;
    }
    default:
        break;
    }

    /* componentwise instructions of one scalar result. */
    uint32_t at = begin(e, token);
    put_dst_vreg(e, ins->dst_count ? ins->dst[0] : 0);
    for (uint32_t i = 0; i < ins->src_count; i++)
        put_scalar_source(e, &ins->src[i], false);
    finish(e, at);
}

/* ------------------------------------------------------------------ bindings */

/* slots for what the code still names: explicit registers first, then the lowest free, in declaration order. */
static uint32_t explicit_register(IrShader *s, const HlRegister *regs, uint32_t count, char kind, uint32_t *space)
{
    for (uint32_t i = 0; i < count; i++) {
        const HlRegister *r = &regs[i];
        if (r->profile && hl_stricmp(r->profile, s->profile.name))
            continue;
        if (r->kind == kind) {
            if (space)
                *space = r->space;
            return r->index;
        }
    }
    return ~0u;
}

static char register_kind(const IrResource *r)
{
    switch (r->kind) {
    case SRC_SAMPLER: return 's';
    case SRC_UAV: return 'u';
    default: return 't';
    }
}

static bool range_used(const uint8_t *used, uint32_t slot, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        if (slot + i >= 256 || used[slot + i])
            return true;
    return false;
}

static void bind(IrShader *s)
{
    Hlsl *h = s->h;
    /* which resources and buffers the remaining code reads. */
    for (uint32_t i = 0; i < s->count; i++) {
        const IrIns *ins = &s->ins[i];
        if (ins->op == IR_NOP)
            continue;
        if (ins->res_kind == SRC_RESOURCE || ins->res_kind == SRC_UAV || ins->res_kind == SRC_SAMPLER)
            s->resources[ins->res].used = true;
        if (ins->sampler || (ins->op >= OP_SAMPLE && ins->op <= OP_SAMPLE_B) || ins->op == OP_LOD ||
            (ins->op >= OP_GATHER4 && ins->op <= OP_GATHER4_PO_C))
            if (ins->sampler < s->resource_count)
                s->resources[ins->sampler].used = true;
        for (uint32_t k = 0; k < ins->src_count; k++) {
            const IrSrc *src = &ins->src[k];
            if (src->kind == SRC_CB) {
                IrCbuffer *cb = &s->cbuffers[src->v];
                cb->used = true;
                if (src->rel)
                    cb->dynamic = true;
                if (src->index + 1 > cb->extent)
                    cb->extent = src->index + 1;
            }
        }
    }
    /* whole resource arrays bind together: an element marks the variable used. */
    uint8_t used_t[256] = {}, used_s[256] = {}, used_u[256] = {}, used_b[256] = {};
    for (int pass = 0; pass < 2; pass++)
        for (uint32_t i = 0; i < s->resource_count; i++) {
            IrResource *r = &s->resources[i];
            if (!r->used)
                continue;
            char kind = register_kind(r);
            uint8_t *used = kind == 's' ? used_s : kind == 'u' ? used_u : used_t;
            uint32_t space = 0;
            uint32_t base = explicit_register(s, r->var->registers, r->var->register_count, kind, &space);
            if ((pass == 0) != (base != ~0u))
                continue;
            uint32_t elements = 1;
            for (const HlType *t = r->var->type; t->kind == TY_ARRAY; t = t->elem)
                elements *= t->count;
            /* elements of one variable keep their places relative to its first. */
            uint32_t first = ~0u;
            for (uint32_t j = 0; j < i; j++)
                if (s->resources[j].var == r->var && s->resources[j].used)
                    first = s->resources[j].slot - s->resources[j].element;
            if (first == ~0u) {
                if (base == ~0u) {
                    base = 0;
                    while (range_used(used, base, elements))
                        base++;
                }
                first = base;
                for (uint32_t k = 0; k < elements && first + k < 256; k++)
                    used[first + k] = 1;
            }
            r->slot = first + r->element;
            r->space = space;
        }
    /* constant buffers: explicit, then $Globals, then the rest in order. */
    HlProgram *prog = s->prog;
    for (int pass = 0; pass < 3; pass++)
        for (uint32_t i = 0; i < s->cbuffer_count; i++) {
            IrCbuffer *cb = &s->cbuffers[i];
            if (!cb->used)
                continue;
            HlBuffer *b = cb->buffer;
            uint32_t space = 0;
            uint32_t slot = explicit_register(s, b->registers, b->register_count, 'b', &space);
            bool globals = b == prog->globals_buffer;
            if (pass == 0 && slot == ~0u)
                continue;
            if (pass == 1 && (slot != ~0u || !globals))
                continue;
            if (pass == 2 && (slot != ~0u || globals))
                continue;
            if (slot == ~0u) {
                slot = 0;
                while (slot < 255 && used_b[slot])
                    slot++;
            }
            used_b[slot & 255] = 1;
            cb->slot = slot;
            cb->space = space;
        }
    (void)h;
}

/* ------------------------------------------------------------------ declarations */

static uint32_t resource_dimension(uint8_t object)
{
    switch (object) {
    case OBJ_BUFFER: case OBJ_RWBUFFER: return RES_BUFFER;
    case OBJ_TEXTURE1D: case OBJ_RWTEXTURE1D: case OBJ_SAMPLER1D: return RES_TEXTURE1D;
    case OBJ_TEXTURE1DARRAY: case OBJ_RWTEXTURE1DARRAY: return RES_TEXTURE1DARRAY;
    case OBJ_TEXTURE2D: case OBJ_RWTEXTURE2D: case OBJ_SAMPLER2D: case OBJ_TEXTURE: return RES_TEXTURE2D;
    case OBJ_TEXTURE2DARRAY: case OBJ_RWTEXTURE2DARRAY: return RES_TEXTURE2DARRAY;
    case OBJ_TEXTURE2DMS: return RES_TEXTURE2DMS;
    case OBJ_TEXTURE2DMSARRAY: return RES_TEXTURE2DMSARRAY;
    case OBJ_TEXTURE3D: case OBJ_RWTEXTURE3D: case OBJ_SAMPLER3D: return RES_TEXTURE3D;
    case OBJ_TEXTURECUBE: case OBJ_SAMPLERCUBE: return RES_TEXTURECUBE;
    case OBJ_TEXTURECUBEARRAY: return RES_TEXTURECUBEARRAY;
    case OBJ_BYTE_ADDRESS_BUFFER: case OBJ_RWBYTE_ADDRESS_BUFFER: return RES_RAW_BUFFER;
    default: return RES_STRUCTURED_BUFFER;
    }
}

uint32_t ir_return_type(const HlType *elem)
{
    if (!elem)
        return RET_FLOAT;
    if (elem->modifiers & TM_UNORM)
        return RET_UNORM;
    if (elem->modifiers & TM_SNORM)
        return RET_SNORM;
    switch (hl_base_storage(elem->base)) {
    case BT_INT: return RET_SINT;
    case BT_UINT: case BT_BOOL: return RET_UINT;
    case BT_DOUBLE: return RET_DOUBLE;
    default: return RET_FLOAT;
    }
}

uint32_t lw_tight_size(const HlType *t, bool row_major_default);

static void put_decl_operand(Emit *e, uint32_t type, uint32_t slot, uint32_t components, uint32_t mask_or_swizzle,
                             bool mask, const IrResource *r)
{
    uint32_t head = components == 0 ? TOKEN_OPERAND(COMP_0, 0, 0, type, 1)
                    : mask      ? TOKEN_OPERAND(COMP_4, SELECT_MASK, mask_or_swizzle, type, 1)
                                : TOKEN_OPERAND(COMP_4, SELECT_SWIZZLE, mask_or_swizzle, type, 1);
    if (e->sm51 && r) {
        put(e->h, e->w, (head & ~(3u << 20)) | 3u << 20);
        put(e->h, e->w, r->id);
        put(e->h, e->w, r->slot);
        put(e->h, e->w, r->dynamic && r->count == 0 ? 0xFFFFFFFF : r->slot + (r->dynamic ? r->count - 1 : 0));
        return;
    }
    put(e->h, e->w, head);
    put(e->h, e->w, slot);
}

static void declare_resource(Emit *e, const IrResource *r)
{
    Hlsl *h = e->h;
    IrShader *s = e->s;
    const HlType *t = r->var->type;
    while (t->kind == TY_ARRAY)
        t = t->elem;
    uint32_t dim = resource_dimension(r->object);
    uint32_t at;
    if (r->kind == SRC_SAMPLER) {
        at = begin(e, OP_DCL_SAMPLER | (r->comparison ? 1u : 0u) << 11);
        put_decl_operand(e, OPERAND_SAMPLER, r->slot, 0, 0, false, r);
    } else if (r->kind == SRC_RESOURCE) {
        if (dim == RES_RAW_BUFFER) {
            at = begin(e, OP_DCL_RESOURCE_RAW);
            put_decl_operand(e, OPERAND_RESOURCE, r->slot, 0, 0, false, r);
        } else if (dim == RES_STRUCTURED_BUFFER) {
            at = begin(e, OP_DCL_RESOURCE_STRUCTURED);
            put_decl_operand(e, OPERAND_RESOURCE, r->slot, 0, 0, false, r);
            put(h, e->w, lw_tight_size(t->elem, s->row_major_default));
        } else {
            at = begin(e, OP_DCL_RESOURCE | dim << 11 | (t->samples & 0x7F) << 16);
            put_decl_operand(e, OPERAND_RESOURCE, r->slot, 0, 0, false, r);
            uint32_t rt = ir_return_type(t->elem);
            put(h, e->w, rt | rt << 4 | rt << 8 | rt << 12);
        }
    } else {
        uint32_t flags = (t->modifiers & TM_GLOBALLY_COHERENT ? 1u << 16 : 0) | (t->modifiers & TM_ROV ? 1u << 17 : 0);
        if (dim == RES_RAW_BUFFER) {
            at = begin(e, OP_DCL_UAV_RAW | flags);
            put_decl_operand(e, OPERAND_UNORDERED_ACCESS_VIEW, r->slot, 0, 0, false, r);
        } else if (dim == RES_STRUCTURED_BUFFER) {
            at = begin(e, OP_DCL_UAV_STRUCTURED | flags | (r->counter ? 1u << 23 : 0));
            put_decl_operand(e, OPERAND_UNORDERED_ACCESS_VIEW, r->slot, 0, 0, false, r);
            put(h, e->w, lw_tight_size(t->elem, s->row_major_default));
        } else {
            at = begin(e, OP_DCL_UAV_TYPED | dim << 11 | flags);
            put_decl_operand(e, OPERAND_UNORDERED_ACCESS_VIEW, r->slot, 0, 0, false, r);
            uint32_t rt = ir_return_type(t->elem);
            put(h, e->w, rt | rt << 4 | rt << 8 | rt << 12);
        }
    }
    if (e->sm51)
        put(h, e->w, r->space);
    finish(e, at);
}

static uint32_t interpolation(uint32_t interp, uint32_t component_type)
{
    if (component_type != 3 || (interp & IM_NOINTERPOLATION))
        return INTERP_CONSTANT;
    bool np = interp & IM_NOPERSPECTIVE, c = interp & IM_CENTROID, s = interp & IM_SAMPLE;
    if (s)
        return np ? INTERP_LINEAR_NOPERSPECTIVE_SAMPLE : INTERP_LINEAR_SAMPLE;
    if (c)
        return np ? INTERP_LINEAR_NOPERSPECTIVE_CENTROID : INTERP_LINEAR_CENTROID;
    return np ? INTERP_LINEAR_NOPERSPECTIVE : INTERP_LINEAR;
}

typedef struct Masks {
    uint8_t in[64], sv[64], out_cp[64], patch[64];
} Masks;

static void scan_masks(const IrIns *ins, Masks *m)
{
    for (uint32_t k = 0; k < ins->src_count; k++) {
        const IrSrc *src = &ins->src[k];
        if (src->v >= 64)
            continue;
        if (src->kind == SRC_IN)
            m->in[src->v] |= 1u << src->comp;
        else if (src->kind == SRC_SV)
            m->sv[src->v] |= 1u << src->comp;
        else if (src->kind == SRC_OUT_CP)
            m->out_cp[src->v] |= 1u << src->comp;
        else if (src->kind == SRC_PATCH)
            m->patch[src->v] |= 1u << src->comp;
    }
}

/* control points and patch constants of hull and domain shaders, read through their own operands. */
static void declare_patch_inputs(Emit *e, const IrSignature *sig, const uint8_t *mask, uint32_t type, uint32_t points)
{
    Hlsl *h = e->h;
    for (uint32_t i = 0; i < sig->count; i++) {
        const IrElement *el = &sig->elements[i];
        if (el->reg >= 64 || !(el->mask & mask[el->reg]))
            continue;
        bool siv = type == OPERAND_INPUT_PATCH_CONSTANT && el->decl_sysval;
        uint32_t at = begin(e, siv ? OP_DCL_INPUT_SIV : OP_DCL_INPUT);
        if (type == OPERAND_INPUT_PATCH_CONSTANT) {
            put(h, e->w, TOKEN_OPERAND(COMP_4, SELECT_MASK, el->mask & mask[el->reg], type, 1));
        } else {
            put(h, e->w, TOKEN_OPERAND(COMP_4, SELECT_MASK, el->mask & mask[el->reg], type, 2));
            put(h, e->w, points);
        }
        put(h, e->w, el->reg);
        if (siv)
            put(h, e->w, el->decl_sysval);
        finish(e, at);
    }
}

static void declare_sv_inputs(Emit *e, const uint8_t *sv_mask)
{
    Hlsl *h = e->h;
    for (uint32_t t = 0; t < 64; t++) {
        if (!sv_mask[t])
            continue;
        bool scalar = t == OPERAND_INPUT_THREAD_ID_IN_GROUP_FLATTENED || t == OPERAND_INPUT_PRIMITIVEID ||
                      t == OPERAND_INPUT_COVERAGE_MASK || t == OPERAND_INPUT_GS_INSTANCE_ID ||
                      t == OPERAND_OUTPUT_CONTROL_POINT_ID || t == OPERAND_INNER_COVERAGE;
        uint32_t at = begin(e, OP_DCL_INPUT);
        if (scalar)
            put(h, e->w, TOKEN_OPERAND(COMP_1, 0, 0, t, 0));
        else
            put(h, e->w, TOKEN_OPERAND(COMP_4, SELECT_MASK, sv_mask[t], t, 0));
        finish(e, at);
    }
}

static void declare_outputs(Emit *e, const IrSignature *sig)
{
    Hlsl *h = e->h;
    for (uint32_t i = 0; i < sig->count; i++) {
        const IrElement *el = &sig->elements[i];
        if (el->operand) {
            uint32_t at = begin(e, OP_DCL_OUTPUT);
            put(h, e->w, TOKEN_OPERAND(COMP_1, 0, 0, el->operand, 0));
            finish(e, at);
            continue;
        }
        uint32_t sv = el->decl_sysval ? el->decl_sysval : el->sysval;
        bool siv = sv && sv != SV_TARGET;
        uint32_t at = begin(e, siv ? OP_DCL_OUTPUT_SIV : OP_DCL_OUTPUT);
        put(h, e->w, TOKEN_OPERAND(COMP_4, SELECT_MASK, el->mask, OPERAND_OUTPUT, 1));
        put(h, e->w, el->reg);
        if (siv)
            put(h, e->w, sv);
        finish(e, at);
    }
}

static void declare_io(Emit *e, Masks *m)
{
    Hlsl *h = e->h;
    IrShader *s = e->s;
    uint8_t stage = s->profile.stage;
    uint8_t *input_mask = m->in;
    if (stage == STAGE_HULL || stage == STAGE_DOMAIN) {
        if (stage == STAGE_HULL && !e->phase) {
            declare_patch_inputs(e, &s->inputs, m->in, OPERAND_INPUT, s->input_control_points);
            declare_sv_inputs(e, m->sv);
            declare_outputs(e, &s->outputs);
        } else if (stage == STAGE_HULL) {
            declare_patch_inputs(e, &s->inputs, m->in, OPERAND_INPUT_CONTROL_POINT, s->input_control_points);
            declare_patch_inputs(e, &s->outputs, m->out_cp, OPERAND_OUTPUT_CONTROL_POINT, s->output_control_points);
            declare_sv_inputs(e, m->sv);
            declare_outputs(e, &s->patch_constants);
        } else {
            declare_patch_inputs(e, &s->inputs, m->in, OPERAND_INPUT_CONTROL_POINT, s->input_control_points);
            declare_patch_inputs(e, &s->patch_constants, m->patch, OPERAND_INPUT_PATCH_CONSTANT, 0);
            declare_sv_inputs(e, m->sv);
            declare_outputs(e, &s->outputs);
        }
        return;
    }
    for (uint32_t i = 0; i < s->inputs.count; i++) {
        const IrElement *el = &s->inputs.elements[i];
        if (el->reg >= 64)
            continue;
        uint8_t mask = el->mask & input_mask[el->reg];
        if (stage == STAGE_GEOMETRY)
            mask = el->mask & input_mask[el->reg];
        if (!mask)
            continue;
        uint32_t op = OP_DCL_INPUT;
        bool siv = el->sysval != 0;
        bool sgv = el->sysval == SV_VERTEX_ID || el->sysval == SV_INSTANCE_ID || el->sysval == SV_IS_FRONT_FACE ||
                   el->sysval == SV_SAMPLE_INDEX || (el->sysval == SV_PRIMITIVE_ID && stage == STAGE_PIXEL);
        if (stage == STAGE_PIXEL) {
            op = siv ? (sgv ? OP_DCL_INPUT_PS_SGV : OP_DCL_INPUT_PS_SIV) : OP_DCL_INPUT_PS;
            uint32_t mode = sgv ? INTERP_CONSTANT : interpolation(el->interp, el->component_type);
            if (el->sysval == SV_POSITION)
                mode = interpolation(el->interp & ~0x100u, 3) == INTERP_LINEAR_CENTROID ? INTERP_LINEAR_NOPERSPECTIVE_CENTROID
                       : (el->interp & IM_SAMPLE)                                       ? INTERP_LINEAR_NOPERSPECTIVE_SAMPLE
                                                                                         : INTERP_LINEAR_NOPERSPECTIVE;
            if (el->sysval == SV_IS_FRONT_FACE || el->sysval == SV_SAMPLE_INDEX)
                mode = INTERP_CONSTANT;
            op |= mode << 11;
        } else if (siv) {
            op = sgv ? OP_DCL_INPUT_SGV : OP_DCL_INPUT_SIV;
        }
        uint32_t at = begin(e, op);
        if (stage == STAGE_GEOMETRY) {
            put(h, e->w, TOKEN_OPERAND(COMP_4, SELECT_MASK, mask, OPERAND_INPUT, 2));
            put(h, e->w, s->input_control_points);
            put(h, e->w, el->reg);
        } else {
            put(h, e->w, TOKEN_OPERAND(COMP_4, SELECT_MASK, mask, OPERAND_INPUT, 1));
            put(h, e->w, el->reg);
        }
        if (siv)
            put(h, e->w, el->sysval);
        finish(e, at);
    }
    /* system values with operands of their own. */
    declare_sv_inputs(e, m->sv);
    if (stage == STAGE_GEOMETRY) {
        uint32_t at = begin(e, OP_DCL_GS_INPUT_PRIMITIVE | s->gs_input_primitive << 11);
        finish(e, at);
        if (s->profile.major >= 5) {
            at = begin(e, OP_DCL_STREAM);
            put(h, e->w, TOKEN_OPERAND(COMP_0, 0, 0, OPERAND_STREAM, 1));
            put(h, e->w, 0);
            finish(e, at);
        }
        at = begin(e, OP_DCL_GS_OUTPUT_TOPOLOGY | s->gs_output_topology << 11);
        finish(e, at);
    }
    declare_outputs(e, &s->outputs);
    if (stage == STAGE_GEOMETRY) {
        uint32_t at = begin(e, OP_DCL_MAX_OUTPUT_VERTEX_COUNT);
        put(h, e->w, s->gs_max_vertices);
        finish(e, at);
        if (s->gs_instances) {
            at = begin(e, OP_DCL_GS_INSTANCE_COUNT);
            put(h, e->w, s->gs_instances);
            finish(e, at);
        }
    }
}

/* ------------------------------------------------------------------ the program */

static void count_uses(IrShader *s, Alloc *a)
{
    uint32_t uses[96];
    for (uint32_t i = 0; i < s->count; i++) {
        const IrIns *ins = &s->ins[i];
        if (ins->op == IR_NOP)
            continue;
        uint32_t n = ir_ins_uses(ins, uses, 96);
        for (uint32_t k = 0; k < n; k++)
            a->v[uses[k]].uses++;
        if (vector_result(ins->op) && ins->dst_count > 1)
            for (uint32_t k = 0; k < ins->dst_count; k++)
                a->v[ins->dst[k]].group = i + 1;
        if (ins->op == OP_MOV && ins->dst_count == 1 && ins->src[0].kind == SRC_REG && !ins->src[0].mod &&
            !(ins->flags & IRF_SAT)) {
            uint32_t d = ins->dst[0], v = ins->src[0].v;
            if (!a->v[d].hint)
                a->v[d].hint = v;
            if (!a->v[v].hint)
                a->v[v].hint = d;
        }
    }
}

int ir_emit_sm4(IrShader *s, HlBuf *code)
{
    Hlsl *h = s->h;
    bind(s);
    Alloc a = {.s = s, .h = h};
    a.v = hl_alloc(h, (s->vregs + 1) * sizeof(VReg));
    for (uint32_t v = 0; v <= s->vregs; v++)
        a.v[v].slot = -1;
    count_uses(s, &a);
    build_ranges(&a);
    allocate(&a);

    Words bodies[2] = {}, decls = {};
    Emit e = {.s = s, .h = h, .a = &a, .w = &bodies[0], .scratch_base = a.temps};
    e.sm51 = s->profile.major == 5 && s->profile.minor == 1;
    Masks masks[2] = {};
    for (uint32_t i = 0; i < s->count; i++) {
        const IrIns *ins = &s->ins[i];
        if (ins->op == IR_NOP)
            continue;
        if (ins->op == OP_HS_FORK_PHASE) {
            e.phase = 1;
            e.w = &bodies[1];
            continue;
        }
        scan_masks(ins, &masks[e.phase]);
        emit_ins(&e, ins);
    }
    uint32_t temps = a.temps + e.scratch_max;
    s->temp_count = temps;
    bool hull = s->profile.stage == STAGE_HULL;

    /* declarations, in the order fxc writes them. */
    e.w = &decls;
    uint32_t flags = 0;
    if (s->refactoring_allowed)
        flags |= 1u << 11;
    if (s->early_depth)
        flags |= 1u << 13;
    bool raw = false;
    for (uint32_t i = 0; i < s->resource_count; i++) {
        uint32_t dim = resource_dimension(s->resources[i].object);
        if (s->resources[i].used && (dim == RES_RAW_BUFFER || dim == RES_STRUCTURED_BUFFER) &&
            s->resources[i].kind != SRC_SAMPLER)
            raw = true;
    }
    if (raw && s->profile.major < 5)
        flags |= 1u << 14;
    if (s->tgsm_count && s->profile.major < 5)
        flags |= 1u << 14;
    if (hull) {
        uint32_t at = begin(&e, OP_HS_DECLS);
        finish(&e, at);
    }
    if (flags || s->profile.major >= 5) {
        uint32_t at = begin(&e, OP_DCL_GLOBAL_FLAGS | flags);
        finish(&e, at);
    }
    if (s->icb_count) {
        put(h, &decls, OP_CUSTOMDATA | 3u << 11);
        put(h, &decls, s->icb_count + 2);
        for (uint32_t i = 0; i < s->icb_count; i++)
            put(h, &decls, s->icb[i]);
    }
    for (uint32_t i = 0; i < s->cbuffer_count; i++) {
        const IrCbuffer *cb = &s->cbuffers[i];
        if (!cb->used)
            continue;
        uint32_t size = cb->dynamic ? (cb->buffer->size + 15) / 16 : cb->extent;
        if (size < cb->extent)
            size = cb->extent;
        uint32_t at = begin(&e, OP_DCL_CONSTANT_BUFFER | (cb->dynamic ? 1u << 11 : 0));
        if (e.sm51) {
            put(h, &decls, TOKEN_OPERAND(COMP_4, SELECT_SWIZZLE, 0xE4, OPERAND_CONSTANT_BUFFER, 3));
            put(h, &decls, i);
            put(h, &decls, cb->slot);
            put(h, &decls, cb->slot);
            put(h, &decls, size);
            put(h, &decls, cb->space);
        } else {
            put(h, &decls, TOKEN_OPERAND(COMP_4, SELECT_SWIZZLE, 0xE4, OPERAND_CONSTANT_BUFFER, 2));
            put(h, &decls, cb->slot);
            put(h, &decls, size);
        }
        finish(&e, at);
    }
    for (int kind = 0; kind < 3; kind++)
        for (uint32_t i = 0; i < s->resource_count; i++) {
            const IrResource *r = &s->resources[i];
            uint8_t want = kind == 0 ? SRC_SAMPLER : kind == 1 ? SRC_RESOURCE : SRC_UAV;
            if (!r->used || r->kind != want)
                continue;
            /* one declaration per slot. */
            bool seen = false;
            for (uint32_t j = 0; j < i; j++)
                if (s->resources[j].used && s->resources[j].kind == r->kind && s->resources[j].slot == r->slot)
                    seen = true;
            if (!seen)
                declare_resource(&e, r);
        }
    if (hull || s->profile.stage == STAGE_DOMAIN) {
        uint32_t at = begin(&e, OP_DCL_INPUT_CONTROL_POINT_COUNT | s->input_control_points << 11);
        finish(&e, at);
        if (hull) {
            at = begin(&e, OP_DCL_OUTPUT_CONTROL_POINT_COUNT | s->output_control_points << 11);
            finish(&e, at);
        }
        at = begin(&e, OP_DCL_TESS_DOMAIN | s->tess_domain << 11);
        finish(&e, at);
        if (hull) {
            at = begin(&e, OP_DCL_TESS_PARTITIONING | s->tess_partitioning << 11);
            finish(&e, at);
            at = begin(&e, OP_DCL_TESS_OUTPUT_PRIMITIVE | s->tess_output_primitive << 11);
            finish(&e, at);
            at = begin(&e, OP_DCL_HS_MAX_TESSFACTOR);
            union { float f; uint32_t u; } factor = {.f = s->max_tess_factor};
            put(h, &decls, factor.u);
            finish(&e, at);
        }
    }
    /* each phase of a hull shader declares its own inputs, outputs and temps. */
    for (uint32_t phase = 0; phase < (hull ? 2u : 1u); phase++) {
        e.phase = phase;
        if (hull) {
            uint32_t at = begin(&e, phase ? OP_HS_FORK_PHASE : OP_HS_CONTROL_POINT_PHASE);
            finish(&e, at);
        }
        declare_io(&e, &masks[phase]);
        if (temps) {
            uint32_t at = begin(&e, OP_DCL_TEMPS);
            put(h, &decls, temps);
            finish(&e, at);
        }
        for (uint32_t i = 0; i < s->indexable_count; i++) {
            uint32_t at = begin(&e, OP_DCL_INDEXABLE_TEMP);
            put(h, &decls, i);
            put(h, &decls, s->indexables[i].size);
            put(h, &decls, 4);
            finish(&e, at);
        }
        if (hull) {
            for (uint32_t k = 0; k < bodies[phase].count; k++)
                put(h, &decls, bodies[phase].data[k]);
        }
    }
    for (uint32_t i = 0; i < s->tgsm_count; i++) {
        const IrTgsm *g = &s->tgsm[i];
        uint32_t at = begin(&e, OP_DCL_TGSM_STRUCTURED);
        put(h, &decls, TOKEN_OPERAND(COMP_0, 0, 0, OPERAND_THREAD_GROUP_SHARED_MEMORY, 1));
        put(h, &decls, i);
        put(h, &decls, g->stride);
        put(h, &decls, g->count);
        finish(&e, at);
    }
    if (s->profile.stage == STAGE_COMPUTE) {
        uint32_t at = begin(&e, OP_DCL_THREAD_GROUP);
        for (uint32_t i = 0; i < 3; i++)
            put(h, &decls, s->thread_group[i]);
        finish(&e, at);
    }

    static const uint32_t program_type[] = {[STAGE_PIXEL] = 0, [STAGE_VERTEX] = 1, [STAGE_GEOMETRY] = 2,
                                            [STAGE_HULL] = 3,  [STAGE_DOMAIN] = 4, [STAGE_COMPUTE] = 5};
    uint32_t version = program_type[s->profile.stage] << 16 | (uint32_t)s->profile.major << 4 | s->profile.minor;
    Words *body = &bodies[0];
    if (hull)
        body = &bodies[1], body->count = 0;
    uint32_t total = 2 + decls.count + body->count;
    uint32_t header[2] = {version, total};
    hl_buf_append(h, code, (const char *)header, 8);
    hl_buf_append(h, code, (const char *)decls.data, decls.count * 4);
    if (body->count)
        hl_buf_append(h, code, (const char *)body->data, body->count * 4);
    return 0;
}
