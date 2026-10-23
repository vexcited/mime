/*
 * structures a dxil function's control flow for spir-v. each loop gets a
 * header holding only its phis, one continue block with the back edge and
 * one merge block only its exits reach. every branch becomes a selection
 * with its own merge, the nearest post-dominator in the loop body, and
 * shared merges become a ladder of blocks. edges to a return block shared
 * with other paths get their own copy of it when they would leave a
 * construct.
 */
#include "cfg.h"

static int fail(Cfg *c, const char *why)
{
    if (!c->error[0]) {
        uint32_t i = 0;
        for (; why[i] && i + 1 < sizeof(c->error); i++)
            c->error[i] = why[i];
        c->error[i] = 0;
    }
    return -1;
}

static void *alloc(Cfg *c, size_t size)
{
    void *p = bc_arena_alloc(c->arena, size ? size : 1);
    if (!p)
        fail(c, "out of memory");
    return p;
}

static uint32_t *u32s(Cfg *c, uint32_t count) { return alloc(c, count * sizeof(uint32_t)); }

/* zeroed, freed when the pass's mark is released. */
static void *tmp(Cfg *c, size_t size)
{
    void *p = bc_arena_alloc(&c->scratch, size ? size : 1);
    if (!p)
        fail(c, "out of memory");
    return p;
}

static uint32_t *tmp32(Cfg *c, uint32_t count) { return tmp(c, count * sizeof(uint32_t)); }

static uint32_t add_block(Cfg *c)
{
    if (c->count == c->capacity) {
        uint32_t want = c->capacity ? c->capacity * 2 : 64;
        CfgBlock *blocks = alloc(c, want * sizeof(CfgBlock));
        if (!blocks)
            return DX_NONE;
        if (c->blocks)
            __builtin_memcpy(blocks, c->blocks, c->count * sizeof(CfgBlock));
        c->blocks = blocks;
        c->capacity = want;
    }
    CfgBlock *b = &c->blocks[c->count];
    __builtin_memset(b, 0, sizeof(*b));
    b->source = DX_NONE;
    b->merge = b->cont = DX_NONE;
    b->idom = DX_NONE;
    b->loop = b->parent = DX_NONE;
    b->cond = DX_NONE;
    return c->count++;
}

static uint32_t add_synth(Cfg *c, uint32_t type)
{
    if (c->synth_count == c->synth_capacity) {
        uint32_t want = c->synth_capacity ? c->synth_capacity * 2 : 16;
        uint32_t *types = u32s(c, want);
        if (!types)
            return CFG_UNDEF;
        if (c->synth_types)
            __builtin_memcpy(types, c->synth_types, c->synth_count * 4);
        c->synth_types = types;
        c->synth_capacity = want;
    }
    c->synth_types[c->synth_count] = type;
    return CFG_SYNTH + c->synth_count++;
}

static void set_succ(Cfg *c, uint32_t b, const uint32_t *succ, uint32_t count)
{
    uint32_t *s = u32s(c, count);
    if (!s)
        return;
    __builtin_memcpy(s, succ, count * 4);
    c->blocks[b].succ = s;
    c->blocks[b].succ_count = count;
}

static void branch_to(Cfg *c, uint32_t b, uint32_t target)
{
    set_succ(c, b, &target, 1);
    CfgBlock *blk = &c->blocks[b];
    blk->term = TERM_BRANCH;
    blk->cond = DX_NONE;
    blk->cases = nullptr;
}

static void redirect(Cfg *c, uint32_t from, uint32_t old_to, uint32_t new_to)
{
    CfgBlock *b = &c->blocks[from];
    for (uint32_t i = 0; i < b->succ_count; i++)
        if (b->succ[i] == old_to)
            b->succ[i] = new_to;
}

/* ------------------------------------------------------------------ phis */

static CfgPhi *add_phi(Cfg *c, uint32_t b, uint32_t result, uint32_t type, uint32_t capacity)
{
    CfgPhi *phis = alloc(c, (c->blocks[b].phi_count + 1) * sizeof(CfgPhi));
    uint32_t *preds = u32s(c, capacity ? capacity : 1), *values = u32s(c, capacity ? capacity : 1);
    if (!phis || !preds || !values)
        return nullptr;
    CfgBlock *blk = &c->blocks[b];
    if (blk->phis)
        __builtin_memcpy(phis, blk->phis, blk->phi_count * sizeof(CfgPhi));
    blk->phis = phis;
    CfgPhi *p = &phis[blk->phi_count++];
    p->result = result;
    p->type = type;
    p->count = 0;
    p->preds = preds;
    p->values = values;
    return p;
}

static uint32_t phi_value(const CfgPhi *p, uint32_t pred)
{
    for (uint32_t i = 0; i < p->count; i++)
        if (p->preds[i] == pred)
            return p->values[i];
    return CFG_UNDEF;
}

static int phi_has(const CfgPhi *p, uint32_t pred)
{
    for (uint32_t i = 0; i < p->count; i++)
        if (p->preds[i] == pred)
            return 1;
    return 0;
}

static void phi_remove(CfgPhi *p, uint32_t pred)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < p->count; i++)
        if (p->preds[i] != pred) {
            p->preds[n] = p->preds[i];
            p->values[n++] = p->values[i];
        }
    p->count = n;
}

static void phi_add(Cfg *c, CfgPhi *p, uint32_t pred, uint32_t value)
{
    uint32_t *preds = u32s(c, p->count + 1), *values = u32s(c, p->count + 1);
    if (!preds || !values)
        return;
    __builtin_memcpy(preds, p->preds, p->count * 4);
    __builtin_memcpy(values, p->values, p->count * 4);
    preds[p->count] = pred;
    values[p->count] = value;
    p->preds = preds;
    p->values = values;
    p->count++;
}

/* ------------------------------------------------------------------ analysis */

static int live(const Cfg *c, uint32_t b) { return b < c->count && !c->blocks[b].dead; }

static int orphan(const Cfg *c, uint32_t b) { return c->blocks[b].idom == DX_NONE; }

/* returns the count, leaving out unreachable blocks. */
static uint32_t reverse_post_order(Cfg *c, uint32_t *order)
{
    BcMark mark = bc_arena_mark(&c->scratch);
    uint32_t n = c->count;
    uint8_t *seen = tmp(c, n);
    uint32_t *stack = tmp32(c, n), *next = tmp32(c, n);
    uint32_t count = 0;
    if (seen && stack && next) {
        uint32_t top = 0, post = n;
        stack[top++] = c->entry;
        seen[c->entry] = 1;
        while (top) {
            uint32_t b = stack[top - 1];
            const CfgBlock *blk = &c->blocks[b];
            if (next[b] < blk->succ_count) {
                uint32_t s = blk->succ[next[b]++];
                if (live(c, s) && !seen[s]) {
                    seen[s] = 1;
                    stack[top++] = s;
                }
                continue;
            }
            order[--post] = b;
            top--;
        }
        count = n - post;
        __builtin_memmove(order, order + post, count * 4);
    }
    bc_arena_release(&c->scratch, mark);
    return count;
}

/* recomputes predecessors and dominators. */
static void analyze(Cfg *c)
{
    uint32_t n = c->count, edges = 0;
    for (uint32_t b = 0; b < n; b++) {
        CfgBlock *blk = &c->blocks[b];
        blk->pred = nullptr;
        blk->pred_count = 0;
        blk->idom = DX_NONE;
        if (live(c, b))
            edges += blk->succ_count;
    }
    if (edges > c->pool_capacity) {
        uint32_t want = edges * 2 + 64;
        c->pool = u32s(c, want);
        if (!c->pool)
            return;
        c->pool_capacity = want;
    }
    BcMark mark = bc_arena_mark(&c->scratch);
    uint32_t *order = tmp32(c, n), *index = tmp32(c, n), *in = tmp32(c, n);
    if (!order || !index || !in) {
        bc_arena_release(&c->scratch, mark);
        return;
    }
    /* the pool holds each block's predecessors in turn. */
    for (uint32_t b = 0; b < n; b++)
        if (live(c, b))
            for (uint32_t i = 0; i < c->blocks[b].succ_count; i++)
                if (live(c, c->blocks[b].succ[i]))
                    in[c->blocks[b].succ[i]]++;
    uint32_t at = 0;
    for (uint32_t b = 0; b < n; b++) {
        c->blocks[b].pred = c->pool + at;
        at += in[b];
    }
    for (uint32_t b = 0; b < n; b++) {
        if (!live(c, b))
            continue;
        const CfgBlock *blk = &c->blocks[b];
        for (uint32_t i = 0; i < blk->succ_count; i++) {
            uint32_t s = blk->succ[i];
            if (!live(c, s))
                continue;
            CfgBlock *sb = &c->blocks[s];
            int known = 0;
            for (uint32_t k = 0; k < sb->pred_count; k++)
                known |= sb->pred[k] == b;
            if (!known)
                sb->pred[sb->pred_count++] = b;
        }
    }
    uint32_t count = reverse_post_order(c, order);
    for (uint32_t b = 0; b < n; b++)
        index[b] = DX_NONE;
    for (uint32_t i = 0; i < count; i++)
        index[order[i]] = i;
    /* Cooper, Harvey and Kennedy's iteration over the reverse post order. */
    c->blocks[c->entry].idom = c->entry;
    for (int changed = 1; changed;) {
        changed = 0;
        for (uint32_t i = 1; i < count; i++) {
            uint32_t b = order[i], best = DX_NONE;
            const CfgBlock *blk = &c->blocks[b];
            for (uint32_t k = 0; k < blk->pred_count; k++) {
                uint32_t p = blk->pred[k];
                if (index[p] == DX_NONE || c->blocks[p].idom == DX_NONE)
                    continue;
                if (best == DX_NONE) {
                    best = p;
                    continue;
                }
                uint32_t x = p, y = best;
                while (x != y) {
                    while (index[x] > index[y])
                        x = c->blocks[x].idom;
                    while (index[y] > index[x])
                        y = c->blocks[y].idom;
                }
                best = x;
            }
            if (best != DX_NONE && c->blocks[b].idom != best) {
                c->blocks[b].idom = best;
                changed = 1;
            }
        }
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t b = order[i];
        c->blocks[b].depth = b == c->entry ? 0 : c->blocks[c->blocks[b].idom].depth + 1;
    }
    bc_arena_release(&c->scratch, mark);
}

int cfg_dominates(const Cfg *c, uint32_t a, uint32_t b)
{
    if (a >= c->count || b >= c->count || c->blocks[a].idom == DX_NONE || c->blocks[b].idom == DX_NONE)
        return 0;
    while (c->blocks[b].depth > c->blocks[a].depth)
        b = c->blocks[b].idom;
    return a == b;
}

/* ------------------------------------------------------------------ building */

static int is_exit_block(const CfgBlock *b) { return b->term == TERM_RETURN || b->term == TERM_UNREACHABLE; }

/* an edge to a block that only returns and has no other predecessor: a return in place. */
static int terminal(const Cfg *c, uint32_t from, uint32_t to)
{
    const CfgBlock *t = &c->blocks[to];
    return is_exit_block(t) && t->pred_count == 1 && t->pred[0] == from;
}

static int build_initial(Cfg *c)
{
    const DxFunction *f = c->function;
    c->entry = add_block(c);
    for (uint32_t i = 0; i < f->block_count; i++)
        add_block(c);
    if (c->error[0])
        return -1;
    branch_to(c, c->entry, 1);
    for (uint32_t i = 0; i < f->block_count; i++) {
        uint32_t b = i + 1;
        const DxBlock *db = &f->blocks[i];
        c->blocks[b].source = i;
        if (!db->count)
            return fail(c, "an empty block");
        const DxInst *t = &f->insts[db->first + db->count - 1];
        switch (t->op) {
        case DI_BR:
            if (t->count == 1 || t->ops[0] == t->ops[1]) {
                branch_to(c, b, t->ops[0] + 1);
            } else {
                uint32_t succ[2] = {t->ops[0] + 1, t->ops[1] + 1};
                set_succ(c, b, succ, 2);
                c->blocks[b].term = TERM_COND;
                c->blocks[b].cond = t->ops[2];
            }
            break;
        case DI_SWITCH: {
            uint32_t cases = (t->count - 2) / 2;
            uint32_t *succ = u32s(c, 1 + cases);
            uint64_t *values = alloc(c, (cases ? cases : 1) * sizeof(uint64_t));
            if (!succ || !values)
                return -1;
            succ[0] = t->ops[1] + 1;
            for (uint32_t k = 0; k < cases; k++) {
                const DxValue *v = dx_value(c->module, f, t->ops[2 + 2 * k]);
                if (!v || (v->kind != DV_INT && v->kind != DV_NULL))
                    return fail(c, "a switch case that is not a constant");
                values[k] = v->bits;
                succ[1 + k] = t->ops[3 + 2 * k] + 1;
            }
            CfgBlock *blk = &c->blocks[b];
            blk->term = TERM_SWITCH;
            blk->cond = t->ops[0];
            blk->succ = succ;
            blk->succ_count = 1 + cases;
            blk->cases = values;
            break;
        }
        case DI_RET: c->blocks[b].term = TERM_RETURN; break;
        case DI_UNREACHABLE: c->blocks[b].term = TERM_UNREACHABLE; break;
        default: return fail(c, "a block without a terminator");
        }
        for (uint32_t k = 0; k < c->blocks[b].succ_count; k++)
            if (c->blocks[b].succ[k] > f->block_count)
                return fail(c, "a branch to no block");
        for (uint32_t k = 0; k < db->count; k++) {
            const DxInst *ins = &f->insts[db->first + k];
            if (ins->op != DI_PHI)
                continue;
            CfgPhi *p = add_phi(c, b, ins->value, ins->type, ins->count / 2);
            if (!p)
                return -1;
            /* llvm repeats an entry for each edge from a block; spir-v takes one. */
            for (uint32_t e = 0; e + 1 < ins->count; e += 2) {
                uint32_t pred = ins->ops[e + 1] + 1;
                if (!phi_has(p, pred)) {
                    p->preds[p->count] = pred;
                    p->values[p->count++] = ins->ops[e];
                }
            }
        }
    }
    return c->error[0] ? -1 : 0;
}

static void remove_dead(Cfg *c)
{
    BcMark mark = bc_arena_mark(&c->scratch);
    uint32_t *order = tmp32(c, c->count);
    uint8_t *reached = tmp(c, c->count);
    if (order && reached) {
        uint32_t count = reverse_post_order(c, order);
        for (uint32_t i = 0; i < count; i++)
            reached[order[i]] = 1;
        for (uint32_t b = 0; b < c->count; b++)
            if (!reached[b])
                c->blocks[b].dead = 1;
        for (uint32_t b = 0; b < c->count; b++)
            for (uint32_t k = 0; k < c->blocks[b].phi_count; k++) {
                CfgPhi *p = &c->blocks[b].phis[k];
                uint32_t n = 0;
                for (uint32_t i = 0; i < p->count; i++)
                    if (reached[p->preds[i]]) {
                        p->preds[n] = p->preds[i];
                        p->values[n++] = p->values[i];
                    }
                p->count = n;
            }
    }
    bc_arena_release(&c->scratch, mark);
}

/* a block between preds and m, taking over their entries of m's phis. */
static uint32_t ladder(Cfg *c, uint32_t m, const uint32_t *preds, uint32_t count)
{
    uint32_t l = add_block(c);
    if (l == DX_NONE)
        return DX_NONE;
    branch_to(c, l, m);
    for (uint32_t i = 0; i < count; i++)
        redirect(c, preds[i], m, l);
    for (uint32_t k = 0; k < c->blocks[m].phi_count; k++) {
        CfgPhi *phi = &c->blocks[m].phis[k];
        if (count == 1) {
            for (uint32_t i = 0; i < phi->count; i++)
                if (phi->preds[i] == preds[0])
                    phi->preds[i] = l;
            continue;
        }
        uint32_t type = phi->type;
        uint32_t result = add_synth(c, type);
        CfgPhi *moved = add_phi(c, l, result, type, count);
        if (!moved)
            return DX_NONE;
        phi = &c->blocks[m].phis[k];
        for (uint32_t i = 0; i < count; i++) {
            moved->preds[moved->count] = preds[i];
            moved->values[moved->count++] = phi_value(phi, preds[i]);
            phi_remove(phi, preds[i]);
        }
        phi_add(c, phi, l, result);
    }
    return c->error[0] ? DX_NONE : l;
}

/* gives the edge from p to return block t its own copy of t. */
static void copy_exit(Cfg *c, uint32_t p, uint32_t t)
{
    uint32_t copy = add_block(c);
    if (copy == DX_NONE)
        return;
    CfgBlock *dst = &c->blocks[copy];
    dst->source = c->blocks[t].source;
    dst->copy = 1;
    dst->term = c->blocks[t].term;
    for (uint32_t k = 0; k < c->blocks[t].phi_count; k++) {
        CfgPhi *phi = &c->blocks[t].phis[k];
        CfgPhi *own = add_phi(c, copy, phi->result, phi->type, 1);
        if (!own)
            return;
        phi = &c->blocks[t].phis[k];
        own->preds[0] = p;
        own->values[0] = phi_value(phi, p);
        own->count = 1;
        phi_remove(phi, p);
    }
    redirect(c, p, t, copy);
}

static uint32_t int32_type(Cfg *c)
{
    for (uint32_t i = 0; i < c->module->type_count; i++)
        if (c->module->types[i].kind == DT_INT && c->module->types[i].width == 32)
            return i;
    fail(c, "no 32-bit integer type");
    return DX_NONE;
}

typedef struct Exit {
    uint32_t from, to;
} Exit;

/* one block all the loop's exits go to, which switches to their targets. */
static uint32_t dispatch(Cfg *c, Exit *exits, uint32_t count, const uint32_t *targets, uint32_t target_count)
{
    uint32_t i32 = int32_type(c);
    if (i32 == DX_NONE)
        return DX_NONE;
    /* a block with two exits gets a block on each, so the selector tells them apart. */
    for (uint32_t i = 0; i < count; i++) {
        uint32_t same = 0;
        for (uint32_t k = 0; k < count; k++)
            same += exits[k].from == exits[i].from;
        if (same < 2)
            continue;
        uint32_t from = exits[i].from;
        uint32_t step = ladder(c, exits[i].to, &from, 1);
        if (step == DX_NONE)
            return DX_NONE;
        exits[i].from = step;
    }
    uint32_t d = add_block(c);
    if (d == DX_NONE)
        return DX_NONE;
    uint32_t selector = add_synth(c, i32);
    CfgPhi *sel = add_phi(c, d, selector, i32, count);
    if (!sel)
        return DX_NONE;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t j = 0;
        while (targets[j] != exits[i].to)
            j++;
        sel->preds[sel->count] = exits[i].from;
        sel->values[sel->count++] = CFG_CONST + j;
    }
    for (uint32_t j = 0; j < target_count; j++) {
        uint32_t t = targets[j];
        for (uint32_t k = 0; k < c->blocks[t].phi_count; k++) {
            uint32_t type = c->blocks[t].phis[k].type;
            uint32_t result = add_synth(c, type);
            CfgPhi *carried = add_phi(c, d, result, type, count);
            if (!carried)
                return DX_NONE;
            CfgPhi *phi = &c->blocks[t].phis[k];
            for (uint32_t i = 0; i < count; i++) {
                carried->preds[carried->count] = exits[i].from;
                carried->values[carried->count++] = exits[i].to == t ? phi_value(phi, exits[i].from) : CFG_UNDEF;
            }
            for (uint32_t i = 0; i < count; i++)
                if (exits[i].to == t)
                    phi_remove(phi, exits[i].from);
            phi_add(c, phi, d, result);
        }
    }
    for (uint32_t i = 0; i < count; i++)
        redirect(c, exits[i].from, exits[i].to, d);
    uint32_t *succ = u32s(c, target_count);
    uint64_t *cases = alloc(c, target_count * sizeof(uint64_t));
    if (!succ || !cases)
        return DX_NONE;
    for (uint32_t j = 0; j < target_count; j++) {
        succ[j] = targets[j];
        if (j)
            cases[j - 1] = j;
    }
    CfgBlock *blk = &c->blocks[d];
    blk->term = TERM_SWITCH;
    blk->cond = selector;
    blk->succ = succ;
    blk->succ_count = target_count;
    blk->cases = cases;
    return d;
}

/* ------------------------------------------------------------------ loops */

/* the blocks that reach h's back edges without passing h. */
static uint32_t loop_body(Cfg *c, uint32_t h, uint8_t *in_body, uint32_t *list)
{
    __builtin_memset(in_body, 0, c->count);
    uint32_t n = 0;
    in_body[h] = 1;
    list[n++] = h;
    const CfgBlock *hb = &c->blocks[h];
    for (uint32_t i = 0; i < hb->pred_count; i++) {
        uint32_t p = hb->pred[i];
        if (cfg_dominates(c, h, p) && !in_body[p]) {
            in_body[p] = 1;
            list[n++] = p;
        }
    }
    for (uint32_t at = 1; at < n; at++) {
        const CfgBlock *b = &c->blocks[list[at]];
        for (uint32_t i = 0; i < b->pred_count; i++) {
            uint32_t p = b->pred[i];
            if (!in_body[p] && c->blocks[p].idom != DX_NONE) {
                in_body[p] = 1;
                list[n++] = p;
            }
        }
    }
    return n;
}

static int is_loop_head(const Cfg *c, uint32_t h)
{
    const CfgBlock *hb = &c->blocks[h];
    for (uint32_t i = 0; i < hb->pred_count; i++)
        if (cfg_dominates(c, h, hb->pred[i]))
            return 1;
    return 0;
}

/* takes over the edges to h and its phis, so h's code may branch and the header holds nothing else. */
static uint32_t hoist(Cfg *c, uint32_t h)
{
    uint32_t top = add_block(c);
    if (top == DX_NONE)
        return DX_NONE;
    CfgBlock *hb = &c->blocks[h], *tb = &c->blocks[top];
    tb->phis = hb->phis;
    tb->phi_count = hb->phi_count;
    hb->phis = nullptr;
    hb->phi_count = 0;
    for (uint32_t i = 0; i < hb->pred_count; i++)
        redirect(c, hb->pred[i], h, top);
    branch_to(c, top, h);
    return top;
}

static int canonicalize_loop(Cfg *c, uint32_t h)
{
    if (c->blocks[h].source != DX_NONE || c->blocks[h].term != TERM_BRANCH) {
        h = hoist(c, h);
        if (h == DX_NONE)
            return -1;
        analyze(c);
    }
    BcMark mark = bc_arena_mark(&c->scratch);
    uint8_t *in_body = tmp(c, c->count);
    uint32_t *list = tmp32(c, c->count), *back = tmp32(c, c->count);
    if (!in_body || !list || !back)
        return -1;
    loop_body(c, h, in_body, list);
    /* the continue block takes the back edges. */
    uint32_t back_count = 0;
    const CfgBlock *hb = &c->blocks[h];
    for (uint32_t i = 0; i < hb->pred_count; i++)
        if (in_body[hb->pred[i]])
            back[back_count++] = hb->pred[i];
    uint32_t cont = ladder(c, h, back, back_count);
    if (cont == DX_NONE)
        return -1;
    uint32_t body_limit = cont;
    analyze(c);

    /* exits: edges out of the body other than returns in place. */
    uint32_t merge = DX_NONE;
    for (int round = 0; merge == DX_NONE; round++) {
        BcMark inner = bc_arena_mark(&c->scratch);
        uint32_t edges = 0;
        for (uint32_t b = 0; b < body_limit; b++)
            if (in_body[b] && live(c, b))
                edges += c->blocks[b].succ_count;
        Exit *exits = tmp(c, (edges + 1) * sizeof(Exit));
        uint32_t *targets = tmp32(c, edges + 1);
        if (!exits || !targets)
            return -1;
        uint32_t count = 0, target_count = 0;
        int returns = 0;
        for (uint32_t b = 0; b < body_limit; b++) {
            if (!in_body[b] || !live(c, b))
                continue;
            const CfgBlock *blk = &c->blocks[b];
            for (uint32_t i = 0; i < blk->succ_count; i++) {
                uint32_t t = blk->succ[i];
                if ((t < body_limit && in_body[t]) || t == cont || terminal(c, b, t))
                    continue;
                int known = 0;
                for (uint32_t k = 0; k < count; k++)
                    known |= exits[k].from == b && exits[k].to == t;
                if (known)
                    continue;
                exits[count++] = (Exit){b, t};
                returns |= is_exit_block(&c->blocks[t]);
                uint32_t j = 0;
                while (j < target_count && targets[j] != t)
                    j++;
                if (j == target_count)
                    targets[target_count++] = t;
            }
        }
        if (target_count > 1 && returns && round == 0) {
            /* returns out of the loop return in place, leaving the other exits. */
            for (uint32_t k = 0; k < count; k++)
                if (is_exit_block(&c->blocks[exits[k].to]))
                    copy_exit(c, exits[k].from, exits[k].to);
            bc_arena_release(&c->scratch, inner);
            analyze(c);
            continue;
        }
        if (!target_count) {
            merge = add_block(c);
            if (merge != DX_NONE)
                c->blocks[merge].term = TERM_UNREACHABLE;
        } else if (target_count == 1) {
            uint32_t t = targets[0];
            const CfgBlock *tb = &c->blocks[t];
            int only_exits = 1;
            for (uint32_t i = 0; i < tb->pred_count; i++)
                only_exits &= tb->pred[i] < body_limit && in_body[tb->pred[i]];
            if (only_exits) {
                merge = t;
            } else {
                uint32_t *preds = tmp32(c, count);
                if (!preds)
                    return -1;
                for (uint32_t k = 0; k < count; k++)
                    preds[k] = exits[k].from;
                merge = ladder(c, t, preds, count);
            }
        } else {
            merge = dispatch(c, exits, count, targets, target_count);
        }
        bc_arena_release(&c->scratch, inner);
        if (merge == DX_NONE)
            return -1;
    }
    CfgBlock *head = &c->blocks[h];
    head->header = HEADER_LOOP;
    head->merge = merge;
    head->cont = cont;

    bc_arena_release(&c->scratch, mark);
    analyze(c);
    return c->error[0] ? -1 : 0;
}

/* innermost loops first, as their body is the smallest. */
static int structure_loops(Cfg *c)
{
    for (uint32_t round = 0; round < 100000; round++) {
        analyze(c);
        BcMark mark = bc_arena_mark(&c->scratch);
        uint8_t *in_body = tmp(c, c->count);
        uint32_t *list = tmp32(c, c->count);
        if (!in_body || !list)
            return -1;
        uint32_t best = DX_NONE, best_size = DX_NONE;
        for (uint32_t h = 0; h < c->count; h++) {
            if (!live(c, h) || c->blocks[h].header == HEADER_LOOP || orphan(c, h) || !is_loop_head(c, h))
                continue;
            uint32_t size = loop_body(c, h, in_body, list);
            for (uint32_t i = 1; i < size; i++)
                if (!cfg_dominates(c, h, list[i]))
                    return fail(c, "irreducible control flow");
            if (size < best_size) {
                best = h;
                best_size = size;
            }
        }
        bc_arena_release(&c->scratch, mark);
        if (best == DX_NONE)
            return 0;
        if (canonicalize_loop(c, best))
            return -1;
    }
    return fail(c, "too many loops");
}

/* each block's innermost loop, and each loop's parent: the smallest other loop holding it. */
static void assign_loops(Cfg *c)
{
    BcMark mark = bc_arena_mark(&c->scratch);
    uint32_t n = c->count;
    uint32_t *headers = tmp32(c, n), *list = tmp32(c, n);
    if (!headers || !list)
        return;
    uint32_t count = 0;
    for (uint32_t b = 0; b < n; b++) {
        CfgBlock *blk = &c->blocks[b];
        blk->loop = blk->parent = DX_NONE;
        blk->body_size = DX_NONE;
        if (live(c, b) && blk->header == HEADER_LOOP && !orphan(c, b))
            headers[count++] = b;
    }
    uint8_t **bodies = tmp(c, (count ? count : 1) * sizeof(uint8_t *));
    if (!bodies)
        return;
    for (uint32_t i = 0; i < count; i++) {
        bodies[i] = tmp(c, n);
        if (!bodies[i])
            return;
        c->blocks[headers[i]].body_size = loop_body(c, headers[i], bodies[i], list);
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t h = headers[i], size = c->blocks[h].body_size;
        for (uint32_t b = 0; b < n; b++) {
            if (!bodies[i][b])
                continue;
            uint32_t cur = c->blocks[b].loop;
            if (cur == DX_NONE || size < c->blocks[cur].body_size)
                c->blocks[b].loop = h;
        }
        for (uint32_t k = 0; k < count; k++) {
            uint32_t g = headers[k];
            if (g == h || !bodies[k][h] || c->blocks[g].body_size <= size)
                continue;
            uint32_t cur = c->blocks[h].parent;
            if (cur == DX_NONE || c->blocks[g].body_size < c->blocks[cur].body_size)
                c->blocks[h].parent = g;
        }
    }
    bc_arena_release(&c->scratch, mark);
}

static int in_loop(const Cfg *c, uint32_t b, uint32_t h)
{
    for (uint32_t x = c->blocks[b].loop; x != DX_NONE; x = c->blocks[x].parent)
        if (x == h)
            return 1;
    return 0;
}

/* the loop owning a block's selections. a header's belong to its parent's body. */
static uint32_t region_of(const Cfg *c, uint32_t b)
{
    const CfgBlock *blk = &c->blocks[b];
    return blk->header == HEADER_LOOP ? blk->parent : blk->loop;
}

/* ------------------------------------------------------------------ selections */

/* the same iteration, on a graph in compressed rows. */
static void graph_dominators(Cfg *c, uint32_t n, const uint32_t *start, const uint32_t *edges, uint32_t entry,
                             uint32_t *idom)
{
    BcMark mark = bc_arena_mark(&c->scratch);
    uint32_t *order = tmp32(c, n), *index = tmp32(c, n), *stack = tmp32(c, n), *next = tmp32(c, n);
    uint32_t *pstart = tmp32(c, n + 1), *pedges = tmp32(c, start[n] + 1), *fill = tmp32(c, n);
    uint8_t *seen = tmp(c, n);
    if (!order || !index || !stack || !next || !pstart || !pedges || !fill || !seen)
        return;
    for (uint32_t v = 0; v < n; v++) {
        idom[v] = DX_NONE;
        index[v] = DX_NONE;
    }
    for (uint32_t e = 0; e < start[n]; e++)
        pstart[edges[e] + 1]++;
    for (uint32_t v = 0; v < n; v++)
        pstart[v + 1] += pstart[v];
    for (uint32_t v = 0; v < n; v++)
        for (uint32_t e = start[v]; e < start[v + 1]; e++)
            pedges[pstart[edges[e]] + fill[edges[e]]++] = v;
    uint32_t top = 0, post = n;
    stack[top++] = entry;
    seen[entry] = 1;
    while (top) {
        uint32_t v = stack[top - 1];
        if (start[v] + next[v] < start[v + 1]) {
            uint32_t s = edges[start[v] + next[v]++];
            if (!seen[s]) {
                seen[s] = 1;
                stack[top++] = s;
            }
            continue;
        }
        order[--post] = v;
        top--;
    }
    uint32_t count = n - post;
    for (uint32_t i = 0; i < count; i++)
        index[order[post + i]] = i;
    idom[entry] = entry;
    for (int changed = 1; changed;) {
        changed = 0;
        for (uint32_t i = 1; i < count; i++) {
            uint32_t v = order[post + i], best = DX_NONE;
            for (uint32_t k = pstart[v]; k < pstart[v + 1]; k++) {
                uint32_t p = pedges[k];
                if (index[p] == DX_NONE || idom[p] == DX_NONE)
                    continue;
                if (best == DX_NONE) {
                    best = p;
                    continue;
                }
                uint32_t x = p, y = best;
                while (x != y) {
                    while (index[x] > index[y])
                        x = idom[x];
                    while (index[y] > index[x])
                        y = idom[y];
                }
                best = x;
            }
            if (best != DX_NONE && idom[v] != best) {
                idom[v] = best;
                changed = 1;
            }
        }
    }
    bc_arena_release(&c->scratch, mark);
}

/* whether an edge leaves the region of loop r: a break, or to an outer loop's merge or continue. */
static int leaves_region(const Cfg *c, uint32_t r, uint32_t to)
{
    for (uint32_t x = r; x != DX_NONE; x = c->blocks[x].parent) {
        const CfgBlock *h = &c->blocks[x];
        if (to == h->merge || (to == h->cont && x != r) || to == x)
            return 1;
    }
    return 0;
}

/*
 * finds the selection merges of a region: the top level when r is DX_NONE,
 * or a loop body. post-dominators come from the reversed region graph,
 * with inner loops as one node going to their merge and an exit node after
 * the blocks that go nowhere and after the continue block. a block that can
 * reach the continue block merges where its paths that do meet, leaving its
 * breaks and returns aside.
 */
static int region_merges(Cfg *c, uint32_t r)
{
    BcMark mark = bc_arena_mark(&c->scratch);
    uint32_t n = c->count;
    uint32_t *local = tmp32(c, n), *nodes = tmp32(c, n);
    if (!local || !nodes)
        return -1;
    uint32_t count = 0, edge_room = 0;
    for (uint32_t b = 0; b < n; b++) {
        local[b] = DX_NONE;
        if (live(c, b) && !orphan(c, b) && region_of(c, b) == r && b != r) {
            local[b] = count;
            nodes[count++] = b;
            edge_room += c->blocks[b].succ_count + 1;
        }
    }
    if (!count) {
        bc_arena_release(&c->scratch, mark);
        return 0;
    }
    uint32_t cont = r != DX_NONE ? c->blocks[r].cont : DX_NONE;
    /* each node's successors in the region, in compressed rows. */
    uint32_t *start = tmp32(c, count + 1), *edges = tmp32(c, edge_room);
    if (!start || !edges)
        return -1;
    uint32_t at = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t b = nodes[i];
        const CfgBlock *blk = &c->blocks[b];
        start[i] = at;
        if (blk->header == HEADER_LOOP) {
            if (local[blk->merge] != DX_NONE)
                edges[at++] = local[blk->merge];
            continue;
        }
        if (b == cont)
            continue;
        for (uint32_t k = 0; k < blk->succ_count; k++) {
            uint32_t t = blk->succ[k];
            if (terminal(c, b, t) || (t != cont && leaves_region(c, r, t)) || local[t] == DX_NONE)
                continue;
            int known = 0;
            for (uint32_t j = start[i]; j < at; j++)
                known |= edges[j] == local[t];
            if (!known)
                edges[at++] = local[t];
        }
    }
    start[count] = at;
    /* blocks that reach the continue block. */
    uint8_t *alive = tmp(c, count);
    if (!alive)
        return -1;
    if (cont != DX_NONE && local[cont] != DX_NONE) {
        alive[local[cont]] = 1;
        for (int changed = 1; changed;) {
            changed = 0;
            for (uint32_t i = 0; i < count; i++) {
                if (alive[i])
                    continue;
                for (uint32_t j = start[i]; j < start[i + 1]; j++)
                    if (alive[edges[j]]) {
                        alive[i] = 1;
                        changed = 1;
                        break;
                    }
            }
        }
    }
    /*
     * two reversed graphs with node count as the exit: one of every node,
     * one of the nodes that reach the continue block.
     */
    uint32_t *ipdom[2];
    for (int pass = 0; pass < 2; pass++) {
        uint32_t *rstart = tmp32(c, count + 2), *redges = tmp32(c, at + count + 1), *fill = tmp32(c, count + 1);
        ipdom[pass] = tmp32(c, count + 1);
        if (!rstart || !redges || !fill || !ipdom[pass])
            return -1;
        for (uint32_t i = 0; i < count; i++) {
            if (pass && !alive[i])
                continue;
            uint32_t kept = 0;
            for (uint32_t j = start[i]; j < start[i + 1]; j++)
                if (!pass || alive[edges[j]]) {
                    rstart[edges[j] + 1]++;
                    kept++;
                }
            if (!kept)
                rstart[count + 1]++;
        }
        for (uint32_t v = 0; v <= count; v++)
            rstart[v + 1] += rstart[v];
        for (uint32_t i = 0; i < count; i++) {
            if (pass && !alive[i])
                continue;
            uint32_t kept = 0;
            for (uint32_t j = start[i]; j < start[i + 1]; j++)
                if (!pass || alive[edges[j]]) {
                    uint32_t s = edges[j];
                    redges[rstart[s] + fill[s]++] = i;
                    kept++;
                }
            if (!kept)
                redges[rstart[count] + fill[count]++] = i;
        }
        graph_dominators(c, count + 1, rstart, redges, count, ipdom[pass]);
    }
    uint32_t *chosen = tmp32(c, count);
    if (!chosen)
        return -1;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t pd = alive[i] ? ipdom[1][i] : ipdom[0][i];
        chosen[i] = pd == DX_NONE || pd >= count ? DX_NONE : nodes[pd];
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t b = nodes[i];
        const CfgBlock *blk = &c->blocks[b];
        if (blk->header == HEADER_LOOP || (blk->term != TERM_COND && blk->term != TERM_SWITCH))
            continue;
        uint32_t merge = chosen[i];
        if (merge == DX_NONE) {
            merge = add_block(c);
            if (merge == DX_NONE)
                return -1;
            c->blocks[merge].term = TERM_UNREACHABLE;
        }
        c->blocks[b].header = HEADER_SELECTION;
        c->blocks[b].merge = merge;
    }
    bc_arena_release(&c->scratch, mark);
    return c->error[0] ? -1 : 0;
}

/* a selection graph edge: not a return in place, break or continue. */
static int region_edge(const Cfg *c, uint32_t r, uint32_t u, uint32_t t)
{
    if (terminal(c, u, t) || c->blocks[u].header == HEADER_LOOP || region_of(c, u) != r)
        return 0;
    if (r != DX_NONE && t == c->blocks[r].cont)
        return 0;
    return !leaves_region(c, r, t);
}

/*
 * a selection whose blocks also reach blocks it does not dominate, other
 * than its merge, jumps into a sibling construct. the edges into those
 * blocks, and into the merge from the blocks around them, then all go
 * through one block that switches between them. returns 1 when it changed
 * the graph.
 */
static int funnel(Cfg *c)
{
    BcMark mark = bc_arena_mark(&c->scratch);
    uint32_t n = c->count;
    uint32_t *targets = tmp32(c, n + 1);
    if (!targets)
        return -1;
    for (uint32_t h = 0; h < n; h++) {
        const CfgBlock *hb = &c->blocks[h];
        if (!live(c, h) || orphan(c, h) || hb->header != HEADER_SELECTION)
            continue;
        uint32_t r = region_of(c, h), m = hb->merge, count = 0;
        int merge_known = orphan(c, m);
        for (uint32_t u = 0; u < n; u++) {
            if (!live(c, u) || orphan(c, u) || !cfg_dominates(c, h, u) ||
                (!merge_known && cfg_dominates(c, m, u)))
                continue;
            const CfgBlock *ub = &c->blocks[u];
            for (uint32_t i = 0; i < ub->succ_count; i++) {
                uint32_t t = ub->succ[i];
                if (t == m || cfg_dominates(c, h, t) || is_exit_block(&c->blocks[t]) || !region_edge(c, r, u, t))
                    continue;
                int known = 0;
                for (uint32_t k = 0; k < count; k++)
                    known |= targets[k] == t;
                if (!known)
                    targets[count++] = t;
            }
        }
        if (!count)
            continue;
        if (!merge_known)
            targets[count++] = m;
        /* the nearest block above all the targets. */
        uint32_t d = targets[0];
        for (uint32_t k = 1; k < count; k++)
            while (!cfg_dominates(c, d, targets[k]))
                d = c->blocks[d].idom;
        for (uint32_t k = 0; k < count; k++)
            if (d == targets[k])
                d = c->blocks[d].idom;
        uint32_t edges = 0;
        for (uint32_t u = 0; u < n; u++)
            edges += c->blocks[u].succ_count;
        Exit *exits = tmp(c, (edges + 1) * sizeof(Exit));
        if (!exits)
            return -1;
        uint32_t exit_count = 0;
        for (uint32_t u = 0; u < n; u++) {
            if (!live(c, u) || orphan(c, u) || !cfg_dominates(c, d, u))
                continue;
            int below = 0;
            for (uint32_t k = 0; k < count; k++)
                below |= cfg_dominates(c, targets[k], u);
            if (below)
                continue;
            const CfgBlock *ub = &c->blocks[u];
            for (uint32_t i = 0; i < ub->succ_count; i++) {
                uint32_t t = ub->succ[i];
                int target = 0;
                for (uint32_t k = 0; k < count; k++)
                    target |= targets[k] == t;
                int known = 0;
                for (uint32_t k = 0; k < exit_count; k++)
                    known |= exits[k].from == u && exits[k].to == t;
                if (target && !known && region_edge(c, r, u, t))
                    exits[exit_count++] = (Exit){u, t};
            }
        }
        uint32_t x = dispatch(c, exits, exit_count, targets, count);
        bc_arena_release(&c->scratch, mark);
        return x == DX_NONE ? -1 : 1;
    }
    bc_arena_release(&c->scratch, mark);
    return 0;
}

static int in_construct(const Cfg *c, uint32_t x, uint32_t n)
{
    const CfgBlock *h = &c->blocks[x];
    if (h->header == HEADER_LOOP)
        return in_loop(c, n, x);
    if (h->header != HEADER_SELECTION || !cfg_dominates(c, x, n))
        return 0;
    /* a selection in a loop's body ends where the loop does. */
    uint32_t r = region_of(c, x);
    if (r != DX_NONE && !in_loop(c, n, r))
        return 0;
    return orphan(c, h->merge) || !cfg_dominates(c, h->merge, n);
}

/* the innermost construct holding n whose header is from or dominates it. */
static uint32_t enclosing(const Cfg *c, uint32_t n, uint32_t from)
{
    for (uint32_t x = from;; x = c->blocks[x].idom) {
        if (c->blocks[x].header != HEADER_NONE && in_construct(c, x, n))
            return x;
        if (x == c->entry)
            return DX_NONE;
    }
}

/* gives each header whose merge other headers also reach its own merge block, outer headers first. */
static int ladders(Cfg *c)
{
    BcMark mark = bc_arena_mark(&c->scratch);
    uint32_t n = c->count;
    uint32_t *headers = tmp32(c, n);
    if (!headers)
        return -1;
    uint32_t count = 0;
    for (uint32_t b = 0; b < n; b++)
        if (live(c, b) && c->blocks[b].header == HEADER_SELECTION && !orphan(c, b))
            headers[count++] = b;
    for (uint32_t i = 1; i < count; i++)
        for (uint32_t k = i; k > 0 && c->blocks[headers[k - 1]].depth > c->blocks[headers[k]].depth; k--) {
            uint32_t t = headers[k];
            headers[k] = headers[k - 1];
            headers[k - 1] = t;
        }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t h = headers[i];
        uint32_t m = c->blocks[h].merge;
        if (orphan(c, m))
            continue;
        BcMark inner = bc_arena_mark(&c->scratch);
        const CfgBlock *mb = &c->blocks[m];
        uint32_t *inside = tmp32(c, mb->pred_count + 1);
        if (!inside)
            return -1;
        uint32_t inside_count = 0;
        int outside = 0;
        for (uint32_t k = 0; k < mb->pred_count; k++) {
            uint32_t p = mb->pred[k];
            if (mb->header == HEADER_LOOP && p == mb->cont)
                continue;
            if (cfg_dominates(c, h, p) && !cfg_dominates(c, m, p))
                inside[inside_count++] = p;
            else
                outside = 1;
        }
        int is_continue = 0;
        for (uint32_t x = c->blocks[h].loop; x != DX_NONE; x = c->blocks[x].parent)
            is_continue |= c->blocks[x].cont == m;
        if ((outside || is_continue) && inside_count) {
            uint32_t l = ladder(c, m, inside, inside_count);
            if (l == DX_NONE)
                return -1;
            for (uint32_t k = i; k < count; k++) {
                uint32_t g = headers[k];
                if (c->blocks[g].merge == m && (g == h || cfg_dominates(c, h, g)))
                    c->blocks[g].merge = l;
            }
            bc_arena_release(&c->scratch, inner);
            analyze(c);
            assign_loops(c);
            continue;
        }
        bc_arena_release(&c->scratch, inner);
    }
    bc_arena_release(&c->scratch, mark);
    return c->error[0] ? -1 : 0;
}

/* spir-v allows edges inside their construct, to its merge, or breaks and continues. */
static int edge_valid(const Cfg *c, uint32_t n, uint32_t t)
{
    uint32_t s = enclosing(c, n, n);
    if (s == DX_NONE)
        return 1;
    const CfgBlock *sb = &c->blocks[s];
    if (t == sb->merge || in_construct(c, s, t))
        return 1;
    /* breaks out of switches up to the innermost loop, and the loop's own. */
    for (uint32_t x = s; x != DX_NONE;) {
        const CfgBlock *xb = &c->blocks[x];
        if (xb->header == HEADER_LOOP)
            return t == xb->merge || t == xb->cont || (n == xb->cont && t == x);
        if (xb->term == TERM_SWITCH && t == xb->merge)
            return 1;
        if (x == c->entry)
            break;
        x = enclosing(c, n, c->blocks[x].idom);
    }
    return 0;
}

/* 1 when every edge is valid, otherwise 0 with the first bad edge in from and to. */
static int valid(const Cfg *c, uint32_t *from, uint32_t *to)
{
    for (uint32_t b = 0; b < c->count; b++) {
        const CfgBlock *blk = &c->blocks[b];
        if (!live(c, b) || orphan(c, b))
            continue;
        for (uint32_t i = 0; i < blk->succ_count; i++) {
            uint32_t t = blk->succ[i];
            if (!terminal(c, b, t) && !edge_valid(c, b, t)) {
                *from = b;
                *to = t;
                return 0;
            }
        }
    }
    return 1;
}

static char *put_number(char *at, uint32_t v)
{
    char digits[10];
    int n = 0;
    do
        digits[n++] = (char)('0' + v % 10);
    while (v /= 10);
    while (n)
        *at++ = digits[--n];
    return at;
}

/* gives every branch edge to a shared return block its own copy. */
static int copy_returns(Cfg *c)
{
    int copied = 0;
    uint32_t n = c->count;
    for (uint32_t b = 0; b < n; b++) {
        if (!live(c, b) || orphan(c, b) || (c->blocks[b].term != TERM_COND && c->blocks[b].term != TERM_SWITCH))
            continue;
        for (uint32_t i = 0; i < c->blocks[b].succ_count; i++) {
            uint32_t t = c->blocks[b].succ[i];
            if (is_exit_block(&c->blocks[t]) && !terminal(c, b, t)) {
                copy_exit(c, b, t);
                copied = 1;
                analyze(c);
            }
        }
    }
    return copied;
}

static int structure_selections(Cfg *c)
{
    uint32_t from = DX_NONE, to = DX_NONE;
    for (int attempt = 0, funnels = 0; attempt < 2; attempt++) {
        analyze(c);
        assign_loops(c);
        for (uint32_t b = 0; b < c->count; b++)
            if (c->blocks[b].header == HEADER_SELECTION) {
                c->blocks[b].header = HEADER_NONE;
                c->blocks[b].merge = DX_NONE;
            }
        uint32_t regions = c->count;
        if (region_merges(c, DX_NONE))
            return -1;
        for (uint32_t h = 0; h < regions; h++)
            if (live(c, h) && c->blocks[h].header == HEADER_LOOP && !orphan(c, h) && region_merges(c, h))
                return -1;
        analyze(c);
        assign_loops(c);
        int changed = funnel(c);
        if (changed < 0)
            return -1;
        if (changed) {
            if (++funnels > 10000)
                return fail(c, "too many funnels");
            attempt--;
            continue;
        }
        if (ladders(c))
            return -1;
        if (valid(c, &from, &to))
            return 0;
        if (!attempt && copy_returns(c))
            continue;
        break;
    }
    char why[64] = "unstructured control flow from block ", *at = why + 37;
    at = put_number(at, from);
    for (const char *s = " to "; *s; s++)
        *at++ = *s;
    *put_number(at, to) = 0;
    return fail(c, why);
}

static int make_order(Cfg *c)
{
    analyze(c);
    c->order = u32s(c, c->count);
    if (!c->order)
        return -1;
    c->order_count = reverse_post_order(c, c->order);
    /* merges nothing reaches go last. */
    for (uint32_t b = 0; b < c->count; b++)
        if (live(c, b) && orphan(c, b))
            c->order[c->order_count++] = b;
    return 0;
}

int cfg_build(Cfg *c, const DxModule *m, const DxFunction *f, BcArena *arena)
{
    __builtin_memset(c, 0, sizeof(*c));
    c->arena = arena;
    c->scratch.alloc = arena->alloc;
    c->module = m;
    c->function = f;
    int failed = build_initial(c);
    if (!failed) {
        remove_dead(c);
        analyze(c);
        failed = structure_loops(c) || structure_selections(c) || make_order(c);
    }
    bc_arena_free(&c->scratch);
    return failed || c->error[0] ? -1 : 0;
}
