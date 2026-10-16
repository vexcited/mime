/*
 * reads the llvm 3.7 module of a dxil part: the type table, globals and
 * functions, constants, metadata and function bodies, numbering values as
 * the bitcode writer did.
 */
#include "ir.h"

enum {
    BLOCK_MODULE = 8,
    BLOCK_PARAMATTR = 9,
    BLOCK_PARAMATTR_GROUP = 10,
    BLOCK_CONSTANTS = 11,
    BLOCK_FUNCTION = 12,
    BLOCK_VALUE_SYMTAB = 14,
    BLOCK_METADATA = 15,
    BLOCK_METADATA_ATTACHMENT = 16,
    BLOCK_TYPE = 17,
};

enum { MODULE_GLOBALVAR = 7, MODULE_FUNCTION = 8 };

enum {
    TYPE_NUMENTRY = 1,
    TYPE_VOID = 2,
    TYPE_FLOAT = 3,
    TYPE_DOUBLE = 4,
    TYPE_LABEL = 5,
    TYPE_OPAQUE = 6,
    TYPE_INTEGER = 7,
    TYPE_POINTER = 8,
    TYPE_FUNCTION_OLD = 9,
    TYPE_HALF = 10,
    TYPE_ARRAY = 11,
    TYPE_VECTOR = 12,
    TYPE_METADATA = 16,
    TYPE_STRUCT_ANON = 18,
    TYPE_STRUCT_NAME = 19,
    TYPE_STRUCT_NAMED = 20,
    TYPE_FUNCTION = 21,
};

enum {
    CST_SETTYPE = 1,
    CST_NULL = 2,
    CST_UNDEF = 3,
    CST_INTEGER = 4,
    CST_WIDE_INTEGER = 5,
    CST_FLOAT = 6,
    CST_AGGREGATE = 7,
    CST_STRING = 8,
    CST_CSTRING = 9,
    CST_CE_BINOP = 10,
    CST_CE_CAST = 11,
    CST_CE_GEP = 12,
    CST_CE_INBOUNDS_GEP = 20,
    CST_DATA = 22,
};

enum {
    MD_STRING_OLD = 1,
    MD_VALUE = 2,
    MD_NODE = 3,
    MD_NAME = 4,
    MD_DISTINCT_NODE = 5,
    MD_KIND = 6,
    MD_NAMED_NODE = 10,
    MD_ATTACHMENT = 11,
};

enum { VST_ENTRY = 1 };

enum {
    FUNC_DECLAREBLOCKS = 1,
    FUNC_BINOP = 2,
    FUNC_CAST = 3,
    FUNC_GEP_OLD = 4,
    FUNC_SELECT = 5,
    FUNC_EXTRACTELT = 6,
    FUNC_INSERTELT = 7,
    FUNC_SHUFFLEVEC = 8,
    FUNC_CMP = 9,
    FUNC_RET = 10,
    FUNC_BR = 11,
    FUNC_SWITCH = 12,
    FUNC_UNREACHABLE = 15,
    FUNC_PHI = 16,
    FUNC_ALLOCA = 19,
    FUNC_LOAD = 20,
    FUNC_EXTRACTVAL = 26,
    FUNC_INSERTVAL = 27,
    FUNC_CMP2 = 28,
    FUNC_VSELECT = 29,
    FUNC_INBOUNDS_GEP_OLD = 30,
    FUNC_DEBUG_LOC_AGAIN = 33,
    FUNC_CALL = 34,
    FUNC_DEBUG_LOC = 35,
    FUNC_ATOMICRMW = 38,
    FUNC_GEP = 43,
    FUNC_STORE = 44,
    FUNC_CMPXCHG = 46,
};

typedef struct Parser {
    DxModule *m;
    int failed;
    /* the function being read. */
    DxFunction *f;
    uint32_t value_capacity;
    uint32_t inst_capacity;
    uint32_t module_capacity;
} Parser;

static int fail(Parser *p, const char *why)
{
    if (!p->failed) {
        p->failed = 1;
        uint32_t i = 0;
        for (; why[i] && i + 1 < sizeof(p->m->error); i++)
            p->m->error[i] = why[i];
        p->m->error[i] = 0;
    }
    return -1;
}

static void *alloc(Parser *p, size_t size)
{
    void *r = bc_arena_alloc(&p->m->arena, size ? size : 1);
    if (!r)
        fail(p, "out of memory");
    return r;
}

static void *grow(Parser *p, void *items, uint32_t count, uint32_t *capacity, size_t size)
{
    if (count < *capacity)
        return items;
    uint32_t want = *capacity ? *capacity * 2 : 16;
    void *n = alloc(p, want * size);
    if (!n)
        return NULL;
    if (items)
        __builtin_memcpy(n, items, count * size);
    *capacity = want;
    return n;
}

static const char *record_string(Parser *p, const BcRecord *r, uint32_t first)
{
    uint32_t n = r->count > first ? r->count - first : 0;
    char *s = alloc(p, n + 1);
    if (!s)
        return "";
    for (uint32_t i = 0; i < n; i++)
        s[i] = (char)r->ops[first + i];
    s[n] = 0;
    return s;
}

static int64_t signed_vbr(uint64_t v)
{
    if (!(v & 1))
        return (int64_t)(v >> 1);
    if (v != 1)
        return -(int64_t)(v >> 1);
    return INT64_MIN;
}

/* ------------------------------------------------------------------ types */

static DxType *type_at(Parser *p, uint32_t id)
{
    if (id >= p->m->type_count) {
        fail(p, "bad type id");
        return NULL;
    }
    return &p->m->types[id];
}

static uint32_t add_type(Parser *p, DxType t)
{
    DxModule *m = p->m;
    DxType *types = grow(p, m->types, m->type_count, &m->type_capacity, sizeof(DxType));
    if (!types)
        return 0;
    m->types = types;
    m->types[m->type_count] = t;
    return m->type_count++;
}

static uint32_t find_type(Parser *p, DxType t)
{
    for (uint32_t i = 0; i < p->m->type_count; i++) {
        const DxType *o = &p->m->types[i];
        if (o->kind == t.kind && o->width == t.width && o->element == t.element && o->count == t.count &&
            !o->name && !o->members && !t.members)
            return i;
    }
    return add_type(p, t);
}

uint32_t dx_pointer_type(DxModule *m, uint32_t element, uint32_t space)
{
    Parser p = {m, 0, NULL, 0, 0, 0};
    return find_type(&p, (DxType){DT_POINTER, space, element, 0, NULL, NULL});
}

static uint32_t int_type(Parser *p, uint32_t width) { return find_type(p, (DxType){DT_INT, width, 0, 0, NULL, NULL}); }

static void read_types(Parser *p, const BcBlock *b)
{
    const char *pending_name = NULL;
    for (uint32_t i = 0; i < b->count && !p->failed; i++) {
        const BcRecord *r = b->entries[i].record;
        if (!r)
            continue;
        DxType t = {0};
        t.element = DX_NONE;
        switch (r->code) {
        case TYPE_NUMENTRY: continue;
        case TYPE_VOID: t.kind = DT_VOID; break;
        case TYPE_HALF: t.kind = DT_HALF; break;
        case TYPE_FLOAT: t.kind = DT_FLOAT; break;
        case TYPE_DOUBLE: t.kind = DT_DOUBLE; break;
        case TYPE_LABEL: t.kind = DT_LABEL; break;
        case TYPE_METADATA: t.kind = DT_METADATA; break;
        case TYPE_OPAQUE:
            t.kind = DT_OPAQUE;
            t.name = pending_name;
            pending_name = NULL;
            break;
        case TYPE_INTEGER:
            if (r->count < 1)
                goto bad;
            t.kind = DT_INT;
            t.width = (uint32_t)r->ops[0];
            break;
        case TYPE_POINTER:
            if (r->count < 1)
                goto bad;
            t.kind = DT_POINTER;
            t.element = (uint32_t)r->ops[0];
            t.width = r->count > 1 ? (uint32_t)r->ops[1] : 0;
            break;
        case TYPE_ARRAY:
        case TYPE_VECTOR:
            if (r->count < 2)
                goto bad;
            t.kind = r->code == TYPE_ARRAY ? DT_ARRAY : DT_VECTOR;
            t.count = (uint32_t)r->ops[0];
            t.element = (uint32_t)r->ops[1];
            break;
        case TYPE_STRUCT_ANON:
        case TYPE_STRUCT_NAMED:
            if (r->count < 1)
                goto bad;
            t.kind = DT_STRUCT;
            t.count = r->count - 1;
            t.members = alloc(p, t.count * sizeof(uint32_t));
            for (uint32_t k = 0; k < t.count && t.members; k++)
                t.members[k] = (uint32_t)r->ops[1 + k];
            if (r->code == TYPE_STRUCT_NAMED) {
                t.name = pending_name;
                pending_name = NULL;
            }
            break;
        case TYPE_STRUCT_NAME: pending_name = record_string(p, r, 0); continue;
        case TYPE_FUNCTION:
        case TYPE_FUNCTION_OLD: {
            uint32_t first = r->code == TYPE_FUNCTION ? 1 : 2;
            if (r->count < first + 1)
                goto bad;
            t.kind = DT_FUNCTION;
            t.width = (uint32_t)r->ops[0];
            t.element = (uint32_t)r->ops[first];
            t.count = r->count - first - 1;
            t.members = alloc(p, t.count * sizeof(uint32_t));
            for (uint32_t k = 0; k < t.count && t.members; k++)
                t.members[k] = (uint32_t)r->ops[first + 1 + k];
            break;
        }
        default: fail(p, "unknown type"); return;
        }
        add_type(p, t);
        continue;
    bad:
        fail(p, "malformed type");
    }
}

/* ------------------------------------------------------------------ values */

/* the values being numbered: the module's, or the function's past them. */
static DxValue *new_value(Parser *p, uint32_t *id)
{
    DxModule *m = p->m;
    if (p->f) {
        DxFunction *f = p->f;
        DxValue *values = grow(p, f->values, f->value_count, &p->value_capacity, sizeof(DxValue));
        if (!values)
            return NULL;
        f->values = values;
        *id = m->value_count + f->value_count;
        DxValue *v = &f->values[f->value_count++];
        __builtin_memset(v, 0, sizeof(*v));
        return v;
    }
    DxValue *values = grow(p, m->values, m->value_count, &p->module_capacity, sizeof(DxValue));
    if (!values)
        return NULL;
    m->values = values;
    *id = m->value_count;
    DxValue *v = &m->values[m->value_count++];
    __builtin_memset(v, 0, sizeof(*v));
    return v;
}

static uint32_t next_value(Parser *p) { return p->m->value_count + (p->f ? p->f->value_count : 0); }

const DxValue *dx_value(const DxModule *m, const DxFunction *f, uint32_t id)
{
    if (id < m->value_count)
        return &m->values[id];
    if (f && id - m->value_count < f->value_count)
        return &f->values[id - m->value_count];
    return NULL;
}

static void read_constants(Parser *p, const BcBlock *b)
{
    uint32_t type = DX_NONE;
    for (uint32_t i = 0; i < b->count && !p->failed; i++) {
        const BcRecord *r = b->entries[i].record;
        if (!r)
            continue;
        if (r->code == CST_SETTYPE) {
            if (r->count < 1 || r->ops[0] >= p->m->type_count) {
                fail(p, "bad constant type");
                return;
            }
            type = (uint32_t)r->ops[0];
            continue;
        }
        if (type == DX_NONE) {
            fail(p, "constant without a type");
            return;
        }
        uint32_t id;
        DxValue *v = new_value(p, &id);
        if (!v)
            return;
        v->type = type;
        switch (r->code) {
        case CST_NULL: v->kind = DV_NULL; break;
        case CST_UNDEF: v->kind = DV_UNDEF; break;
        case CST_INTEGER:
            v->kind = DV_INT;
            v->bits = r->count ? (uint64_t)signed_vbr(r->ops[0]) : 0;
            break;
        case CST_WIDE_INTEGER:
            v->kind = DV_INT;
            v->bits = r->count ? (uint64_t)signed_vbr(r->ops[0]) : 0;
            break;
        case CST_FLOAT:
            v->kind = DV_FLOAT;
            v->bits = r->count ? r->ops[0] : 0;
            break;
        case CST_AGGREGATE:
            v->kind = DV_AGGREGATE;
            v->count = r->count;
            v->ops = alloc(p, r->count * sizeof(uint32_t));
            for (uint32_t k = 0; k < r->count && v->ops; k++)
                v->ops[k] = (uint32_t)r->ops[k];
            break;
        case CST_STRING:
        case CST_CSTRING:
        case CST_DATA:
            v->kind = DV_AGGREGATE;
            v->count = r->count + (r->code == CST_CSTRING);
            v->data = alloc(p, v->count * sizeof(uint64_t));
            for (uint32_t k = 0; k < r->count && v->data; k++)
                v->data[k] = r->ops[k];
            break;
        case CST_CE_BINOP:
            if (r->count < 3)
                goto bad;
            v->kind = DV_EXPR;
            v->sub = DX_EXPR_BINOP;
            v->bits = r->ops[0];
            v->count = 2;
            v->ops = alloc(p, 2 * sizeof(uint32_t));
            if (v->ops) {
                v->ops[0] = (uint32_t)r->ops[1];
                v->ops[1] = (uint32_t)r->ops[2];
            }
            break;
        case CST_CE_CAST:
            if (r->count < 3)
                goto bad;
            v->kind = DV_EXPR;
            v->sub = DX_EXPR_CAST;
            v->bits = r->ops[0];
            v->count = 1;
            v->ops = alloc(p, sizeof(uint32_t));
            if (v->ops)
                v->ops[0] = (uint32_t)r->ops[2];
            break;
        case CST_CE_GEP:
        case CST_CE_INBOUNDS_GEP: {
            uint32_t at = r->count & 1;
            v->kind = DV_EXPR;
            v->sub = DX_EXPR_GEP;
            v->count = (r->count - at) / 2;
            v->ops = alloc(p, v->count * sizeof(uint32_t));
            for (uint32_t k = 0; k < v->count && v->ops; k++)
                v->ops[k] = (uint32_t)r->ops[at + 2 * k + 1];
            break;
        }
        default: fail(p, "unsupported constant"); return;
        }
        continue;
    bad:
        fail(p, "malformed constant");
    }
}

/* ------------------------------------------------------------------ metadata */

static DxMetadata *new_md(Parser *p, uint32_t *capacity)
{
    DxModule *m = p->m;
    DxMetadata *list = grow(p, m->metadata, m->metadata_count, capacity, sizeof(DxMetadata));
    if (!list)
        return NULL;
    m->metadata = list;
    DxMetadata *md = &m->metadata[m->metadata_count++];
    __builtin_memset(md, 0, sizeof(*md));
    return md;
}

static void read_metadata(Parser *p, const BcBlock *b, uint32_t *capacity, uint32_t *named_capacity)
{
    DxModule *m = p->m;
    const char *name = NULL;
    for (uint32_t i = 0; i < b->count && !p->failed; i++) {
        const BcRecord *r = b->entries[i].record;
        if (!r)
            continue;
        switch (r->code) {
        case MD_NAME: name = record_string(p, r, 0); continue;
        case MD_KIND:
        case MD_ATTACHMENT: continue;
        case MD_NAMED_NODE: {
            DxNamedMetadata *list = grow(p, m->named, m->named_count, named_capacity, sizeof(DxNamedMetadata));
            if (!list)
                return;
            m->named = list;
            DxNamedMetadata *n = &m->named[m->named_count++];
            n->name = name ? name : "";
            n->count = r->count;
            n->nodes = alloc(p, r->count * sizeof(uint32_t));
            for (uint32_t k = 0; k < r->count && n->nodes; k++)
                n->nodes[k] = (uint32_t)r->ops[k];
            name = NULL;
            continue;
        }
        default: break;
        }
        DxMetadata *md = new_md(p, capacity);
        if (!md)
            return;
        switch (r->code) {
        case MD_STRING_OLD:
            md->kind = DM_STRING;
            md->string = record_string(p, r, 0);
            break;
        case MD_VALUE:
            if (r->count < 2) {
                fail(p, "malformed metadata value");
                return;
            }
            md->kind = DM_VALUE;
            md->type = (uint32_t)r->ops[0];
            md->value = (uint32_t)r->ops[1];
            break;
        case MD_NODE:
        case MD_DISTINCT_NODE:
            md->kind = DM_NODE;
            md->count = r->count;
            md->ops = alloc(p, r->count * sizeof(uint32_t));
            for (uint32_t k = 0; k < r->count && md->ops; k++)
                md->ops[k] = r->ops[k] ? (uint32_t)r->ops[k] - 1 : DX_NONE;
            break;
        default:
            /* debug information and others: a node nothing reads. */
            md->kind = DM_NULL;
            break;
        }
    }
}

const DxNamedMetadata *dx_named(const DxModule *m, const char *name)
{
    for (uint32_t i = 0; i < m->named_count; i++) {
        const char *a = m->named[i].name, *b = name;
        while (*a && *a == *b)
            a++, b++;
        if (*a == *b)
            return &m->named[i];
    }
    return NULL;
}

const DxMetadata *dx_md_op(const DxModule *m, const DxMetadata *node, uint32_t index)
{
    if (!node || node->kind != DM_NODE || index >= node->count || node->ops[index] >= m->metadata_count)
        return NULL;
    const DxMetadata *md = &m->metadata[node->ops[index]];
    return md->kind == DM_NULL ? NULL : md;
}

uint64_t dx_md_int(const DxModule *m, const DxMetadata *node, uint32_t index, uint64_t fallback)
{
    const DxMetadata *md = dx_md_op(m, node, index);
    if (!md || md->kind != DM_VALUE)
        return fallback;
    const DxValue *v = dx_value(m, NULL, md->value);
    if (!v || v->kind != DV_INT)
        return v && v->kind == DV_NULL ? 0 : fallback;
    return v->bits;
}

const char *dx_md_string(const DxModule *m, const DxMetadata *node, uint32_t index)
{
    const DxMetadata *md = dx_md_op(m, node, index);
    return md && md->kind == DM_STRING ? md->string : NULL;
}

/* ------------------------------------------------------------------ functions */

typedef struct Cursor {
    Parser *p;
    const BcRecord *r;
    uint32_t at;
} Cursor;

static int more(Cursor *c) { return c->at < c->r->count; }

static uint64_t take(Cursor *c)
{
    if (c->at >= c->r->count) {
        fail(c->p, "short instruction");
        return 0;
    }
    return c->r->ops[c->at++];
}

static uint32_t value_type(Parser *p, uint32_t id)
{
    const DxValue *v = dx_value(p->m, p->f, id);
    return v ? v->type : DX_NONE;
}

/* a relative operand; a forward reference carries its type. */
static uint32_t typed_operand(Cursor *c, uint32_t *type)
{
    uint32_t now = next_value(c->p);
    uint32_t id = now - (uint32_t)take(c);
    if (id >= now) {
        uint32_t t = (uint32_t)take(c);
        if (type)
            *type = t;
    } else if (type) {
        *type = value_type(c->p, id);
    }
    return id;
}

static uint32_t operand(Cursor *c) { return next_value(c->p) - (uint32_t)take(c); }

static uint32_t signed_operand(Cursor *c) { return (uint32_t)((int64_t)next_value(c->p) - signed_vbr(take(c))); }

static DxInst *new_inst(Parser *p, uint8_t op, uint32_t count)
{
    DxFunction *f = p->f;
    DxInst *insts = grow(p, f->insts, f->inst_count, &p->inst_capacity, sizeof(DxInst));
    if (!insts)
        return NULL;
    f->insts = insts;
    DxInst *ins = &f->insts[f->inst_count++];
    __builtin_memset(ins, 0, sizeof(*ins));
    ins->op = op;
    ins->type = DX_NONE;
    ins->value = DX_NONE;
    ins->count = count;
    ins->ops = alloc(p, count * sizeof(uint32_t));
    return ins->ops || !count ? ins : NULL;
}

/* the type an element pointer's indices reach from a base of type base. */
static uint32_t indexed_type(Parser *p, uint32_t type, const uint32_t *indices, uint32_t count)
{
    for (uint32_t i = 0; i < count && !p->failed; i++) {
        const DxType *t = type_at(p, type);
        if (!t)
            return DX_NONE;
        if (t->kind == DT_STRUCT) {
            const DxValue *v = dx_value(p->m, p->f, indices[i]);
            if (!v || (v->kind != DV_INT && v->kind != DV_NULL) || v->bits >= t->count) {
                fail(p, "bad struct index");
                return DX_NONE;
            }
            type = t->members[v->bits];
        } else if (t->kind == DT_ARRAY || t->kind == DT_VECTOR || t->kind == DT_POINTER) {
            type = t->element;
        } else {
            fail(p, "bad element index");
            return DX_NONE;
        }
    }
    return type;
}

static uint32_t aggregate_member(Parser *p, uint32_t type, uint32_t index)
{
    const DxType *t = type_at(p, type);
    if (!t)
        return DX_NONE;
    if (t->kind == DT_STRUCT && index < t->count)
        return t->members[index];
    if (t->kind == DT_ARRAY || t->kind == DT_VECTOR)
        return t->element;
    fail(p, "bad aggregate index");
    return DX_NONE;
}

static int is_terminator(uint8_t op) { return op == DI_RET || op == DI_BR || op == DI_SWITCH || op == DI_UNREACHABLE; }

static void read_instruction(Parser *p, const BcRecord *r, uint32_t *block)
{
    Cursor c = {p, r, 0};
    DxInst *ins = NULL;
    uint32_t type = DX_NONE;
    switch (r->code) {
    case FUNC_BINOP: {
        ins = new_inst(p, DI_BINOP, 2);
        if (!ins)
            return;
        ins->ops[0] = typed_operand(&c, &type);
        ins->ops[1] = operand(&c);
        ins->sub = (uint8_t)take(&c);
        break;
    }
    case FUNC_CAST:
        ins = new_inst(p, DI_CAST, 1);
        if (!ins)
            return;
        ins->ops[0] = typed_operand(&c, NULL);
        type = (uint32_t)take(&c);
        ins->sub = (uint8_t)take(&c);
        break;
    case FUNC_GEP:
    case FUNC_GEP_OLD:
    case FUNC_INBOUNDS_GEP_OLD: {
        uint32_t source = DX_NONE;
        int inbounds = r->code == FUNC_INBOUNDS_GEP_OLD;
        if (r->code == FUNC_GEP) {
            inbounds = (int)take(&c);
            source = (uint32_t)take(&c);
        }
        uint32_t ids[64], types[64], n = 0;
        while (more(&c) && n < 64 && !p->failed) {
            ids[n] = typed_operand(&c, &types[n]);
            n++;
        }
        if (n < 1 || more(&c)) {
            fail(p, "malformed element pointer");
            return;
        }
        ins = new_inst(p, DI_GEP, n);
        if (!ins)
            return;
        __builtin_memcpy(ins->ops, ids, n * 4);
        ins->flags = inbounds ? DX_INST_INBOUNDS : 0;
        const DxType *base = type_at(p, types[0]);
        if (!base || base->kind != DT_POINTER) {
            fail(p, "element pointer of a non pointer");
            return;
        }
        if (source == DX_NONE)
            source = base->element;
        uint32_t reached = indexed_type(p, source, ids + 2, n > 2 ? n - 2 : 0);
        if (reached != DX_NONE)
            type = dx_pointer_type(p->m, reached, base->width);
        break;
    }
    case FUNC_SELECT:
    case FUNC_VSELECT:
        ins = new_inst(p, DI_SELECT, 3);
        if (!ins)
            return;
        ins->ops[1] = typed_operand(&c, &type);
        ins->ops[2] = operand(&c);
        ins->ops[0] = r->code == FUNC_VSELECT ? typed_operand(&c, NULL) : operand(&c);
        break;
    case FUNC_EXTRACTELT: {
        ins = new_inst(p, DI_EXTRACTELT, 2);
        if (!ins)
            return;
        uint32_t vt;
        ins->ops[0] = typed_operand(&c, &vt);
        ins->ops[1] = typed_operand(&c, NULL);
        const DxType *t = type_at(p, vt);
        type = t ? t->element : DX_NONE;
        break;
    }
    case FUNC_INSERTELT:
        ins = new_inst(p, DI_INSERTELT, 3);
        if (!ins)
            return;
        ins->ops[0] = typed_operand(&c, &type);
        ins->ops[1] = operand(&c);
        ins->ops[2] = typed_operand(&c, NULL);
        break;
    case FUNC_SHUFFLEVEC: {
        ins = new_inst(p, DI_SHUFFLEVEC, 3);
        if (!ins)
            return;
        uint32_t vt;
        ins->ops[0] = typed_operand(&c, &vt);
        ins->ops[1] = operand(&c);
        ins->ops[2] = operand(&c);
        const DxType *t = type_at(p, vt);
        const DxValue *mask = dx_value(p->m, p->f, ins->ops[2]);
        const DxType *mt = mask ? type_at(p, mask->type) : NULL;
        if (t && mt)
            type = find_type(p, (DxType){DT_VECTOR, 0, t->element, mt->count, NULL, NULL});
        break;
    }
    case FUNC_CMP:
    case FUNC_CMP2: {
        ins = new_inst(p, DI_CMP, 2);
        if (!ins)
            return;
        uint32_t ot;
        ins->ops[0] = typed_operand(&c, &ot);
        ins->ops[1] = operand(&c);
        ins->sub = (uint8_t)take(&c);
        const DxType *t = type_at(p, ot);
        type = int_type(p, 1);
        if (t && t->kind == DT_VECTOR)
            type = find_type(p, (DxType){DT_VECTOR, 0, type, t->count, NULL, NULL});
        break;
    }
    case FUNC_RET:
        ins = new_inst(p, DI_RET, more(&c) ? 1 : 0);
        if (ins && ins->count)
            ins->ops[0] = typed_operand(&c, NULL);
        break;
    case FUNC_BR:
        if (r->count == 1) {
            ins = new_inst(p, DI_BR, 1);
            if (ins)
                ins->ops[0] = (uint32_t)take(&c);
        } else {
            ins = new_inst(p, DI_BR, 3);
            if (ins) {
                ins->ops[0] = (uint32_t)take(&c);
                ins->ops[1] = (uint32_t)take(&c);
                ins->ops[2] = operand(&c);
            }
        }
        break;
    case FUNC_SWITCH: {
        if (r->count < 3 || (r->ops[0] >> 16) == 0x4B5) {
            fail(p, "unsupported switch");
            return;
        }
        uint32_t cases = (r->count - 3) / 2;
        ins = new_inst(p, DI_SWITCH, 2 + cases * 2);
        if (!ins)
            return;
        take(&c);
        ins->ops[0] = operand(&c);
        ins->ops[1] = (uint32_t)take(&c);
        for (uint32_t k = 0; k < cases * 2; k++)
            ins->ops[2 + k] = (uint32_t)take(&c);
        break;
    }
    case FUNC_UNREACHABLE: ins = new_inst(p, DI_UNREACHABLE, 0); break;
    case FUNC_PHI: {
        if (r->count < 1 || !(r->count & 1)) {
            fail(p, "malformed phi");
            return;
        }
        type = (uint32_t)take(&c);
        ins = new_inst(p, DI_PHI, r->count - 1);
        if (!ins)
            return;
        for (uint32_t k = 0; k + 1 < r->count; k += 2) {
            ins->ops[k] = signed_operand(&c);
            ins->ops[k + 1] = (uint32_t)take(&c);
        }
        break;
    }
    case FUNC_ALLOCA: {
        if (r->count < 4) {
            fail(p, "malformed alloca");
            return;
        }
        uint32_t allocated = (uint32_t)r->ops[0];
        uint64_t align = r->ops[3];
        ins = new_inst(p, DI_ALLOCA, 1);
        if (!ins)
            return;
        ins->ops[0] = (uint32_t)r->ops[2];
        if (align >> 6 & 1) {
            type = dx_pointer_type(p->m, allocated, 0);
        } else {
            type = allocated;
        }
        break;
    }
    case FUNC_LOAD: {
        ins = new_inst(p, DI_LOAD, 1);
        if (!ins)
            return;
        uint32_t pt;
        ins->ops[0] = typed_operand(&c, &pt);
        if (r->count - c.at == 3) {
            type = (uint32_t)take(&c);
        } else {
            const DxType *t = type_at(p, pt);
            type = t ? t->element : DX_NONE;
        }
        take(&c);
        if (take(&c))
            ins->flags |= DX_INST_VOLATILE;
        break;
    }
    case FUNC_STORE:
        ins = new_inst(p, DI_STORE, 2);
        if (!ins)
            return;
        ins->ops[0] = typed_operand(&c, NULL);
        ins->ops[1] = typed_operand(&c, NULL);
        break;
    case FUNC_EXTRACTVAL: {
        uint32_t at;
        uint32_t agg_type;
        Cursor peek = c;
        uint32_t agg = typed_operand(&peek, &agg_type);
        at = peek.at;
        uint32_t n = r->count - at;
        ins = new_inst(p, DI_EXTRACTVAL, 1 + n);
        if (!ins)
            return;
        ins->ops[0] = agg;
        type = agg_type;
        for (uint32_t k = 0; k < n; k++) {
            ins->ops[1 + k] = (uint32_t)r->ops[at + k];
            type = aggregate_member(p, type, ins->ops[1 + k]);
        }
        break;
    }
    case FUNC_INSERTVAL: {
        Cursor peek = c;
        uint32_t agg = typed_operand(&peek, &type);
        uint32_t val = typed_operand(&peek, NULL);
        uint32_t n = r->count - peek.at;
        ins = new_inst(p, DI_INSERTVAL, 2 + n);
        if (!ins)
            return;
        ins->ops[0] = agg;
        ins->ops[1] = val;
        for (uint32_t k = 0; k < n; k++)
            ins->ops[2 + k] = (uint32_t)r->ops[peek.at + k];
        break;
    }
    case FUNC_CALL: {
        take(&c);
        uint64_t cc = take(&c);
        if (cc >> 17 & 1)
            take(&c);
        uint32_t fn_type = DX_NONE;
        if (cc >> 15 & 1)
            fn_type = (uint32_t)take(&c);
        uint32_t callee_type;
        uint32_t callee = typed_operand(&c, &callee_type);
        if (fn_type == DX_NONE) {
            const DxType *t = type_at(p, callee_type);
            fn_type = t && t->kind == DT_POINTER ? t->element : DX_NONE;
        }
        const DxType *ft = fn_type != DX_NONE ? type_at(p, fn_type) : NULL;
        if (!ft || ft->kind != DT_FUNCTION) {
            fail(p, "call of a non function");
            return;
        }
        uint32_t args[64], n = 0;
        for (uint32_t k = 0; k < ft->count && n < 64; k++) {
            const DxType *pt = type_at(p, ft->members[k]);
            args[n++] = pt && pt->kind == DT_LABEL ? (uint32_t)take(&c) : operand(&c);
        }
        while (more(&c) && n < 64 && !p->failed)
            args[n++] = typed_operand(&c, NULL);
        ins = new_inst(p, DI_CALL, 1 + n);
        if (!ins)
            return;
        ins->ops[0] = callee;
        __builtin_memcpy(ins->ops + 1, args, n * 4);
        type = ft->element;
        break;
    }
    case FUNC_ATOMICRMW: {
        ins = new_inst(p, DI_ATOMICRMW, 2);
        if (!ins)
            return;
        uint32_t pt;
        ins->ops[0] = typed_operand(&c, &pt);
        ins->ops[1] = operand(&c);
        ins->sub = (uint8_t)take(&c);
        const DxType *t = type_at(p, pt);
        type = t ? t->element : DX_NONE;
        break;
    }
    case FUNC_CMPXCHG: {
        ins = new_inst(p, DI_CMPXCHG, 3);
        if (!ins)
            return;
        uint32_t pt;
        ins->ops[0] = typed_operand(&c, &pt);
        ins->ops[1] = typed_operand(&c, NULL);
        ins->ops[2] = operand(&c);
        const DxType *t = type_at(p, pt);
        if (t) {
            uint32_t members[2] = {t->element, int_type(p, 1)};
            uint32_t *m = alloc(p, sizeof(members));
            if (m) {
                __builtin_memcpy(m, members, sizeof(members));
                type = add_type(p, (DxType){DT_STRUCT, 0, DX_NONE, 2, m, NULL});
            }
        }
        break;
    }
    case FUNC_DEBUG_LOC:
    case FUNC_DEBUG_LOC_AGAIN: return;
    default: fail(p, "unsupported instruction"); return;
    }
    if (!ins || p->failed)
        return;
    DxFunction *f = p->f;
    if (*block >= f->block_count) {
        fail(p, "instruction past the last block");
        return;
    }
    ins->block = *block;
    ins->type = type;
    const DxType *t = type != DX_NONE ? type_at(p, type) : NULL;
    if (t && t->kind != DT_VOID) {
        uint32_t id;
        DxValue *v = new_value(p, &id);
        if (!v)
            return;
        v->kind = DV_INSTRUCTION;
        v->type = type;
        v->inst = f->inst_count - 1;
        /* the instruction may have moved with the array. */
        f->insts[f->inst_count - 1].value = id;
    } else if (r->code != FUNC_STORE && !is_terminator(ins->op) && r->code != FUNC_CALL) {
        if (type == DX_NONE) {
            fail(p, "instruction without a type");
            return;
        }
    }
    DxBlock *bb = &f->blocks[*block];
    if (!bb->count)
        bb->first = f->inst_count - 1;
    bb->count++;
    if (is_terminator(ins->op))
        (*block)++;
}

static void read_body(Parser *p, DxFunction *f, const BcBlock *b)
{
    DxModule *m = p->m;
    const DxType *ft = type_at(p, f->type);
    if (!ft)
        return;
    p->f = f;
    p->value_capacity = 0;
    p->inst_capacity = 0;
    for (uint32_t k = 0; k < ft->count; k++) {
        uint32_t id;
        DxValue *v = new_value(p, &id);
        if (!v)
            return;
        v->kind = DV_ARGUMENT;
        v->type = ft->members[k];
        v->inst = k;
    }
    uint32_t block = 0;
    for (uint32_t i = 0; i < b->count && !p->failed; i++) {
        const BcEntry *e = &b->entries[i];
        if (e->block) {
            if (e->block->id == BLOCK_CONSTANTS)
                read_constants(p, e->block);
            continue;
        }
        const BcRecord *r = e->record;
        if (r->code == FUNC_DECLAREBLOCKS) {
            if (r->count < 1 || r->ops[0] == 0 || r->ops[0] > 1u << 20 || f->blocks) {
                fail(p, "bad block count");
                return;
            }
            f->block_count = (uint32_t)r->ops[0];
            f->blocks = alloc(p, f->block_count * sizeof(DxBlock));
            continue;
        }
        if (!f->blocks) {
            fail(p, "instruction before the blocks");
            return;
        }
        read_instruction(p, r, &block);
    }
    if (!p->failed && block != f->block_count)
        fail(p, "a block without a terminator");
    (void)m;
    p->f = NULL;
}

/* ------------------------------------------------------------------ the module */

static void read_symbols(Parser *p, const BcBlock *b)
{
    DxModule *m = p->m;
    for (uint32_t i = 0; i < b->count && !p->failed; i++) {
        const BcRecord *r = b->entries[i].record;
        if (!r || r->code != VST_ENTRY || r->count < 1 || r->ops[0] >= m->value_count)
            continue;
        m->values[r->ops[0]].name = record_string(p, r, 1);
    }
}

int dx_module_read(DxModule *m, const uint8_t *bitcode, size_t size, const MimeAllocator *allocator)
{
    __builtin_memset(m, 0, sizeof(*m));
    m->arena.alloc = allocator;
    Parser p = {m, 0, NULL, 0, 0, 0};
    BcBlock *top = bc_parse(bitcode, size, &m->arena);
    if (!top)
        return fail(&p, "malformed bitcode");
    const BcBlock *module = NULL;
    for (uint32_t i = 0; i < top->count; i++)
        if (top->entries[i].block && top->entries[i].block->id == BLOCK_MODULE)
            module = top->entries[i].block;
    if (!module)
        return fail(&p, "no module");

    uint32_t function_capacity = 0, md_capacity = 0, named_capacity = 0, body = 0;
    /* the functions with bodies, in the order their bodies follow. */
    uint32_t *bodies = NULL, body_count = 0, body_capacity = 0;
    for (uint32_t i = 0; i < module->count && !p.failed; i++) {
        const BcEntry *e = &module->entries[i];
        if (e->block) {
            switch (e->block->id) {
            case BLOCK_TYPE: read_types(&p, e->block); break;
            case BLOCK_CONSTANTS: read_constants(&p, e->block); break;
            case BLOCK_METADATA: read_metadata(&p, e->block, &md_capacity, &named_capacity); break;
            case BLOCK_VALUE_SYMTAB: read_symbols(&p, e->block); break;
            case BLOCK_FUNCTION:
                if (body >= body_count)
                    return fail(&p, "a body without a function");
                read_body(&p, &m->functions[bodies[body++]], e->block);
                break;
            default: break;
            }
            continue;
        }
        const BcRecord *r = e->record;
        if (r->code == MODULE_GLOBALVAR) {
            if (r->count < 3)
                return fail(&p, "malformed global");
            uint32_t id;
            DxValue *v = new_value(&p, &id);
            if (!v)
                break;
            v->kind = DV_GLOBAL;
            uint32_t flags = (uint32_t)r->ops[1];
            v->sub = flags & 1;
            if (flags & 2) {
                v->type = dx_pointer_type(m, (uint32_t)r->ops[0], flags >> 2);
            } else {
                v->type = (uint32_t)r->ops[0];
            }
            v->count = 1;
            v->ops = alloc(&p, sizeof(uint32_t));
            if (v->ops)
                v->ops[0] = r->ops[2] ? (uint32_t)r->ops[2] - 1 : DX_NONE;
        } else if (r->code == MODULE_FUNCTION) {
            if (r->count < 3)
                return fail(&p, "malformed function");
            uint32_t id;
            DxValue *v = new_value(&p, &id);
            if (!v)
                break;
            uint32_t type = (uint32_t)r->ops[0];
            const DxType *t = type_at(&p, type);
            if (t && t->kind == DT_POINTER)
                type = t->element;
            v->kind = DV_FUNCTION;
            v->type = dx_pointer_type(m, type, 0);
            DxFunction *list = grow(&p, m->functions, m->function_count, &function_capacity, sizeof(DxFunction));
            if (!list)
                break;
            m->functions = list;
            v->inst = m->function_count;
            DxFunction *f = &m->functions[m->function_count++];
            __builtin_memset(f, 0, sizeof(*f));
            f->type = type;
            f->value = id;
            f->prototype = r->ops[2] != 0;
            if (!f->prototype) {
                uint32_t *b = grow(&p, bodies, body_count, &body_capacity, sizeof(uint32_t));
                if (!b)
                    break;
                bodies = b;
                bodies[body_count++] = m->function_count - 1;
            }
        }
    }
    if (p.failed)
        return -1;
    for (uint32_t i = 0; i < m->function_count; i++)
        m->functions[i].name = m->values[m->functions[i].value].name;
    return 0;
}

void dx_module_free(DxModule *m) { bc_arena_free(&m->arena); }
