#include "ir.h"
#include "../dxbc/dxbc.h"

uint32_t lw_tight_size(const HlType *t, bool row_major_default);
uint32_t ir_return_type(const HlType *elem);

typedef struct Patch {
    uint32_t at;
    const char *string;
    const HlType *type;
} Patch;

typedef struct Chunk {
    Hlsl *h;
    HlBuf b;
    Patch *patches;
    uint32_t patch_count, patch_capacity;
} Chunk;

static uint32_t w32(Chunk *c, uint32_t v)
{
    uint32_t at = (uint32_t)c->b.length;
    hl_buf_append(c->h, &c->b, (const char *)&v, 4);
    return at;
}

static void w16(Chunk *c, uint16_t v) { hl_buf_append(c->h, &c->b, (const char *)&v, 2); }

static void set32(Chunk *c, uint32_t at, uint32_t v) { __builtin_memcpy(c->b.data + at, &v, 4); }

static void string_at(Chunk *c, uint32_t at, const char *s)
{
    HL_PUSH(c->h, c->patches, c->patch_count, c->patch_capacity, ((Patch){at, s, nullptr}));
}

/* strings at the end, each once. */
static void write_strings(Chunk *c)
{
    const char **seen = hl_alloc(c->h, (c->patch_count + 1) * sizeof(char *));
    uint32_t *where = hl_alloc(c->h, (c->patch_count + 1) * sizeof(uint32_t));
    uint32_t count = 0;
    for (uint32_t i = 0; i < c->patch_count; i++) {
        const Patch *p = &c->patches[i];
        if (!p->string)
            continue;
        uint32_t at = ~0u;
        for (uint32_t j = 0; j < count; j++)
            if (hl_streq(seen[j], p->string))
                at = where[j];
        if (at == ~0u) {
            at = (uint32_t)c->b.length;
            hl_buf_append(c->h, &c->b, p->string, __builtin_strlen(p->string) + 1);
            seen[count] = p->string;
            where[count++] = at;
        }
        set32(c, p->at, at);
    }
    while (c->b.length & 3)
        hl_buf_putc(c->h, &c->b, (char)0xAB);
}

/* ------------------------------------------------------------------ RDEF */

enum { CLASS_SCALAR, CLASS_VECTOR, CLASS_MATRIX_ROWS, CLASS_MATRIX_COLUMNS, CLASS_OBJECT, CLASS_STRUCT };

static uint16_t svt(uint8_t base)
{
    switch (base) {
    case BT_BOOL: return 1;
    case BT_INT: case BT_LIT_INT: return 2;
    case BT_UINT: return 19;
    case BT_DOUBLE: return 39;
    case BT_MIN16FLOAT: return 58;
    case BT_MIN10FLOAT: return 59;
    case BT_MIN16INT: return 60;
    case BT_MIN12INT: return 61;
    case BT_MIN16UINT: return 62;
    default: return 3;
    }
}

typedef struct TypeEntry {
    const HlType *type;
    uint32_t offset;
} TypeEntry;

typedef struct Rdef {
    Chunk c;
    IrShader *s;
    bool sm5;
    TypeEntry *types;
    uint32_t type_count, type_capacity;
} Rdef;

static void type_at(Rdef *r, uint32_t at, const HlType *t)
{
    HL_PUSH(r->c.h, r->c.patches, r->c.patch_count, r->c.patch_capacity, ((Patch){at, nullptr, t}));
}

/* the types of the queued patches, written as the queue grows; struct members queue theirs. */
static void write_types(Rdef *r)
{
    Chunk *c = &r->c;
    for (uint32_t i = 0; i < c->patch_count; i++) {
        Patch p = c->patches[i];
        if (!p.type)
            continue;
        const HlType *t = p.type;
        uint32_t found = ~0u;
        for (uint32_t j = 0; j < r->type_count; j++)
            if (r->types[j].type == t)
                found = r->types[j].offset;
        if (found != ~0u) {
            set32(c, p.at, found);
            continue;
        }
        const HlType *elem = t;
        uint32_t elements = 0;
        if (t->kind == TY_ARRAY) {
            elements = t->count;
            elem = t->elem;
            while (elem->kind == TY_ARRAY) {
                elements *= elem->count;
                elem = elem->elem;
            }
        }
        uint32_t at = (uint32_t)c->b.length;
        HL_PUSH(c->h, r->types, r->type_count, r->type_capacity, ((TypeEntry){t, at}));
        set32(c, p.at, at);
        uint16_t cls, type = 0, rows = 1, cols = 1, members = 0;
        switch (elem->kind) {
        case TY_SCALAR: cls = CLASS_SCALAR; type = svt(elem->base); break;
        case TY_VECTOR: cls = CLASS_VECTOR; type = svt(elem->base); cols = elem->cols; break;
        case TY_MATRIX:
            cls = hl_type_row_major(elem, r->s->row_major_default) ? CLASS_MATRIX_ROWS : CLASS_MATRIX_COLUMNS;
            type = svt(elem->base);
            rows = elem->rows;
            cols = elem->cols;
            break;
        case TY_STRUCT:
            cls = CLASS_STRUCT;
            type = 0;
            rows = 1;
            cols = (uint16_t)hl_type_components(elem);
            for (uint32_t k = 0; k < elem->st->field_count; k++)
                if (elem->st->fields[k].type->kind != TY_OBJECT)
                    members++;
            break;
        default: cls = CLASS_OBJECT; break;
        }
        w16(c, cls);
        w16(c, type);
        w16(c, rows);
        w16(c, cols);
        w16(c, (uint16_t)elements);
        w16(c, members);
        uint32_t member_at = w32(c, 0);
        if (r->sm5) {
            w32(c, 0);
            w32(c, 0);
            w32(c, 0);
            w32(c, 0);
            uint32_t name_at = w32(c, 0);
            string_at(c, name_at, elem->kind == TY_STRUCT ? (elem->st->name ? elem->st->name : "")
                                                           : hl_type_name(c->h, elem));
        }
        if (members) {
            set32(c, member_at, (uint32_t)c->b.length);
            /* member offsets as a constant buffer packs the struct. */
            uint32_t offset = 0;
            bool rm = r->s->row_major_default;
            for (uint32_t k = 0; k < elem->st->field_count; k++) {
                const HlField *f = &elem->st->fields[k];
                if (f->type->kind == TY_OBJECT)
                    continue;
                uint32_t size = hl_type_cb_size(f->type, rm);
                bool aggregate = f->type->kind == TY_ARRAY || f->type->kind == TY_STRUCT || f->type->kind == TY_MATRIX;
                if (aggregate || (offset & 15) + size > 16)
                    offset = (offset + 15) & ~15u;
                string_at(c, w32(c, 0), f->name);
                type_at(r, w32(c, 0), f->type);
                w32(c, offset);
                offset += size;
            }
        }
    }
}

static uint32_t srv_dimension(uint8_t object)
{
    switch (object) {
    case OBJ_BUFFER: case OBJ_RWBUFFER: return 1;
    case OBJ_TEXTURE1D: case OBJ_RWTEXTURE1D: return 2;
    case OBJ_TEXTURE1DARRAY: case OBJ_RWTEXTURE1DARRAY: return 3;
    case OBJ_TEXTURE2D: case OBJ_RWTEXTURE2D: return 4;
    case OBJ_TEXTURE2DARRAY: case OBJ_RWTEXTURE2DARRAY: return 5;
    case OBJ_TEXTURE2DMS: return 6;
    case OBJ_TEXTURE2DMSARRAY: return 7;
    case OBJ_TEXTURE3D: case OBJ_RWTEXTURE3D: return 8;
    case OBJ_TEXTURECUBE: return 9;
    case OBJ_TEXTURECUBEARRAY: return 10;
    case OBJ_STRUCTURED_BUFFER: case OBJ_RWSTRUCTURED_BUFFER: case OBJ_APPEND_STRUCTURED_BUFFER:
    case OBJ_CONSUME_STRUCTURED_BUFFER: case OBJ_BYTE_ADDRESS_BUFFER: case OBJ_RWBYTE_ADDRESS_BUFFER:
        return 1;
    default: return 0;
    }
}

static uint32_t input_type(const IrResource *r)
{
    if (r->kind == SRC_SAMPLER)
        return 3;
    switch (r->object) {
    case OBJ_STRUCTURED_BUFFER: return 5;
    case OBJ_BYTE_ADDRESS_BUFFER: return 7;
    case OBJ_RWSTRUCTURED_BUFFER: return r->counter ? 11 : 6;
    case OBJ_RWBYTE_ADDRESS_BUFFER: return 8;
    case OBJ_APPEND_STRUCTURED_BUFFER: return 9;
    case OBJ_CONSUME_STRUCTURED_BUFFER: return 10;
    default: return r->kind == SRC_UAV ? 4 : 2;
    }
}

static bool structured(uint8_t object)
{
    return object == OBJ_STRUCTURED_BUFFER || object == OBJ_RWSTRUCTURED_BUFFER ||
           object == OBJ_APPEND_STRUCTURED_BUFFER || object == OBJ_CONSUME_STRUCTURED_BUFFER;
}

static const HlType *element_type(const HlType *t)
{
    while (t->kind == TY_ARRAY)
        t = t->elem;
    return t;
}

/* a variable is used when the code reads any byte of it. */
static bool var_used(IrShader *s, uint32_t buffer, uint32_t start, uint32_t size)
{
    for (uint32_t i = 0; i < s->count; i++) {
        const IrIns *ins = &s->ins[i];
        if (ins->op == IR_NOP)
            continue;
        for (uint32_t k = 0; k < ins->src_count; k++) {
            const IrSrc *src = &ins->src[k];
            if (src->kind != SRC_CB || src->v != buffer)
                continue;
            uint32_t at = src->index * 16 + src->comp * 4;
            if (src->rel ? at < start + size : (at >= start && at < start + size))
                return true;
        }
    }
    return false;
}

static void write_rdef(IrShader *s, HlBuf *out, uint32_t flags)
{
    Rdef r_ = {.c = {.h = s->h}, .s = s}, *r = &r_;
    Chunk *c = &r->c;
    r->sm5 = s->profile.major >= 5;
    bool sm51 = s->profile.major == 5 && s->profile.minor == 1;
    /* bindings: samplers, textures, views, then constant buffers, each by slot. */
    uint32_t binding_count = 0, buffer_count = 0;
    IrResource **bound = hl_alloc(s->h, (s->resource_count + 1) * sizeof(IrResource *));
    for (int kind = 0; kind < 3; kind++) {
        uint8_t want = kind == 0 ? SRC_SAMPLER : kind == 1 ? SRC_RESOURCE : SRC_UAV;
        uint32_t first = binding_count;
        for (uint32_t i = 0; i < s->resource_count; i++) {
            IrResource *res = &s->resources[i];
            if (!res->used || res->kind != want)
                continue;
            bool seen = false;
            for (uint32_t j = first; j < binding_count; j++)
                if (bound[j]->var == res->var)
                    seen = true;
            if (seen)
                continue;
            bound[binding_count++] = res;
        }
        for (uint32_t i = first; i < binding_count; i++)
            for (uint32_t j = i + 1; j < binding_count; j++)
                if (bound[j]->slot < bound[i]->slot) {
                    IrResource *t = bound[i];
                    bound[i] = bound[j];
                    bound[j] = t;
                }
    }
    uint32_t used_cbs = 0;
    for (uint32_t i = 0; i < s->cbuffer_count; i++)
        if (s->cbuffers[i].used)
            used_cbs++;
    uint32_t structured_count = 0;
    for (uint32_t i = 0; i < binding_count; i++)
        if (structured(bound[i]->object))
            structured_count++;
    buffer_count = used_cbs + structured_count;
    uint32_t total_bindings = binding_count + used_cbs;

    uint32_t target = (uint32_t)s->profile.minor | (uint32_t)s->profile.major << 8;
    static const uint16_t program[] = {[STAGE_PIXEL] = 0xFFFF, [STAGE_VERTEX] = 0xFFFE, [STAGE_GEOMETRY] = 0x4753,
                                       [STAGE_HULL] = 0x4853,  [STAGE_DOMAIN] = 0x4453, [STAGE_COMPUTE] = 0x4353};
    target |= (uint32_t)program[s->profile.stage] << 16;
    w32(c, buffer_count);
    uint32_t buffers_at = w32(c, 0);
    w32(c, total_bindings);
    uint32_t bindings_at = w32(c, 0);
    w32(c, target);
    w32(c, flags);
    uint32_t creator_at = w32(c, 0);
    if (r->sm5) {
        w32(c, sm51 ? 0x25441313 : DXBC_FOURCC('R', 'D', '1', '1'));
        w32(c, 60);
        w32(c, 24);
        w32(c, sm51 ? 40 : 32);
        w32(c, 40);
        w32(c, 36);
        w32(c, 12);
        w32(c, 0);
    }
    string_at(c, creator_at, "Mime HLSL Shader Compiler 10.1");

    /* binding descriptions. */
    set32(c, bindings_at, (uint32_t)c->b.length);
    for (uint32_t i = 0; i < binding_count; i++) {
        IrResource *res = bound[i];
        const HlType *t = element_type(res->var->type);
        uint32_t count = 1;
        for (const HlType *a = res->var->type; a->kind == TY_ARRAY; a = a->elem)
            count *= a->count;
        string_at(c, w32(c, 0), res->var->name);
        w32(c, input_type(res));
        uint32_t ret = 0, samples = 0, flags_ = 0;
        if (res->kind != SRC_SAMPLER) {
            if (structured(res->object)) {
                ret = 6;
                samples = t->elem ? lw_tight_size(t->elem, s->row_major_default) : 0;
            } else if (res->object == OBJ_BYTE_ADDRESS_BUFFER || res->object == OBJ_RWBYTE_ADDRESS_BUFFER) {
                ret = 6;
            } else {
                ret = ir_return_type(t->elem);
                samples = res->object == OBJ_TEXTURE2DMS || res->object == OBJ_TEXTURE2DMSARRAY ? t->samples : ~0u;
                if (res->kind == SRC_UAV)
                    samples = 0;
                uint32_t comps = t->elem ? hl_type_components(t->elem) : 4;
                flags_ |= (comps - 1) << 2;
            }
        } else if (res->comparison) {
            flags_ |= 2;
        }
        w32(c, ret);
        w32(c, res->kind == SRC_SAMPLER ? 0 : srv_dimension(res->object));
        w32(c, samples);
        w32(c, res->slot - res->element);
        w32(c, count);
        w32(c, flags_);
        if (sm51) {
            w32(c, res->space);
            w32(c, res->id);
        }
    }
    for (uint32_t i = 0; i < s->cbuffer_count; i++) {
        const IrCbuffer *cb = &s->cbuffers[i];
        if (!cb->used)
            continue;
        string_at(c, w32(c, 0), cb->buffer->name);
        w32(c, cb->buffer->tbuffer ? 1 : 0);
        w32(c, 0);
        w32(c, 0);
        w32(c, 0);
        w32(c, cb->slot);
        w32(c, 1);
        w32(c, 0);
        if (sm51) {
            w32(c, cb->space);
            w32(c, i);
        }
    }

    /* buffer descriptions, then their variables. */
    set32(c, buffers_at, (uint32_t)c->b.length);
    uint32_t *var_list_at = hl_alloc(s->h, (buffer_count + 1) * sizeof(uint32_t));
    uint32_t k = 0;
    for (uint32_t i = 0; i < s->cbuffer_count; i++) {
        const IrCbuffer *cb = &s->cbuffers[i];
        if (!cb->used)
            continue;
        string_at(c, w32(c, 0), cb->buffer->name);
        w32(c, cb->buffer->var_count);
        var_list_at[k++] = w32(c, 0);
        w32(c, cb->buffer->size);
        w32(c, 0);
        w32(c, cb->buffer->tbuffer ? 1 : 0);
    }
    for (uint32_t i = 0; i < binding_count; i++) {
        if (!structured(bound[i]->object))
            continue;
        const HlType *t = element_type(bound[i]->var->type);
        string_at(c, w32(c, 0), bound[i]->var->name);
        w32(c, 1);
        var_list_at[k++] = w32(c, 0);
        w32(c, t->elem ? lw_tight_size(t->elem, s->row_major_default) : 0);
        w32(c, 0);
        w32(c, 3);
    }
    k = 0;
    for (uint32_t i = 0; i < s->cbuffer_count; i++) {
        const IrCbuffer *cb = &s->cbuffers[i];
        if (!cb->used)
            continue;
        set32(c, var_list_at[k++], (uint32_t)c->b.length);
        for (uint32_t j = 0; j < cb->buffer->var_count; j++) {
            HlVar *v = cb->buffer->vars[j];
            uint32_t start = v->packoffset * 4, size = hl_type_cb_size(v->type, s->row_major_default);
            string_at(c, w32(c, 0), v->name);
            w32(c, start);
            w32(c, size);
            w32(c, var_used(s, i, start, size) ? 2 : 0);
            type_at(r, w32(c, 0), v->type);
            w32(c, 0);
            if (r->sm5) {
                w32(c, ~0u);
                w32(c, 0);
                w32(c, ~0u);
                w32(c, 0);
            }
        }
    }
    for (uint32_t i = 0; i < binding_count; i++) {
        if (!structured(bound[i]->object))
            continue;
        const HlType *t = element_type(bound[i]->var->type);
        set32(c, var_list_at[k++], (uint32_t)c->b.length);
        string_at(c, w32(c, 0), "$Element");
        w32(c, 0);
        w32(c, t->elem ? lw_tight_size(t->elem, s->row_major_default) : 0);
        w32(c, 2);
        type_at(r, w32(c, 0), t->elem ? t->elem : hl_type_scalar(s->h, BT_UINT));
        w32(c, 0);
        if (r->sm5) {
            w32(c, ~0u);
            w32(c, 0);
            w32(c, ~0u);
            w32(c, 0);
        }
    }
    write_types(r);
    write_strings(c);
    hl_buf_append(s->h, out, c->b.data, c->b.length);
}

/* ------------------------------------------------------------------ signatures */

static void write_signature(IrShader *s, const IrSignature *sig, bool output, HlBuf *out, const uint8_t *read_masks,
                            const uint8_t *write_masks)
{
    Chunk c_ = {.h = s->h}, *c = &c_;
    w32(c, sig->count);
    w32(c, 8);
    for (uint32_t i = 0; i < sig->count; i++) {
        const IrElement *e = &sig->elements[i];
        string_at(c, w32(c, 0), e->name);
        w32(c, e->index);
        w32(c, e->sysval == 0x100 || e->sysval == 0x101 ? 0 : e->sysval);
        w32(c, e->component_type);
        w32(c, e->reg);
        uint8_t mask = e->mask;
        uint8_t used = 0;
        if (e->reg < 64) {
            if (output)
                used = (uint8_t)(mask & ~write_masks[e->reg]);
            else
                used = (uint8_t)(mask & read_masks[e->reg]);
        } else if (e->operand) {
            mask = 1;
            used = output ? 0x0E : 1;
            if (output)
                used = (uint8_t)~1 & 0xF;
        }
        uint8_t bytes[4] = {mask, used, 0, 0};
        hl_buf_append(s->h, &c->b, (const char *)bytes, 4);
    }
    write_strings(c);
    hl_buf_append(s->h, out, c->b.data, c->b.length);
}

/* ------------------------------------------------------------------ STAT */

static void write_stat(IrShader *s, HlBuf *out)
{
    uint32_t stat[37] = {};
    uint32_t instructions = 0, floats = 0, ints = 0, uints = 0, dynamic_flow = 0, textures = 0, loads = 0, movs = 0,
             movcs = 0, conversions = 0, emits = 0, cuts = 0, barriers = 0, interlocked = 0, stores = 0;
    for (uint32_t i = 0; i < s->count; i++) {
        const IrIns *ins = &s->ins[i];
        if (ins->op == IR_NOP)
            continue;
        instructions++;
        switch (ins->op) {
        case OP_ADD: case OP_MUL: case OP_MAD: case OP_DIV: case OP_MIN: case OP_MAX: case OP_FRC: case OP_SQRT:
        case OP_RSQ: case OP_EXP: case OP_LOG: case OP_DP2: case OP_DP3: case OP_DP4: case OP_SINCOS: case OP_LT:
        case OP_GE: case OP_EQ: case OP_NE: case OP_ROUND_NE: case OP_ROUND_NI: case OP_ROUND_PI: case OP_ROUND_Z:
        case OP_RCP:
            floats++;
            break;
        case OP_IADD: case OP_IMUL: case OP_IMAD: case OP_ILT: case OP_IGE: case OP_IEQ: case OP_INE: case OP_INEG:
        case OP_IMAX: case OP_IMIN: case OP_ISHL: case OP_ISHR: case OP_AND: case OP_OR: case OP_XOR: case OP_NOT:
            ints++;
            break;
        case OP_UDIV: case OP_ULT: case OP_UGE: case OP_UMUL: case OP_UMAD: case OP_UMAX: case OP_UMIN: case OP_USHR:
            uints++;
            break;
        case OP_IF: case OP_LOOP: case OP_BREAKC: case OP_CONTINUEC: case OP_SWITCH: case OP_RETC:
            dynamic_flow++;
            break;
        case OP_SAMPLE: case OP_SAMPLE_B: case OP_SAMPLE_L: case OP_SAMPLE_D: case OP_SAMPLE_C: case OP_SAMPLE_C_LZ:
        case OP_GATHER4: case OP_GATHER4_C: case OP_GATHER4_PO: case OP_GATHER4_PO_C:
            textures++;
            break;
        case OP_LD: case OP_LD_MS: case OP_LD_UAV_TYPED: case OP_LD_RAW: case OP_LD_STRUCTURED:
            loads++;
            break;
        case OP_MOV: case IR_STORE:
            movs++;
            break;
        case OP_MOVC:
            movcs++;
            break;
        case OP_FTOI: case OP_FTOU: case OP_ITOF: case OP_UTOF: case OP_F16TOF32: case OP_F32TOF16:
            conversions++;
            break;
        case OP_EMIT: case OP_EMIT_STREAM: emits++; break;
        case OP_CUT: case OP_CUT_STREAM: cuts++; break;
        case OP_SYNC: barriers++; break;
        case OP_STORE_UAV_TYPED: case OP_STORE_RAW: case OP_STORE_STRUCTURED: stores++; break;
        default:
            if (ins->op >= OP_ATOMIC_AND && ins->op <= OP_IMM_ATOMIC_UMIN)
                interlocked++;
            break;
        }
    }
    stat[0] = instructions;
    stat[1] = s->temp_count;
    stat[4] = floats;
    stat[5] = ints;
    stat[6] = uints;
    stat[8] = dynamic_flow;
    stat[10] = s->indexable_count;
    stat[12] = cuts;
    stat[13] = emits;
    stat[14] = textures;
    stat[15] = loads;
    stat[19] = movs;
    stat[20] = movcs;
    stat[21] = conversions;
    stat[23] = s->gs_input_primitive;
    stat[24] = s->gs_output_topology;
    stat[25] = s->gs_max_vertices;
    stat[30] = barriers;
    stat[31] = interlocked;
    stat[32] = stores;
    uint32_t words = s->profile.major >= 5 ? 37 : 29;
    hl_buf_append(s->h, out, (const char *)stat, words * 4);
}

/* ------------------------------------------------------------------ the container */

int ir_container(IrShader *s, const HlBuf *code, HlBuf *out)
{
    Hlsl *h = s->h;
    uint8_t read_masks[64] = {}, write_masks[64] = {};
    for (uint32_t i = 0; i < s->count; i++) {
        const IrIns *ins = &s->ins[i];
        if (ins->op == IR_NOP)
            continue;
        for (uint32_t k = 0; k < ins->src_count; k++)
            if (ins->src[k].kind == SRC_IN && ins->src[k].v < 64)
                read_masks[ins->src[k].v] |= 1u << ins->src[k].comp;
        if (ins->op == IR_STORE && ins->store.kind == DST_OUT && ins->store.v < 64)
            write_masks[ins->store.v] |= 1u << ins->store.comp;
    }
    HlBuf chunks[8] = {};
    uint32_t tags[8], count = 0;
    write_rdef(s, &chunks[count], h->options->flags | 0x100);
    tags[count++] = DXBC_FOURCC('R', 'D', 'E', 'F');
    write_signature(s, &s->inputs, false, &chunks[count], read_masks, write_masks);
    tags[count++] = DXBC_FOURCC('I', 'S', 'G', 'N');
    write_signature(s, &s->outputs, true, &chunks[count], read_masks, write_masks);
    tags[count++] = DXBC_FOURCC('O', 'S', 'G', 'N');
    hl_buf_append(h, &chunks[count], code->data, code->length);
    tags[count++] = s->profile.major >= 5 ? DXBC_FOURCC('S', 'H', 'E', 'X') : DXBC_FOURCC('S', 'H', 'D', 'R');
    write_stat(s, &chunks[count]);
    tags[count++] = DXBC_FOURCC('S', 'T', 'A', 'T');

    uint32_t header = 32 + 4 * count, total = header;
    for (uint32_t i = 0; i < count; i++)
        total += 8 + (uint32_t)((chunks[i].length + 3) & ~(size_t)3);
    uint32_t words[8] = {DXBC_FOURCC('D', 'X', 'B', 'C'), 0, 0, 0, 0, 1, total, count};
    hl_buf_append(h, out, (const char *)words, 32);
    uint32_t at = header;
    for (uint32_t i = 0; i < count; i++) {
        hl_buf_append(h, out, (const char *)&at, 4);
        at += 8 + (uint32_t)((chunks[i].length + 3) & ~(size_t)3);
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t size = (uint32_t)((chunks[i].length + 3) & ~(size_t)3);
        hl_buf_append(h, out, (const char *)&tags[i], 4);
        hl_buf_append(h, out, (const char *)&size, 4);
        if (chunks[i].length)
            hl_buf_append(h, out, chunks[i].data, chunks[i].length);
        for (size_t p = chunks[i].length; p < size; p++)
            hl_buf_putc(h, out, 0);
    }
    uint32_t checksum[4];
    dxbc_checksum(out->data, out->length, checksum);
    __builtin_memcpy(out->data + 4, checksum, 16);
    return 0;
}

/* ------------------------------------------------------------------ the backend */

int hl_backend(Hlsl *h, HlProgram *prog, HlFunc *entry)
{
    if (prog->profile.stage == STAGE_LIBRARY) {
        hl_error(h, (HlLoc){0, 1, 1}, 3000, "library targets are not supported yet");
        return -1;
    }
    if (prog->profile.major < 4) {
        hl_error(h, (HlLoc){0, 1, 1}, 3000, "shader model %u targets are not supported yet", prog->profile.major);
        return -1;
    }
    IrShader *s = hl_alloc(h, sizeof(IrShader));
    s->h = h;
    s->prog = prog;
    s->profile = prog->profile;
    s->row_major_default = (h->options->flags & HLSL_PACK_MATRIX_ROW_MAJOR) != 0;
    s->refactoring_allowed = !(h->options->flags & HLSL_IEEE_STRICTNESS);
    s->cbuffer_count = prog->buffer_count;
    s->cbuffers = hl_alloc(h, (prog->buffer_count + 1) * sizeof(IrCbuffer));
    for (uint32_t i = 0; i < prog->buffer_count; i++)
        s->cbuffers[i].buffer = prog->buffers[i];
    if (ir_lower(s, entry))
        return -1;
    ir_optimize(s);
    HlBuf code = {};
    if (ir_emit_sm4(s, &code))
        return -1;
    if (h->errors)
        return -1;
    return ir_container(s, &code, &h->output);
}
