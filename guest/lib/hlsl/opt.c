#include "ir.h"

/* ------------------------------------------------------------------ control flow */

bool ir_is_marker(uint16_t op)
{
    switch (op) {
    case OP_IF: case OP_ELSE: case OP_ENDIF: case OP_LOOP: case OP_ENDLOOP: case OP_BREAK: case OP_BREAKC:
    case OP_CONTINUE: case OP_CONTINUEC: case OP_SWITCH: case OP_CASE: case OP_DEFAULT: case OP_ENDSWITCH:
    case OP_RET: case OP_RETC: case OP_HS_FORK_PHASE:
        return true;
    default:
        return false;
    }
}

/* instructions kept whatever their results: stores, flow, synchronization, atomics and the like. */
bool ir_has_side_effects(const IrIns *ins)
{
    if (ir_is_marker(ins->op) || ins->to_output)
        return true;
    switch (ins->op) {
    case IR_STORE: case OP_DISCARD: case OP_EMIT: case OP_EMITTHENCUT: case OP_CUT: case OP_EMIT_STREAM:
    case OP_CUT_STREAM: case OP_EMITTHENCUT_STREAM: case OP_SYNC: case OP_STORE_RAW: case OP_STORE_STRUCTURED:
    case OP_STORE_UAV_TYPED: case OP_ATOMIC_AND: case OP_ATOMIC_OR: case OP_ATOMIC_XOR: case OP_ATOMIC_CMP_STORE:
    case OP_ATOMIC_IADD: case OP_ATOMIC_IMAX: case OP_ATOMIC_IMIN: case OP_ATOMIC_UMAX: case OP_ATOMIC_UMIN:
    case OP_IMM_ATOMIC_ALLOC: case OP_IMM_ATOMIC_CONSUME: case OP_IMM_ATOMIC_IADD: case OP_IMM_ATOMIC_AND:
    case OP_IMM_ATOMIC_OR: case OP_IMM_ATOMIC_XOR: case OP_IMM_ATOMIC_EXCH: case OP_IMM_ATOMIC_CMP_EXCH:
    case OP_IMM_ATOMIC_IMAX: case OP_IMM_ATOMIC_IMIN: case OP_IMM_ATOMIC_UMAX: case OP_IMM_ATOMIC_UMIN:
        return true;
    default:
        return false;
    }
}

/* every virtual register a source reads: its own, and a relative index. */
static inline void src_regs(const IrSrc *s, uint32_t out[2], uint32_t *n)
{
    *n = 0;
    if (s->kind == SRC_REG)
        out[(*n)++] = s->v;
    if (s->rel && s->kind != SRC_REG)
        out[(*n)++] = s->rel;
}

typedef struct Cfg {
    uint32_t block_count;
    /* the first instruction of each block, and one past the last. */
    uint32_t *start, *end;
    uint32_t *succ;
    uint32_t succ_count;
    uint32_t *succ_start;
    uint32_t *block_of;
} Cfg;

typedef struct Frame {
    uint32_t kind;
    uint32_t open;
    uint32_t else_at;
    uint32_t *cases;
    uint32_t case_count, case_capacity;
    bool has_default;
} Frame;

/* blocks split at every marker, and each marker a block of its own. */
static void build_cfg(IrShader *s, Cfg *g)
{
    Hlsl *h = s->h;
    uint32_t n = s->count;
    g->block_of = hl_alloc(h, (n + 1) * sizeof(uint32_t));
    g->start = hl_alloc(h, (n + 2) * sizeof(uint32_t));
    g->end = hl_alloc(h, (n + 2) * sizeof(uint32_t));
    uint32_t b = 0;
    for (uint32_t i = 0; i < n; i++) {
        bool marker = ir_is_marker(s->ins[i].op);
        bool after_marker = i && ir_is_marker(s->ins[i - 1].op);
        if (i == 0 || marker || after_marker) {
            if (i) {
                g->end[b] = i;
                b++;
            }
            g->start[b] = i;
        }
        g->block_of[i] = b;
    }
    g->end[b] = n;
    g->block_count = n ? b + 1 : 0;
    /* successors, at most a few per block except switches. */
    uint32_t capacity = g->block_count * 2 + 16;
    uint32_t *edges_from = hl_alloc(h, capacity * sizeof(uint32_t));
    uint32_t *edges_to = hl_alloc(h, capacity * sizeof(uint32_t));
    uint32_t edge_count = 0;
#define EDGE(f, t)                                                                                                 \
    do {                                                                                                           \
        if (edge_count >= capacity) {                                                                              \
            edges_from = hl_realloc(h, edges_from, capacity * sizeof(uint32_t), capacity * 2 * sizeof(uint32_t));   \
            edges_to = hl_realloc(h, edges_to, capacity * sizeof(uint32_t), capacity * 2 * sizeof(uint32_t));       \
            capacity *= 2;                                                                                         \
        }                                                                                                          \
        edges_from[edge_count] = (f);                                                                              \
        edges_to[edge_count] = (t);                                                                                \
        edge_count++;                                                                                              \
    } while (0)
    Frame *stack = hl_alloc(h, (n + 1) * sizeof(Frame));
    uint32_t depth = 0;
    /* forward pass for matching markers: ifs know their else and endif, loops their end. */
    uint32_t *match = hl_alloc(h, (n + 1) * sizeof(uint32_t));
    uint32_t *loop_of = hl_alloc(h, (n + 1) * sizeof(uint32_t));
    uint32_t *breakable_of = hl_alloc(h, (n + 1) * sizeof(uint32_t));
    for (uint32_t i = 0; i < n; i++) {
        uint16_t op = s->ins[i].op;
        match[i] = ~0u;
        /* the innermost loop, and the innermost loop or switch. */
        uint32_t loop = ~0u, brk = ~0u;
        for (uint32_t d = depth; d > 0; d--) {
            Frame *f = &stack[d - 1];
            if (f->kind == OP_LOOP && loop == ~0u)
                loop = f->open;
            if ((f->kind == OP_LOOP || f->kind == OP_SWITCH) && brk == ~0u)
                brk = f->open;
        }
        loop_of[i] = loop;
        breakable_of[i] = brk;
        switch (op) {
        case OP_IF: case OP_LOOP: case OP_SWITCH:
            stack[depth++] = (Frame){.kind = op, .open = i, .else_at = ~0u};
            break;
        case OP_ELSE:
            if (depth)
                stack[depth - 1].else_at = i;
            break;
        case OP_CASE: case OP_DEFAULT:
            if (depth) {
                Frame *f = &stack[depth - 1];
                HL_PUSH(h, f->cases, f->case_count, f->case_capacity, i);
                if (op == OP_DEFAULT)
                    f->has_default = true;
            }
            break;
        case OP_ENDIF: case OP_ENDLOOP: case OP_ENDSWITCH:
            if (depth) {
                Frame *f = &stack[--depth];
                match[f->open] = i;
                match[i] = f->open;
                if (f->else_at != ~0u) {
                    match[f->else_at] = i;
                    /* the if jumps to the else's body. */
                    loop_of[f->open] = f->else_at;
                } else if (f->kind == OP_IF) {
                    loop_of[f->open] = ~0u;
                }
                if (f->kind == OP_SWITCH) {
                    for (uint32_t c = 0; c < f->case_count; c++)
                        EDGE(g->block_of[f->open], g->block_of[f->cases[c]]);
                    if (!f->has_default)
                        EDGE(g->block_of[f->open], g->block_of[i]);
                }
            }
            break;
        }
    }
    for (uint32_t bi = 0; bi < g->block_count; bi++) {
        uint32_t last = g->end[bi] - 1;
        uint16_t op = s->ins[last].op;
        uint32_t next = bi + 1 < g->block_count ? bi + 1 : ~0u;
        switch (op) {
        case OP_IF: {
            if (next != ~0u)
                EDGE(bi, next);
            /* the false edge goes past the else marker into its body, or to the endif. */
            if (loop_of[last] != ~0u)
                EDGE(bi, g->block_of[loop_of[last]] + 1);
            else if (match[last] != ~0u)
                EDGE(bi, g->block_of[match[last]]);
            break;
        }
        case OP_ELSE:
            if (match[last] != ~0u)
                EDGE(bi, g->block_of[match[last]]);
            break;
        case OP_ENDLOOP:
            if (match[last] != ~0u)
                EDGE(bi, g->block_of[match[last]]);
            break;
        case OP_BREAK: case OP_BREAKC: {
            /* past the endloop or endswitch. */
            uint32_t open = breakable_of[last];
            if (open != ~0u && match[open] != ~0u && g->block_of[match[open]] + 1 < g->block_count)
                EDGE(bi, g->block_of[match[open]] + 1);
            if (op == OP_BREAKC && next != ~0u)
                EDGE(bi, next);
            break;
        }
        case OP_CONTINUE: case OP_CONTINUEC: {
            uint32_t open = loop_of[last];
            if (open != ~0u)
                EDGE(bi, g->block_of[open]);
            if (op == OP_CONTINUEC && next != ~0u)
                EDGE(bi, next);
            break;
        }
        case OP_SWITCH:
            break;
        case OP_RET:
            break;
        default:
            if (next != ~0u)
                EDGE(bi, next);
            break;
        }
    }
#undef EDGE
    /* edges grouped by source. */
    g->succ_start = hl_alloc(h, (g->block_count + 1) * sizeof(uint32_t));
    for (uint32_t e = 0; e < edge_count; e++)
        g->succ_start[edges_from[e] + 1]++;
    for (uint32_t bi = 0; bi < g->block_count; bi++)
        g->succ_start[bi + 1] += g->succ_start[bi];
    g->succ = hl_alloc(h, (edge_count + 1) * sizeof(uint32_t));
    uint32_t *fill = hl_alloc(h, (g->block_count + 1) * sizeof(uint32_t));
    for (uint32_t e = 0; e < edge_count; e++) {
        uint32_t f = edges_from[e];
        g->succ[g->succ_start[f] + fill[f]++] = edges_to[e];
    }
    g->succ_count = edge_count;
}

/* ------------------------------------------------------------------ liveness */

typedef struct Liveness {
    uint32_t words;
    uint64_t *in, *out;
} Liveness;

static inline void bit_set(uint64_t *b, uint32_t v) { b[v >> 6] |= 1ull << (v & 63); }
static inline void bit_clear(uint64_t *b, uint32_t v) { b[v >> 6] &= ~(1ull << (v & 63)); }
static inline bool bit_test(const uint64_t *b, uint32_t v) { return (b[v >> 6] >> (v & 63)) & 1; }

/* the registers an instruction reads, through a callback-free list. */
static uint32_t ins_uses(const IrIns *ins, uint32_t *out, uint32_t max)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < ins->src_count; i++) {
        uint32_t r[2], k;
        src_regs(&ins->src[i], r, &k);
        for (uint32_t j = 0; j < k && n < max; j++)
            out[n++] = r[j];
    }
    if (ins->op == IR_STORE) {
        if (ins->store.rel && n < max)
            out[n++] = ins->store.rel;
        if (ins->store.rel2 && n < max)
            out[n++] = ins->store.rel2;
    }
    if (ins->res_rel && n < max)
        out[n++] = ins->res_rel;
    if (ins->sampler_rel && n < max)
        out[n++] = ins->sampler_rel;
    return n;
}

#define MAX_USES 80

static void liveness(IrShader *s, Cfg *g, Liveness *lv)
{
    Hlsl *h = s->h;
    uint32_t words = (s->vregs + 64) / 64;
    lv->words = words;
    uint32_t blocks = g->block_count;
    lv->in = hl_alloc(h, (size_t)blocks * words * 8 + 8);
    lv->out = hl_alloc(h, (size_t)blocks * words * 8 + 8);
    uint64_t *use = hl_alloc(h, (size_t)blocks * words * 8 + 8);
    uint64_t *def = hl_alloc(h, (size_t)blocks * words * 8 + 8);
    uint32_t uses[MAX_USES];
    for (uint32_t b = 0; b < blocks; b++) {
        uint64_t *u = use + (size_t)b * words, *d = def + (size_t)b * words;
        for (uint32_t i = g->start[b]; i < g->end[b]; i++) {
            const IrIns *ins = &s->ins[i];
            if (ins->op == IR_NOP)
                continue;
            uint32_t n = ins_uses(ins, uses, MAX_USES);
            for (uint32_t k = 0; k < n; k++)
                if (!bit_test(d, uses[k]))
                    bit_set(u, uses[k]);
            for (uint32_t k = 0; k < ins->dst_count; k++)
                bit_set(d, ins->dst[k]);
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (uint32_t b = blocks; b-- > 0;) {
            uint64_t *out = lv->out + (size_t)b * words, *in = lv->in + (size_t)b * words;
            for (uint32_t e = g->succ_start[b]; e < g->succ_start[b + 1]; e++) {
                const uint64_t *sin = lv->in + (size_t)g->succ[e] * words;
                for (uint32_t w = 0; w < words; w++)
                    out[w] |= sin[w];
            }
            const uint64_t *u = use + (size_t)b * words, *d = def + (size_t)b * words;
            for (uint32_t w = 0; w < words; w++) {
                uint64_t v = u[w] | (out[w] & ~d[w]);
                if (v != in[w]) {
                    in[w] = v;
                    changed = true;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ dead code */

static bool dead_code(IrShader *s)
{
    Cfg g = {};
    build_cfg(s, &g);
    Liveness lv = {};
    liveness(s, &g, &lv);
    uint64_t *live = hl_alloc(s->h, lv.words * 8 + 8);
    uint32_t uses[MAX_USES];
    bool removed = false;
    for (uint32_t b = 0; b < g.block_count; b++) {
        __builtin_memcpy(live, lv.out + (size_t)b * lv.words, lv.words * 8);
        for (uint32_t i = g.end[b]; i-- > g.start[b];) {
            IrIns *ins = &s->ins[i];
            if (ins->op == IR_NOP)
                continue;
            bool needed = ir_has_side_effects(ins) || !ins->dst_count;
            for (uint32_t k = 0; k < ins->dst_count && !needed; k++)
                needed = bit_test(live, ins->dst[k]);
            if (!needed) {
                ins->op = IR_NOP;
                removed = true;
                continue;
            }
            for (uint32_t k = 0; k < ins->dst_count; k++)
                bit_clear(live, ins->dst[k]);
            uint32_t n = ins_uses(ins, uses, MAX_USES);
            for (uint32_t k = 0; k < n; k++)
                bit_set(live, uses[k]);
        }
    }
    return removed;
}

/* ------------------------------------------------------------------ copies */

static bool int_op(uint16_t op)
{
    switch (op) {
    case OP_ADD: case OP_MUL: case OP_MAD: case OP_DIV: case OP_MIN: case OP_MAX: case OP_FRC: case OP_ROUND_NE:
    case OP_ROUND_NI: case OP_ROUND_PI: case OP_ROUND_Z: case OP_SQRT: case OP_RSQ: case OP_RCP: case OP_EXP:
    case OP_LOG: case OP_EQ: case OP_NE: case OP_LT: case OP_GE: case OP_MOV: case OP_FTOI: case OP_FTOU:
    case OP_DP2: case OP_DP3: case OP_DP4: case OP_SINCOS: case OP_DERIV_RTX: case OP_DERIV_RTY:
    case OP_DERIV_RTX_COARSE: case OP_DERIV_RTX_FINE: case OP_DERIV_RTY_COARSE: case OP_DERIV_RTY_FINE:
    case IR_STORE:
        return false;
    default:
        return true;
    }
}

/* replaces reads of single definition temps defined by moves of values that do not change. */
static bool propagate_copies(IrShader *s)
{
    Hlsl *h = s->h;
    uint32_t *defs = hl_alloc(h, (s->vregs + 1) * sizeof(uint32_t));
    uint32_t *def_at = hl_alloc(h, (s->vregs + 1) * sizeof(uint32_t));
    for (uint32_t i = 0; i < s->count; i++) {
        IrIns *ins = &s->ins[i];
        if (ins->op == IR_NOP)
            continue;
        for (uint32_t k = 0; k < ins->dst_count; k++) {
            defs[ins->dst[k]]++;
            def_at[ins->dst[k]] = i;
        }
    }
    bool changed = false;
    for (uint32_t i = 0; i < s->count; i++) {
        IrIns *ins = &s->ins[i];
        if (ins->op == IR_NOP)
            continue;
        for (uint32_t k = 0; k < ins->src_count; k++) {
            IrSrc *src = &ins->src[k];
            if (src->kind != SRC_REG || defs[src->v] != 1)
                continue;
            const IrIns *d = &s->ins[def_at[src->v]];
            if (d->op != OP_MOV || (d->flags & IRF_SAT) || d->dst_count != 1)
                continue;
            IrSrc v = d->src[0];
            bool ok;
            switch (v.kind) {
            case SRC_IMM:
            case SRC_IN:
            case SRC_SV:
            case SRC_CB:
            case SRC_ICB:
            case SRC_PATCH:
            case SRC_OUT_CP:
                ok = !v.rel || defs[v.rel] == 1;
                break;
            case SRC_REG:
                ok = defs[v.v] == 1;
                break;
            default:
                ok = false;
            }
            if (!ok)
                continue;
            /* modifiers combine: abs of anything drops its sign. */
            uint8_t mod = v.mod;
            if (src->mod & MODIFIER_ABS)
                mod = MODIFIER_ABS;
            if (src->mod & MODIFIER_NEG)
                mod ^= MODIFIER_NEG;
            if (mod && int_op(ins->op))
                continue;
            if (v.kind == SRC_IMM && mod) {
                union { uint32_t u; float f; } c = {.u = v.v};
                if (mod & MODIFIER_ABS)
                    c.f = __builtin_fabsf(c.f);
                if (mod & MODIFIER_NEG)
                    c.f = -c.f;
                v.v = c.u;
                mod = 0;
            }
            v.mod = mod;
            *src = v;
            changed = true;
        }
    }
    return changed;
}

/* mul then add of its single use becomes mad, when nothing between redefines the mul's sources. */
static bool fuse_mad(IrShader *s)
{
    Hlsl *h = s->h;
    uint32_t *defs = hl_alloc(h, (s->vregs + 1) * sizeof(uint32_t));
    uint32_t *uses = hl_alloc(h, (s->vregs + 1) * sizeof(uint32_t));
    uint32_t *def_at = hl_alloc(h, (s->vregs + 1) * sizeof(uint32_t));
    uint32_t list[MAX_USES];
    for (uint32_t i = 0; i < s->count; i++) {
        IrIns *ins = &s->ins[i];
        if (ins->op == IR_NOP)
            continue;
        for (uint32_t k = 0; k < ins->dst_count; k++) {
            defs[ins->dst[k]]++;
            def_at[ins->dst[k]] = i;
        }
        uint32_t n = ins_uses(ins, list, MAX_USES);
        for (uint32_t k = 0; k < n; k++)
            uses[list[k]]++;
    }
    bool changed = false;
    for (uint32_t i = 0; i < s->count; i++) {
        IrIns *add = &s->ins[i];
        if (add->op != OP_ADD || (add->flags & IRF_PRECISE))
            continue;
        for (uint32_t side = 0; side < 2; side++) {
            IrSrc *t = &add->src[side];
            if (t->kind != SRC_REG || (t->mod & MODIFIER_ABS) || defs[t->v] != 1 || uses[t->v] != 1)
                continue;
            uint32_t at = def_at[t->v];
            IrIns *mul = &s->ins[at];
            if (mul->op != OP_MUL || at > i || (mul->flags & (IRF_SAT | IRF_PRECISE)))
                continue;
            /* straight code between them, with the sources unchanged. */
            bool ok = true;
            for (uint32_t j = at + 1; j < i && ok; j++) {
                const IrIns *x = &s->ins[j];
                if (x->op == IR_NOP)
                    continue;
                if (ir_is_marker(x->op)) {
                    ok = false;
                    break;
                }
                for (uint32_t k = 0; k < x->dst_count; k++)
                    for (uint32_t m = 0; m < 2; m++)
                        if ((mul->src[m].kind == SRC_REG && mul->src[m].v == x->dst[k]) || mul->src[m].rel == x->dst[k])
                            ok = false;
            }
            if (!ok)
                continue;
            IrSrc a = mul->src[0], b = mul->src[1], c = add->src[1 - side];
            if (t->mod & MODIFIER_NEG)
                a.mod ^= MODIFIER_NEG;
            IrSrc *src = hl_alloc(h, 3 * sizeof(IrSrc));
            src[0] = a;
            src[1] = b;
            src[2] = c;
            add->op = OP_MAD;
            add->src = src;
            add->src_count = 3;
            mul->op = IR_NOP;
            changed = true;
            break;
        }
    }
    return changed;
}

static bool vector_op(uint16_t op)
{
    switch (op) {
    case OP_SAMPLE: case OP_SAMPLE_B: case OP_SAMPLE_D: case OP_SAMPLE_L: case OP_SAMPLE_C: case OP_SAMPLE_C_LZ:
    case OP_LD: case OP_LD_MS: case OP_LD_UAV_TYPED: case OP_LD_STRUCTURED: case OP_LD_RAW: case OP_GATHER4:
    case OP_GATHER4_C: case OP_GATHER4_PO: case OP_GATHER4_PO_C: case OP_RESINFO: case OP_LOD: case OP_SAMPLE_POS:
    case OP_BUFINFO: case OP_SAMPLE_INFO: case OP_SINCOS: case OP_UDIV: case OP_IMUL: case OP_UMUL: case OP_DP2:
    case OP_DP3: case OP_DP4: case OP_EVAL_CENTROID: case OP_EVAL_SAMPLE_INDEX: case OP_EVAL_SNAPPED:
        return true;
    default:
        return op >= IR_STORE || op >= OP_ATOMIC_AND;
    }
}

static bool cse_candidate(const IrIns *ins)
{
    if (ins->op == IR_NOP || ins->dst_count != 1 || ins->to_output || ins->res_kind || ir_has_side_effects(ins) ||
        ins->group_count)
        return false;
    switch (ins->op) {
    case OP_SINCOS: case OP_UDIV: case OP_DERIV_RTX: case OP_DERIV_RTY: case OP_DERIV_RTX_COARSE:
    case OP_DERIV_RTX_FINE: case OP_DERIV_RTY_COARSE: case OP_DERIV_RTY_FINE: case OP_EVAL_CENTROID:
    case OP_EVAL_SAMPLE_INDEX: case OP_EVAL_SNAPPED:
        return false;
    default:
        return ins->op < IR_STORE;
    }
}

static bool same_src(const IrSrc *a, const IrSrc *b)
{
    return a->kind == b->kind && a->mod == b->mod && a->comp == b->comp && a->v == b->v && a->index == b->index &&
           a->rel == b->rel;
}

/* the same pure instruction twice in straight code, over values that do not change, computes once. */
static bool common_subexpressions(IrShader *s)
{
    Hlsl *h = s->h;
    uint32_t *defs = hl_alloc(h, (s->vregs + 1) * sizeof(uint32_t));
    uint32_t *rename = hl_alloc(h, (s->vregs + 1) * sizeof(uint32_t));
    for (uint32_t i = 0; i < s->count; i++)
        if (s->ins[i].op != IR_NOP)
            for (uint32_t k = 0; k < s->ins[i].dst_count; k++)
                defs[s->ins[i].dst[k]]++;
    uint32_t window[256], count = 0;
    bool changed = false;
    for (uint32_t i = 0; i < s->count; i++) {
        IrIns *ins = &s->ins[i];
        if (ins->op == IR_NOP)
            continue;
        if (ir_is_marker(ins->op)) {
            count = 0;
            continue;
        }
        for (uint32_t k = 0; k < ins->src_count; k++) {
            IrSrc *src = &ins->src[k];
            if (src->kind == SRC_REG && rename[src->v])
                src->v = rename[src->v];
            if (src->rel && rename[src->rel])
                src->rel = rename[src->rel];
        }
        if (ins->op == IR_STORE && ins->store.rel && rename[ins->store.rel])
            ins->store.rel = rename[ins->store.rel];
        if (ins->res_rel && rename[ins->res_rel])
            ins->res_rel = rename[ins->res_rel];
        if (ins->sampler_rel && rename[ins->sampler_rel])
            ins->sampler_rel = rename[ins->sampler_rel];
        if (!cse_candidate(ins) || defs[ins->dst[0]] != 1)
            continue;
        bool stable = true;
        for (uint32_t k = 0; k < ins->src_count && stable; k++) {
            const IrSrc *src = &ins->src[k];
            if (src->kind == SRC_X || src->kind == SRC_TGSM || src->kind == SRC_UAV)
                stable = false;
            if (src->kind == SRC_REG && defs[src->v] != 1)
                stable = false;
            if (src->rel && defs[src->rel] != 1)
                stable = false;
        }
        if (!stable)
            continue;
        uint32_t found = 0;
        for (uint32_t w = 0; w < count && !found; w++) {
            const IrIns *o = &s->ins[window[w]];
            if (o->op != ins->op || o->flags != ins->flags || o->src_count != ins->src_count || o->op == IR_NOP)
                continue;
            bool same = true;
            for (uint32_t k = 0; k < ins->src_count && same; k++)
                same = same_src(&o->src[k], &ins->src[k]);
            if (same)
                found = o->dst[0];
        }
        if (found) {
            rename[ins->dst[0]] = found;
            ins->op = IR_NOP;
            changed = true;
            continue;
        }
        if (count < 256)
            window[count++] = i;
        else
            count = 0;
    }
    return changed;
}

/* a result whose only use is a store to an output in the same straight code writes the output itself. */
static void fold_output_stores(IrShader *s)
{
    Hlsl *h = s->h;
    uint32_t *defs = hl_alloc(h, (s->vregs + 1) * sizeof(uint32_t));
    uint32_t *uses = hl_alloc(h, (s->vregs + 1) * sizeof(uint32_t));
    uint32_t *def_at = hl_alloc(h, (s->vregs + 1) * sizeof(uint32_t));
    uint32_t list[MAX_USES];
    for (uint32_t i = 0; i < s->count; i++) {
        IrIns *ins = &s->ins[i];
        if (ins->op == IR_NOP)
            continue;
        for (uint32_t k = 0; k < ins->dst_count; k++) {
            defs[ins->dst[k]]++;
            def_at[ins->dst[k]] = i;
        }
        uint32_t n = ins_uses(ins, list, MAX_USES);
        for (uint32_t k = 0; k < n; k++)
            uses[list[k]]++;
    }
    for (uint32_t i = 0; i < s->count; i++) {
        IrIns *st = &s->ins[i];
        if (st->op != IR_STORE || (st->store.kind != DST_OUT && st->store.kind != DST_SPECIAL))
            continue;
        IrSrc *v = &st->src[0];
        if (v->kind != SRC_REG || v->mod || defs[v->v] != 1 || uses[v->v] != 1)
            continue;
        uint32_t at = def_at[v->v];
        IrIns *def = &s->ins[at];
        if (at > i || def->dst_count != 1 || vector_op(def->op) || def->to_output)
            continue;
        bool ok = true;
        for (uint32_t j = at + 1; j < i && ok; j++) {
            const IrIns *x = &s->ins[j];
            if (x->op == IR_NOP)
                continue;
            if (ir_is_marker(x->op) || (x->op == IR_STORE && x->store.kind == st->store.kind && x->store.v == st->store.v &&
                                        x->store.comp == st->store.comp))
                ok = false;
        }
        if (!ok)
            continue;
        def->to_output = true;
        def->output = st->store;
        st->op = IR_NOP;
    }
}

void ir_optimize(IrShader *s)
{
    for (int round = 0; round < 8; round++) {
        bool changed = propagate_copies(s);
        changed |= common_subexpressions(s);
        changed |= dead_code(s);
        if (!changed)
            break;
    }
    if (!(s->h->options->flags & HLSL_SKIP_OPTIMIZATION) && fuse_mad(s))
        dead_code(s);
    /* shader models 2 and 3 write pixel outputs with whole movs, which the emitter makes. */
    if (!s->legacy)
        fold_output_stores(s);
}

/* ------------------------------------------------------------------ exported for the emitter */

void ir_cfg_liveness(IrShader *s, uint32_t *block_count, uint32_t **start, uint32_t **end, uint64_t **live_out,
                     uint32_t *words)
{
    Cfg g = {};
    build_cfg(s, &g);
    Liveness lv = {};
    liveness(s, &g, &lv);
    *block_count = g.block_count;
    *start = g.start;
    *end = g.end;
    *live_out = lv.out;
    *words = lv.words;
}

uint32_t ir_ins_uses(const IrIns *ins, uint32_t *out, uint32_t max) { return ins_uses(ins, out, max); }

/* ------------------------------------------------------------------ dumps */

const char *ir_op_name(uint32_t op)
{
    static const struct {
        uint32_t op;
        const char *name;
    } names[] = {
        {OP_ADD, "add"}, {OP_AND, "and"}, {OP_BREAK, "break"}, {OP_BREAKC, "breakc"}, {OP_CASE, "case"},
        {OP_CONTINUE, "continue"}, {OP_CONTINUEC, "continuec"}, {OP_CUT, "cut"}, {OP_DEFAULT, "default"},
        {OP_DERIV_RTX, "deriv_rtx"}, {OP_DERIV_RTY, "deriv_rty"}, {OP_DISCARD, "discard"}, {OP_DIV, "div"},
        {OP_DP2, "dp2"}, {OP_DP3, "dp3"}, {OP_DP4, "dp4"}, {OP_ELSE, "else"}, {OP_EMIT, "emit"},
        {OP_ENDIF, "endif"}, {OP_ENDLOOP, "endloop"}, {OP_ENDSWITCH, "endswitch"}, {OP_EQ, "eq"}, {OP_EXP, "exp"},
        {OP_FRC, "frc"}, {OP_FTOI, "ftoi"}, {OP_FTOU, "ftou"}, {OP_GE, "ge"}, {OP_IADD, "iadd"}, {OP_IF, "if"},
        {OP_IEQ, "ieq"}, {OP_IGE, "ige"}, {OP_ILT, "ilt"}, {OP_IMAD, "imad"}, {OP_IMAX, "imax"}, {OP_IMIN, "imin"},
        {OP_IMUL, "imul"}, {OP_INE, "ine"}, {OP_INEG, "ineg"}, {OP_ISHL, "ishl"}, {OP_ISHR, "ishr"},
        {OP_ITOF, "itof"}, {OP_LD, "ld"}, {OP_LD_MS, "ld_ms"}, {OP_LOG, "log"}, {OP_LOOP, "loop"}, {OP_LT, "lt"},
        {OP_MAD, "mad"}, {OP_MIN, "min"}, {OP_MAX, "max"}, {OP_MOV, "mov"}, {OP_MOVC, "movc"}, {OP_MUL, "mul"},
        {OP_NE, "ne"}, {OP_NOT, "not"}, {OP_OR, "or"}, {OP_RESINFO, "resinfo"}, {OP_RET, "ret"},
        {OP_ROUND_NE, "round_ne"}, {OP_ROUND_NI, "round_ni"}, {OP_ROUND_PI, "round_pi"}, {OP_ROUND_Z, "round_z"},
        {OP_RSQ, "rsq"}, {OP_SAMPLE, "sample"}, {OP_SAMPLE_C, "sample_c"}, {OP_SAMPLE_C_LZ, "sample_c_lz"},
        {OP_SAMPLE_L, "sample_l"}, {OP_SAMPLE_D, "sample_d"}, {OP_SAMPLE_B, "sample_b"}, {OP_SQRT, "sqrt"},
        {OP_SWITCH, "switch"}, {OP_SINCOS, "sincos"}, {OP_UDIV, "udiv"}, {OP_ULT, "ult"}, {OP_UGE, "uge"},
        {OP_UMUL, "umul"}, {OP_UMAD, "umad"}, {OP_UMAX, "umax"}, {OP_UMIN, "umin"}, {OP_USHR, "ushr"},
        {OP_UTOF, "utof"}, {OP_XOR, "xor"}, {OP_LOD, "lod"}, {OP_GATHER4, "gather4"}, {OP_SAMPLE_POS, "sample_pos"},
        {OP_SAMPLE_INFO, "sample_info"}, {OP_BUFINFO, "bufinfo"}, {OP_RCP, "rcp"}, {OP_F32TOF16, "f32tof16"},
        {OP_F16TOF32, "f16tof32"}, {OP_COUNTBITS, "countbits"}, {OP_FIRSTBIT_HI, "firstbit_hi"},
        {OP_FIRSTBIT_LO, "firstbit_lo"}, {OP_FIRSTBIT_SHI, "firstbit_shi"}, {OP_BFREV, "bfrev"},
        {OP_LD_UAV_TYPED, "ld_uav_typed"}, {OP_STORE_UAV_TYPED, "store_uav_typed"}, {OP_LD_RAW, "ld_raw"},
        {OP_STORE_RAW, "store_raw"}, {OP_LD_STRUCTURED, "ld_structured"}, {OP_STORE_STRUCTURED, "store_structured"},
        {OP_ATOMIC_IADD, "atomic_iadd"}, {OP_IMM_ATOMIC_IADD, "imm_atomic_iadd"}, {OP_SYNC, "sync"},
        {IR_STORE, "store"}, {IR_NOP, "nop"},
    };
    for (size_t i = 0; i < HL_ARRAY_SIZE(names); i++)
        if (names[i].op == op)
            return names[i].name;
    return "op";
}

static void dump_src(IrShader *s, HlBuf *out, const IrSrc *src)
{
    Hlsl *h = s->h;
    const char *neg = src->mod & MODIFIER_NEG ? "-" : "";
    const char *abs = src->mod & MODIFIER_ABS ? "|" : "";
    static const char xyzw[] = "xyzw";
    switch (src->kind) {
    case SRC_REG: hl_buf_printf(h, out, "%s%s%%%u%s", neg, abs, src->v, abs); break;
    case SRC_IMM: hl_buf_printf(h, out, "%s%s0x%x%s", neg, abs, src->v, abs); break;
    case SRC_CB: hl_buf_printf(h, out, "%s%scb%u[%u%s%u].%c%s", neg, abs, src->v, src->index, src->rel ? "+%" : "", src->rel, xyzw[src->comp & 3], abs); break;
    case SRC_IN: hl_buf_printf(h, out, "%s%sv%u.%c%s", neg, abs, src->v, xyzw[src->comp & 3], abs); break;
    case SRC_SV: hl_buf_printf(h, out, "%s%ssv%u.%c%s", neg, abs, src->v, xyzw[src->comp & 3], abs); break;
    case SRC_ICB: hl_buf_printf(h, out, "icb[%u+%%%u].%c", src->index, src->rel, xyzw[src->comp & 3]); break;
    case SRC_X: hl_buf_printf(h, out, "x%u[%u+%%%u].%c", src->v, src->index, src->rel, xyzw[src->comp & 3]); break;
    default: hl_buf_printf(h, out, "?%u", src->kind); break;
    }
}

void ir_dump(IrShader *s, HlBuf *out)
{
    Hlsl *h = s->h;
    for (uint32_t i = 0; i < s->count; i++) {
        const IrIns *ins = &s->ins[i];
        if (ins->op == IR_NOP)
            continue;
        hl_buf_printf(h, out, "%4u: %s%s", i, ir_op_name(ins->op), ins->flags & IRF_SAT ? "_sat" : "");
        if (ins->flags & IRF_NZ)
            hl_buf_puts(h, out, "_nz");
        for (uint32_t k = 0; k < ins->dst_count; k++)
            hl_buf_printf(h, out, " %%%u", ins->dst[k]);
        if (ins->op == IR_STORE)
            hl_buf_printf(h, out, " [%u:%u.%u+%%%u]", ins->store.kind, ins->store.v, ins->store.index, ins->store.rel);
        for (uint32_t k = 0; k < ins->src_count; k++) {
            hl_buf_puts(h, out, k || ins->dst_count ? ", " : " ");
            dump_src(s, out, &ins->src[k]);
        }
        if (ins->res_kind)
            hl_buf_printf(h, out, " res%u:%u", ins->res_kind, ins->res);
        hl_buf_putc(h, out, '\n');
    }
}
